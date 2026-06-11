// SPDX-License-Identifier: BSD-3-Clause
#include "menu.hpp"

#include <cstdio>
#include <ctime>
#include <iostream>
#include <string>

#include "backend.hpp"
#include "config.hpp"
#include "runner.hpp"

#ifndef IMX95_VERSION
#define IMX95_VERSION "dev"
#endif

namespace imx95 {

namespace {

const char* kConfigPath = "imx95-test.cfg";

std::string read_line(const char* prompt) {
    std::fputs(prompt, stdout);
    std::fflush(stdout);
    std::string s;
    if (!std::getline(std::cin, s)) {
        // EOF (e.g. piped/closed stdin): behave like quit.
        return "q";
    }
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

void rule() { std::puts("--------------------------------------------------"); }

// Radio picker for the GPU level. Selection applies immediately and stays
// visible; 'b' returns. No submit/clear ceremony.
void pick_gpu(Config& cfg) {
    for (;;) {
        rule();
        std::printf("GPU workload   selected: %s        (b = back & save)\n", to_string(cfg.gpu));
        std::puts("  1) off");
        std::puts("  2) low   (light load)");
        std::puts("  3) mid   (moderate load)");
        std::puts("  4) max   (heavy - pegs the GPU; tune with IMX95_GPU_* env vars)");
        std::string c = read_line("> ");
        if (c == "1") cfg.gpu = GpuLevel::Off;
        else if (c == "2") cfg.gpu = GpuLevel::Low;
        else if (c == "3") cfg.gpu = GpuLevel::Mid;
        else if (c == "4") cfg.gpu = GpuLevel::Max;
        else if (c == "b" || c == "q") return;
    }
}

// Radio picker for a single VPU resolution (decode or encode). Picking a new
// resolution replaces the prior one, so "4k AND 1080p decode" is impossible.
void pick_res(VideoRes& slot, const char* title) {
    for (;;) {
        rule();
        std::printf("%s   selected: %s        (b = back & save)\n", title, to_string(slot));
        std::puts("  1) off");
        std::puts("  2) 720p");
        std::puts("  3) 1080p");
        std::puts("  4) 4k");
        std::string c = read_line("> ");
        if (c == "1") slot = VideoRes::Off;
        else if (c == "2") slot = VideoRes::R720p;
        else if (c == "3") slot = VideoRes::R1080p;
        else if (c == "4") slot = VideoRes::R4k;
        else if (c == "b" || c == "q") return;
    }
}

void configure_menu(Config& cfg) {
    for (;;) {
        rule();
        std::puts("Configure workloads              (b = back, keeps selections)");
        std::printf("  1) GPU ........... %s\n", to_string(cfg.gpu));
        std::printf("  2) VPU decode .... %s\n", to_string(cfg.dec));
        std::printf("  3) VPU encode .... %s\n", to_string(cfg.enc));
        std::puts("  c) Clear all");
        std::string c = read_line("> ");
        if (c == "1") pick_gpu(cfg);
        else if (c == "2") pick_res(cfg.dec, "VPU decode");
        else if (c == "3") pick_res(cfg.enc, "VPU encode");
        else if (c == "c") cfg.clear();
        else if (c == "b" || c == "q") return;
    }
}

void list_selected(const Config& cfg) {
    std::puts("Ready to run:");
    if (cfg.gpu != GpuLevel::Off) std::printf("  - GPU %s\n", to_string(cfg.gpu));
    if (cfg.dec != VideoRes::Off) std::printf("  - VPU decode %s\n", to_string(cfg.dec));
    if (cfg.enc != VideoRes::Off) std::printf("  - VPU encode %s\n", to_string(cfg.enc));
}

// Prompt for a loop count; returns the count, or -1 if the user backed out.
long ask_loop_count() {
    for (;;) {
        std::string s = read_line("How many loops? (number, b = back) > ");
        if (s == "b" || s == "q" || s.empty()) return -1;
        try {
            long n = std::stol(s);
            if (n > 0) return n;
        } catch (...) {
        }
        std::puts("Please enter a positive number, or b to go back.");
    }
}

std::string default_log_name() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    char buf[64];
    std::strftime(buf, sizeof(buf), "imx95-run-%Y%m%d-%H%M%S.log", &tmv);
    return buf;
}

// Configure a detached background run that logs to a file, then launch it.
void detached_run_menu(const Config& cfg) {
    uint64_t loops = 0;
    for (;;) {
        rule();
        std::puts("Detached run - runs in the background, logs to a file (terminal stays free)");
        std::puts("  1) Continuous   (stop later with: kill <pid>)");
        std::puts("  2) Once");
        std::puts("  3) Fixed count...");
        std::puts("  b) Back");
        std::string c = read_line("> ");
        if (c == "1") { loops = 0; break; }
        else if (c == "2") { loops = 1; break; }
        else if (c == "3") {
            long n = ask_loop_count();
            if (n < 0) continue;
            loops = static_cast<uint64_t>(n);
            break;
        } else if (c == "b" || c == "q") {
            return;
        }
    }
    std::string def = default_log_name();
    std::string lp = read_line(("Log file name (press Enter for " + def + ") > ").c_str());
    if (lp.empty()) lp = def;
    std::string err;
    if (run_detached(cfg, loops, lp, err))
        std::puts("Running in the background. Manage it from the main menu -> Detached runs.");
    else
        std::printf("Detached run failed: %s\n", err.c_str());
}

// List the detached runs launched this session and let the user stop them
// in-app (so there's no need to drop to a shell to `kill` a PID).
void detached_runs_menu() {
    for (;;) {
        rule();
        auto runs = detached_runs();
        if (runs.empty()) {
            std::puts("No detached runs launched this session.");
            std::puts("  (Run -> 4 starts one in the background.)   b) Back");
        } else {
            std::puts("Detached runs launched this session:");
            for (size_t i = 0; i < runs.size(); ++i) {
                bool alive = detached_alive(runs[i].pid);
                std::printf("  %zu) pid %-6ld %-7s %-8s %s   log: %s\n", i + 1, runs[i].pid,
                            alive ? "ALIVE" : "exited",
                            runs[i].continuous ? "(cont)" : "(finite)",
                            runs[i].config.c_str(), runs[i].log.c_str());
            }
            std::puts("  enter a number to STOP that run, a) stop all, b) Back");
        }
        std::string c = read_line("> ");
        if (c == "b" || c == "q") return;
        if (c == "a") {
            for (auto& r : runs)
                if (detached_alive(r.pid)) stop_detached(r.pid);
            std::puts("Sent stop to all running detached jobs (they write a final report to their log).");
            continue;
        }
        try {
            size_t idx = std::stoul(c);
            if (idx >= 1 && idx <= runs.size()) {
                stop_detached(runs[idx - 1].pid);
                std::printf("Sent stop to pid %ld — it will write its final report to %s\n",
                            runs[idx - 1].pid, runs[idx - 1].log.c_str());
            }
        } catch (...) {
        }
    }
}

// Returns true if the user chose to quit the whole app.
bool ask_fixed_count_and_run(const Config& cfg) {
    for (;;) {
        std::string s = read_line("How many loops? (number, b = back) > ");
        if (s == "b" || s == "q" || s.empty()) return false;
        try {
            long n = std::stol(s);
            if (n > 0) return run_workloads(cfg, static_cast<uint64_t>(n)) == RunOutcome::QuitApp;
        } catch (...) {
        }
        std::puts("Please enter a positive number, or b to go back.");
    }
}

// Returns true if the user chose to quit the whole app.
bool run_menu(const Config& cfg) {
    if (!cfg.any()) {
        std::puts("\nNothing configured yet — choose 1) Configure workloads first.\n");
        return false;
    }
    for (;;) {
        rule();
        list_selected(cfg);
        std::puts("Run mode:                        (b = back)");
        std::puts("  1) Continuous (until Ctrl-C / space-menu quit)");
        std::puts("  2) Once (each workload one full pass)");
        std::puts("  3) Fixed count...");
        std::puts("  4) Detached -> log file (runs in background, terminal stays free)");
        std::string c = read_line("> ");
        RunOutcome out = RunOutcome::Completed;
        if (c == "1") out = run_workloads(cfg, 0);
        else if (c == "2") out = run_workloads(cfg, 1);
        else if (c == "3") { if (ask_fixed_count_and_run(cfg)) return true; else continue; }
        else if (c == "4") { detached_run_menu(cfg); return false; }
        else if (c == "b" || c == "q") return false;
        else continue;
        if (out == RunOutcome::QuitApp) return true;
        return false;  // back to top menu after a run
    }
}

void loadsave_menu(Config& cfg) {
    for (;;) {
        rule();
        std::printf("Load / Save config   (file: %s)        (b = back)\n", kConfigPath);
        std::puts("  1) Save current config");
        std::puts("  2) Load config");
        std::string c = read_line("> ");
        std::string err;
        if (c == "1") {
            if (cfg.save(kConfigPath, err)) std::printf("Saved to %s\n", kConfigPath);
            else std::printf("Save failed: %s\n", err.c_str());
        } else if (c == "2") {
            if (cfg.load(kConfigPath, err)) std::printf("Loaded: %s\n", cfg.summary().c_str());
            else std::printf("Load failed: %s\n", err.c_str());
        } else if (c == "b" || c == "q") {
            return;
        }
    }
}

} // namespace

// Succinct operating guide, paginated so it never scrolls off in one blast.
void show_help() {
    static const char* p1 = R"(i.MX95 Media Test Framework - quick help   (1/3)

WHAT IT DOES
  Apply heavy GPU / VPU workloads (alone or together) and measure how they
  affect each other - chiefly via global DDR memory bandwidth.

MAIN MENU
  1) Configure   choose which blocks to run, and how hard
  2) Run         start the selected workloads
  3) Detached    view / stop runs launched in the background
  4) Load/Save   store or reload your configuration
  h) Help (this screen)     q) Quit

