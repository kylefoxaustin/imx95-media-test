// SPDX-License-Identifier: BSD-3-Clause
#include "runner.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <csignal>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "backend.hpp"
#include "term.hpp"

namespace imx95 {

namespace {

using Clock = std::chrono::steady_clock;

// Set by SIGINT/SIGTERM. Async-signal-safe: a plain atomic store.
std::atomic<bool> g_interrupted{false};

void on_signal(int) { g_interrupted.store(true); }

double secs_since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

std::string fmt_bytes_gb(uint64_t b) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f GB", b / 1e9);
    return buf;
}

std::string fmt_rate_gbs(double bytes_per_s) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f GB/s", bytes_per_s / 1e9);
    return buf;
}

// Per-workload state. init()/step()/shutdown() ALL run on this worker's own
// thread because some backends (notably EGL/GLES) bind resources to the
// creating thread — doing init() elsewhere makes every later GPU call a no-op.
struct Track {
    Workload* w;
    uint64_t last_frames = 0;
    std::atomic<bool> inited{false};
    bool ok = false;
    std::string err;
};

void worker(Track* t, uint64_t target_loops, std::atomic<bool>* stop, std::atomic<bool>* go,
            std::atomic<int>* running) {
    t->ok = t->w->init(t->err);
    t->inited.store(true, std::memory_order_release);
    if (!t->ok) { running->fetch_sub(1, std::memory_order_relaxed); return; }

    while (!go->load(std::memory_order_acquire) && !stop->load(std::memory_order_relaxed))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    const uint64_t fpl = t->w->frames_per_loop();
    while (!stop->load(std::memory_order_relaxed)) {
        if (!t->w->step()) break;
        if (target_loops && fpl &&
            t->w->stats()->frames.load(std::memory_order_relaxed) >= target_loops * fpl)
            break;
    }
    t->w->shutdown();
    running->fetch_sub(1, std::memory_order_relaxed);
}

using Tracks = std::vector<std::unique_ptr<Track>>;

const char* mode_str(uint64_t target_loops) {
    static std::string s;
    if (target_loops == 0) return "continuous";
    if (target_loops == 1) return "once";
    s = std::to_string(target_loops) + " loops";
    return s.c_str();
}

// Draws the dashboard once. Returns the lines already cleared via home cursor.
void draw(const Tracks& tracks, double elapsed,
          const std::vector<double>& fps, double ddr_r_bps, double ddr_w_bps,
          const DdrSample& ddr_total, uint64_t target_loops) {
    term_home();
    int mm = static_cast<int>(elapsed) / 60;
    int ss = static_cast<int>(elapsed) % 60;
    std::printf("RUNNING  elapsed %02d:%02d   mode %-10s   [space]=menu  [Ctrl-C]=stop",
                mm, ss, mode_str(target_loops));
    term_clear_to_eol();
    std::printf("\n");
    term_clear_to_eol();
    std::printf("\n");

    for (size_t i = 0; i < tracks.size(); ++i) {
        uint64_t frames = tracks[i]->w->stats()->frames.load(std::memory_order_relaxed);
        std::printf("  %-10s %8.1f fps   frames %10llu", tracks[i]->w->stats()->name.c_str(),
                    fps[i], static_cast<unsigned long long>(frames));
        term_clear_to_eol();
        std::printf("\n");
    }

    std::printf("  %-10s R %s  W %s  Σ %s", "DDR",
                fmt_rate_gbs(ddr_r_bps).c_str(), fmt_rate_gbs(ddr_w_bps).c_str(),
                fmt_rate_gbs(ddr_r_bps + ddr_w_bps).c_str());
    term_clear_to_eol();
    std::printf("\n    (total moved: %s)",
                fmt_bytes_gb(ddr_total.read_bytes + ddr_total.write_bytes).c_str());
    term_clear_to_eol();
    std::printf("\n");
    std::fflush(stdout);
}

// Blocking space-menu overlay. Returns 'r' resume, 's' stop, 'q' quit-app.
char pause_overlay() {
    std::printf("\n  -- paused --  r) resume   s) stop & report   q) quit app  : ");
    std::fflush(stdout);
    for (;;) {
        int c = term_getch();
        if (c == 'r' || c == 's' || c == 'q') {
            std::printf("%c\n", c);
            std::fflush(stdout);
            return static_cast<char>(c);
        }
        if (g_interrupted.load()) return 's';
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void print_report(const Tracks& tracks, double elapsed,
                  const DdrSample& ddr, uint64_t target_loops, RunOutcome outcome) {
    std::printf("\n==== Run report ====\n");
    std::printf("backends: gpu:%s vpu:%s ddr:%s   mode: %s   elapsed: %.2f s   ended: %s\n",
                gpu_backend_name(), vpu_backend_name(), ddr_backend_name(),
                mode_str(target_loops), elapsed,
                outcome == RunOutcome::Completed ? "completed"
                : outcome == RunOutcome::QuitApp ? "quit"
                                                 : "stopped");
    for (const auto& t : tracks) {
        auto* s = t->w->stats();
        uint64_t frames = s->frames.load();
        uint64_t bytes = s->bytes.load();
        double avg_fps = elapsed > 0 ? frames / elapsed : 0.0;
        std::printf("  %-10s frames %10llu   avg %7.1f fps   moved %s   footprint %s\n",
                    s->name.c_str(), static_cast<unsigned long long>(frames), avg_fps,
                    fmt_bytes_gb(bytes).c_str(), fmt_bytes_gb(s->alloc.load()).c_str());
    }
    double ddr_avg = elapsed > 0 ? (ddr.read_bytes + ddr.write_bytes) / elapsed : 0.0;
    std::printf("  %-10s read %s   write %s   total %s   avg %s\n", "DDR",
                fmt_bytes_gb(ddr.read_bytes).c_str(), fmt_bytes_gb(ddr.write_bytes).c_str(),
                fmt_bytes_gb(ddr.read_bytes + ddr.write_bytes).c_str(),
                fmt_rate_gbs(ddr_avg).c_str());
    std::printf("====================\n\n");
}

} // namespace

void install_signal_handlers() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
}

