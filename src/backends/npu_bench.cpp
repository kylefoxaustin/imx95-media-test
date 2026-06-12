// SPDX-License-Identifier: BSD-3-Clause
//
// Real NPU workload: the eIQ Neutron NPU runs a quantized TFLite model via the
// Neutron delegate. The TFLite runtime + delegate live on the board (not a
// dlopen-friendly C API), so rather than embed a megabyte ML stack we drive the
// platform's own `benchmark_model`: each step() runs a batch of inferences
// (model + Neutron external delegate) and counts them. The global DDR PMU
// captures the NPU's memory traffic, so it slots into the interference harness
// like the other blocks.
//
// The model MUST be neutron-converted with the converter version that MATCHES
// the board's BSP (firmware + libNeutronDriver) — a mismatch makes the driver
// crash in model-prepare even though the delegate partitions the graph. init()
// runs a 1-inference probe and fails with guidance if the model does not
// execute. Configure with:
//   IMX95_NPU_MODEL=<neutron .tflite>   (required, BSP-matched)
//   IMX95_NPU_BENCH=<benchmark_model>   (else auto-discovered)
//   IMX95_NPU_DELEGATE=<...>            (default /usr/lib/libneutron_delegate.so)
//   IMX95_NPU_RUNS=<inferences/batch>   (default 500)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <glob.h>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "backend.hpp"
#include "backends/traffic_estimate.hpp"
#include "neutron_convert.hpp"

namespace imx95 {

namespace {

bool file_exists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

std::string find_benchmark() {
    if (const char* e = std::getenv("IMX95_NPU_BENCH"))
        if (*e) return e;
    if (file_exists("/usr/bin/benchmark_model")) return "/usr/bin/benchmark_model";
    const char* vers[] = {"2.19.0", "2.16.2", "2.14.0", "2.10.0", "2.9.1"};
    for (auto v : vers) {
        std::string p = std::string("/usr/bin/tensorflow-lite-") + v + "/examples/benchmark_model";
        if (file_exists(p)) return p;
    }
    return "";
}

// Directory holding the running binary (so a model uploaded *next to* it is
// found even when the program is launched from elsewhere).
std::string exe_dir() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return "";
    buf[n] = '\0';
    std::string p(buf);
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? "" : p.substr(0, s);
}

// Resolve which model to run. `IMX95_NPU_MODEL` always wins (explicit path). With
// it unset, auto-detect by CONTENT: scan the cwd and the binary's directory for
// .tflite files that are actually neutron-converted (carry Neutron ops). This is
// name-agnostic — rename the model freely, and plain/un-converted .tflite files
// in the same folder are ignored. Exactly one converted model -> use it; zero or
// several -> return "" with guidance in `err` (so multiple models stay explicit).
// `how` describes the resolution for reporting ("IMX95_NPU_MODEL" / "auto-detected").
std::string resolve_npu_model(std::string& how, std::string& err) {
    if (const char* m = std::getenv("IMX95_NPU_MODEL")) {
        if (*m) {
            if (!file_exists(m)) { err = std::string("NPU model not found: ") + m; return ""; }
            how = "IMX95_NPU_MODEL";
            return m;
        }
    }
    // Auto-detect: gather converted .tflite candidates from cwd + exe dir.
    std::vector<std::string> dirs = {"."};
    std::string ed = exe_dir();
    if (!ed.empty() && ed != ".") dirs.push_back(ed);

    std::vector<std::string> hits;
    std::set<std::string> seen;  // dedup by canonical path (cwd may == exe dir)
    for (const auto& d : dirs) {
        glob_t g{};
        if (glob((d + "/*.tflite").c_str(), 0, nullptr, &g) == 0) {
            for (size_t i = 0; i < g.gl_pathc; ++i) {
                std::string p = g.gl_pathv[i];
                if (!tflite_is_converted(p)) continue;
                char rp[4096];
                std::string key = realpath(p.c_str(), rp) ? rp : p;
                if (seen.insert(key).second) hits.push_back(p);
            }
        }
        globfree(&g);
    }
    if (hits.empty()) {
        err = "no NPU model. Upload a neutron-converted .tflite next to the binary, "
              "or convert one with menu 'n', or set IMX95_NPU_MODEL=<path>.";
        return "";
    }
    if (hits.size() > 1) {
        // Show HOW to pick one (env var in front of the command), with a real
        // filename filled in, then list the candidates.
        std::string list;
        for (const auto& h : hits) list += "\n      " + h;
        err = "multiple converted models found — pin one by launching with the model\n"
              "    in front of the command, e.g.:\n"
              "      IMX95_NPU_MODEL=" + hits[0] + " ./imx95-test\n"
              "    candidates:" + list;
        return "";
    }
    how = "auto-detected";
    return hits[0];
}

// " (converter 2.2.3, target imx95)" for a converted model, or "" if it carries
// no version metadata — appended to status lines so you see exactly what ran.
std::string model_version_suffix(const std::string& path) {
    NeutronModelInfo mi = neutron_model_info(path);
    if (mi.version.empty() && mi.target.empty()) return "";
    std::string s = " (";
    if (!mi.version.empty()) s += "converter " + mi.version;
    if (!mi.target.empty()) s += (mi.version.empty() ? "" : ", ") + std::string("target ") + mi.target;
    s += ")";
    return s;
}

class BenchNpu : public Workload {
public:
    BenchNpu() { stats_.name = "NPU"; }
    const char* kind() const override { return "NPU"; }
    uint64_t frames_per_loop() const override { return runs_; }

