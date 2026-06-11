// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <memory>
#include <string>

#include "config.hpp"
#include "stats.hpp"

namespace imx95 {

// A Workload runs on its own thread. The runner calls step() in a tight loop;
// each call performs one unit of work (render / decode / encode a frame) and
// bumps stats().frames. step() must be short so a stop request stays
// responsive. It returns false only when the workload can do no more work
// (e.g. a non-looping clip ended); the runner handles loop counting itself.
class Workload {
public:
    virtual ~Workload() = default;

    virtual const char* kind() const = 0;  // "GPU" | "DEC" | "ENC"
    virtual bool init(std::string& err) = 0;
    virtual bool step() = 0;
    virtual void shutdown() = 0;

    // Frames that constitute one "loop": a full clip pass for VPU, a fixed
    // frame budget for the GPU. Used to honour run-once / run-N-loops.
    virtual uint64_t frames_per_loop() const = 0;

    WorkStats* stats() { return &stats_; }

protected:
    WorkStats stats_;
};

// Global SoC DDR traffic monitor (real impl wraps the i.MX9 DDR PMU).
class DdrMonitor {
public:
    virtual ~DdrMonitor() = default;
    virtual bool init(std::string& err) = 0;
    virtual DdrSample sample() = 0;  // cumulative since init()
    virtual void shutdown() = 0;
};

// Backend factories. The host build links the mock implementations; a target
// build (-DIMX95_TARGET=ON) links the real GLES / V4L2 / DDR-PMU ones behind
// these same signatures.
std::unique_ptr<Workload>   make_gpu_workload(GpuLevel lvl);
std::unique_ptr<Workload>   make_decode_workload(VideoRes res);
std::unique_ptr<Workload>   make_encode_workload(VideoRes res);
std::unique_ptr<DdrMonitor> make_ddr_monitor();

const char* backend_name();  // "mock" | "real"

} // namespace imx95