CONFIGURE
  GPU:  off / low / mid / max.
  VPU decode and encode are independent (run both) and each is one
  resolution: 720p / 1080p / 4k. A choice applies immediately; 'b' = back.)";

    static const char* p2 = R"(i.MX95 Media Test Framework - quick help   (2/3)

RUN MODES  (menu 2)
  1) Continuous   runs until you stop it
  2) Once         each workload does one full pass
  3) Fixed count  a set number of loops
  4) Detached     runs in the background, logs to a file (menu stays free)

DURING A LIVE RUN
  space    open a menu: resume / stop & report / quit
  Ctrl-C   stop gracefully
  Either way you get a per-workload + DDR report when it ends.

DETACHED RUNS
  Start:  Run -> 4, pick a mode, press Enter for the default log name.
          You return to the menu and can launch more runs alongside it.
  Stop:   main menu -> Detached runs -> a number (or 'a' for all),
          or from a shell: kill <pid> (printed when it starts).
  Watch:  tail -f <logfile>.   Each run writes a final report to its log.)";

    static const char* p3 = R"(i.MX95 Media Test Framework - quick help   (3/3)

WHAT THE NUMBERS MEAN
  Per workload : frames, average fps, bytes moved, buffer footprint.
  DDR          : global SoC read/write bandwidth - the cross-block
                 interference signal. Compare a block alone vs. with others.