    bool init(std::string& err) override {
        bench_ = find_benchmark();
        if (bench_.empty()) { err = "benchmark_model not found; set IMX95_NPU_BENCH=<path>"; return false; }
        std::string how;
        model_ = resolve_npu_model(how, err);
        if (model_.empty()) return false;  // err already set with guidance
        if (how == "auto-detected")
            std::fprintf(stderr, "[NPU] using auto-detected converted model: %s%s\n",
                         model_.c_str(), model_version_suffix(model_).c_str());
        const char* d = std::getenv("IMX95_NPU_DELEGATE");
        delegate_ = (d && *d) ? d : "/usr/lib/libneutron_delegate.so";
        if (const char* r = std::getenv("IMX95_NPU_RUNS")) {
            long v = std::atol(r);
            if (v > 0) runs_ = static_cast<uint64_t>(v);
        }
        // Probe: confirm the model actually executes on the NPU (a converter
        // that doesn't match this board's BSP makes the driver crash here).
        if (run_batch(1) < 0) {
            err = "NPU model did not execute via the Neutron delegate. The .tflite must be "
                  "neutron-converted with the converter version matching THIS board's BSP "
                  "(firmware/libNeutronDriver). See docs/BOARD.md.";
            return false;
        }
        stats_.alloc.store(4u << 20);  // ~model weights
        return true;
    }

    bool step() override {
        long n = run_batch(runs_);
        if (n < 0) return false;
        stats_.frames.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
        uint64_t per = (4u << 20) + 224 * 224 * 3;  // weights + input per inference
        stats_.bytes.fetch_add(per * n, std::memory_order_relaxed);
        traffic_estimate_add(per * n, 4096 * n);
        return true;
    }

    void shutdown() override {}

private:
    // Run `runs` inferences via benchmark_model; returns the inference count, or
    // -1 if the process failed/crashed (e.g. driver/converter mismatch).
    long run_batch(uint64_t runs) {
        std::string cmd = "'" + bench_ + "' --graph='" + model_ + "' --external_delegate_path='" +
                          delegate_ + "' --num_runs=" + std::to_string(runs) +
                          " --warmup_runs=0 --min_secs=0 2>&1";
        FILE* f = popen(cmd.c_str(), "r");
        if (!f) return -1;
        char line[512];
        uint64_t count = 0;
        while (std::fgets(line, sizeof(line), f)) {
            if (const char* p = std::strstr(line, "count=")) {
                uint64_t c = std::strtoull(p + 6, nullptr, 10);
                if (c > count) count = c;  // the main benchmark run is the largest count
            }
        }
        pclose(f);  // exit status is unreliable: the harness sets SIGCHLD=SIG_IGN
                    // (auto-reaping detached runs), so pclose() returns -1/ECHILD
                    // even on a clean exit. The inference count is the real signal.
        // benchmark_model prints "count=N" only once inferences actually run; a
        // converter/firmware mismatch crashes at model-prepare BEFORE any
        // inference, so it emits no count -> we report failure.
        return count > 0 ? static_cast<long>(count) : -1;
    }

    std::string bench_, model_, delegate_;
    uint64_t runs_ = 500;
};

} // namespace

std::unique_ptr<Workload> make_npu_workload() { return std::make_unique<BenchNpu>(); }

const char* npu_backend_name() { return "bench"; }

namespace {

// eIQ-alignment context appended to a failed NPU check: the firmware build and
// any (possibly mismatched) on-board converter — usually the reason a model crashes.
std::string align_context(const NeutronAlign& al) {
    std::string ctx;
    if (!al.firmware.empty()) ctx += "  [firmware " + al.firmware + "]";
    if (!al.converter.empty()) {
        ctx += " [on-board converter " + al.converter + "]";
        if (!al.firmware.empty() && !al.matched) ctx += " (!!) version MISMATCH";
        else if (al.matched) ctx += " — matches firmware; convert on target via menu 'n'";
    }
    return ctx;
}

std::string base_name(const std::string& p) {
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

}  // namespace

CheckResult npu_check() {
    NeutronAlign al = neutron_alignment();

    // Which model would a run use? (explicit env, or an auto-detected converted
    // model beside the binary). Report it so the user sees what's being probed.
    std::string how, rerr;
    std::string model = resolve_npu_model(how, rerr);
    if (model.empty())
        return {false, rerr + align_context(al)};  // no model to probe

    auto w = make_npu_workload();  // init() re-resolves the same model + probes 1 inference
    std::string err;
    if (w->init(err)) {
        w->shutdown();
        std::string fwctx;
        if (!al.firmware.empty()) fwctx = "  [firmware " + al.firmware + "]";
        return {true, "Neutron inference OK (" + how + ": " + base_name(model) + ")" +
                          model_version_suffix(model) + fwctx};
    }
    // Model present but the probe failed — surface the likely mismatch context.
    return {false, err + align_context(al)};
}

} // namespace imx95
