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
#include <string>
#include <sys/stat.h>
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

class BenchNpu : public Workload {
public:
    BenchNpu() { stats_.name = "NPU"; }
    const char* kind() const override { return "NPU"; }
    uint64_t frames_per_loop() const override { return runs_; }

    bool init(std::string& err) override {
        bench_ = find_benchmark();
        if (bench_.empty()) { err = "benchmark_model not found; set IMX95_NPU_BENCH=<path>"; return false; }
        const char* m = std::getenv("IMX95_NPU_MODEL");
        if (!m || !*m) { err = "set IMX95_NPU_MODEL=<neutron-compiled .tflite>"; return false; }
        model_ = m;
        if (!file_exists(model_)) { err = "NPU model not found: " + model_; return false; }
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
        int rc = pclose(f);
        if (count > 0) return static_cast<long>(count);
        if (rc != 0) return -1;                       // crashed / failed, no inferences
        return static_cast<long>(runs);               // ran but parse missed; assume requested
    }

    std::string bench_, model_, delegate_;
    uint64_t runs_ = 500;
};

} // namespace

std::unique_ptr<Workload> make_npu_workload() { return std::make_unique<BenchNpu>(); }

const char* npu_backend_name() { return "bench"; }

CheckResult npu_check() {
    // Surface the eIQ alignment picture: the running firmware's build, any
    // converter present on the rootfs(es), and whether they match — a mismatch
    // is what makes a converted model segfault the driver at execution.
    NeutronAlign al = neutron_alignment();
    std::string fwctx;
    if (!al.firmware.empty()) fwctx += "  [firmware " + al.firmware + "]";

    auto w = make_npu_workload();  // init() locates tools/model + probes one inference
    std::string err;
    if (w->init(err)) {
        w->shutdown();
        return {true, "Neutron inference OK" + fwctx};  // it runs — no version warning needed
    }
    // Failed: add the eIQ-alignment context that usually explains a crash —
    // the firmware build and any (possibly mismatched) on-board converter.
    std::string ctx = fwctx;
    if (!al.converter.empty()) {
        ctx += " [on-board converter " + al.converter + "]";
        if (!al.firmware.empty() && !al.matched) ctx += " (!!) version MISMATCH";
        else if (al.matched) ctx += " — matches firmware; convert on target via menu 'n'";
    }
    return {false, err + ctx};
}

} // namespace imx95