TUNING  (environment variables, set before launching the program)
  GPU load : IMX95_GPU_W _H _SUBDIV _INSTANCES _LIGHTS _EXTRA _PASSES
  VPU      : IMX95_VPU_CODEC=h264|hevc   IMX95_VPU_STREAM=<file.h264>
  DDR      : IMX95_DDR_BEAT_BYTES=32 (confirm against the reference manual)
  GPU disp : IMX95_EGL_PLATFORM=gbm|default   IMX95_DRM_DEVICE=/dev/dri/cardN

  Run as root so the DDR PMU and codec/GPU device nodes are accessible.)";

    const char* pages[] = {p1, p2, p3};
    for (int i = 0; i < 3; ++i) {
        rule();
        std::puts(pages[i]);
        if (i < 2) {
            if (read_line("\n-- Enter for more, q to stop -- ") == "q") return;
        } else {
            read_line("\n-- end of help, press Enter -- ");
        }
    }
}

void run_app() {
    install_signal_handlers();
    Config cfg;
    std::printf("\n== i.MX95 Media Test Framework v%s ==   (backends: gpu:%s vpu:%s ddr:%s)\n",
                IMX95_VERSION, gpu_backend_name(), vpu_backend_name(), ddr_backend_name());

    for (;;) {
        rule();
        std::printf("config:  %s\n", cfg.summary().c_str());
        int active = 0;
        for (auto& r : detached_runs())
            if (detached_alive(r.pid)) ++active;
        std::puts("  1) Configure workloads");
        std::puts("  2) Run");
        std::printf("  3) Detached runs%s\n", active ? (" (" + std::to_string(active) + " active)").c_str() : "");
        std::puts("  4) Load / Save config");
        std::puts("  h) Help");
        std::puts("  q) Quit");
        std::string c = read_line("> ");
        if (c == "1") configure_menu(cfg);
        else if (c == "2") { if (run_menu(cfg)) break; }
        else if (c == "3") detached_runs_menu();
        else if (c == "4") loadsave_menu(cfg);
        else if (c == "h" || c == "?") show_help();
        else if (c == "q") break;
    }
    std::puts("\nBye.");
}

} // namespace imx95