RunOutcome run_workloads(const Config& cfg, uint64_t target_loops) {
    // Build the configured workloads + the DDR monitor.
    std::vector<std::unique_ptr<Workload>> loads;
    if (cfg.gpu != GpuLevel::Off) loads.push_back(make_gpu_workload(cfg.gpu));
    if (cfg.dec != VideoRes::Off) loads.push_back(make_decode_workload(cfg.dec));
    if (cfg.enc != VideoRes::Off) loads.push_back(make_encode_workload(cfg.enc));
    auto ddr = make_ddr_monitor();

    if (loads.empty()) {
        std::printf("Nothing configured to run.\n");
        return RunOutcome::Completed;
    }

    std::string err;
    if (!ddr->init(err)) {
        std::printf("DDR monitor unavailable (%s) — continuing without it.\n", err.c_str());
    }

    g_interrupted.store(false);
    std::atomic<bool> stop{false};
    std::atomic<bool> go{false};
    std::atomic<int> running{static_cast<int>(loads.size())};
    std::vector<std::thread> threads;
    Tracks tracks;
    for (auto& w : loads) {
        tracks.push_back(std::make_unique<Track>());
        tracks.back()->w = w.get();
    }

    // Each worker init()s on its own thread (resource-ownership correctness),
    // then waits at the `go` barrier. Acquiring real GPU/VPU resources (and the
    // decoder's in-memory bootstrap) can take a moment, so say so.
    std::printf("Preparing workloads...\n");
    std::fflush(stdout);
    for (auto& t : tracks)
        threads.emplace_back(worker, t.get(), target_loops, &stop, &go, &running);

    for (;;) {
        bool all = true;
        for (auto& t : tracks)
            if (!t->inited.load(std::memory_order_acquire)) all = false;
        if (all) break;
        if (g_interrupted.load()) { stop.store(true); break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    bool any_fail = false;
    for (auto& t : tracks)
        if (t->inited.load() && !t->ok) {
            std::printf("Failed to init %s: %s\n", t->w->stats()->name.c_str(), t->err.c_str());
            any_fail = true;
        }
    if (any_fail || g_interrupted.load()) {
        stop.store(true);
        go.store(true);  // release any workers still waiting at the barrier
        for (auto& th : threads) th.join();
        ddr->shutdown();
        return g_interrupted.load() ? RunOutcome::Stopped : RunOutcome::Completed;
    }

    // All up: start the clock and release the workers together.
    auto t0 = Clock::now();
    DdrSample base = ddr->sample();
    go.store(true, std::memory_order_release);

    RunOutcome outcome = RunOutcome::Completed;
    {
        RawMode raw;
        term_clear();
        auto last = Clock::now();
        DdrSample last_ddr = base;
        std::vector<uint64_t> last_frames(tracks.size(), 0);
        std::vector<double> fps(tracks.size(), 0.0);

        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));

            auto now = Clock::now();
            double dt = std::chrono::duration<double>(now - last).count();
            DdrSample cur = ddr->sample();

            for (size_t i = 0; i < tracks.size(); ++i) {
                uint64_t f = tracks[i]->w->stats()->frames.load(std::memory_order_relaxed);
                if (dt > 0) fps[i] = (f - last_frames[i]) / dt;
                last_frames[i] = f;
            }
            double ddr_r = dt > 0 ? (cur.read_bytes - last_ddr.read_bytes) / dt : 0.0;
            double ddr_w = dt > 0 ? (cur.write_bytes - last_ddr.write_bytes) / dt : 0.0;
            last = now;
            last_ddr = cur;

            DdrSample total{cur.read_bytes - base.read_bytes, cur.write_bytes - base.write_bytes};
            draw(tracks, secs_since(t0), fps, ddr_r, ddr_w, total, target_loops);

            if (g_interrupted.load()) { outcome = RunOutcome::Stopped; break; }
            if (running.load(std::memory_order_relaxed) == 0) {
                outcome = RunOutcome::Completed;
                break;
            }

            int c = term_getch();
            if (c == ' ') {
                char act = pause_overlay();
                if (act == 's') { outcome = RunOutcome::Stopped; break; }
                if (act == 'q') { outcome = RunOutcome::QuitApp; break; }
                last = Clock::now();              // don't count paused time as a huge dt
                last_ddr = ddr->sample();
                term_clear();
            } else if (c == 'q') {
                outcome = RunOutcome::QuitApp;
                break;
            }
        }
    }  // RawMode restored here

    stop.store(true);
    for (auto& t : threads) t.join();

    DdrSample end = ddr->sample();
    DdrSample moved{end.read_bytes - base.read_bytes, end.write_bytes - base.write_bytes};
    double elapsed = secs_since(t0);

    ddr->shutdown();  // workloads shut themselves down on their own threads

    print_report(tracks, elapsed, moved, target_loops, outcome);
    return outcome;
}

} // namespace imx95
