// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <atomic>
#include <cstdint>
#include <string>

namespace imx95 {

// Live counters a running workload updates from its own thread. Read
// concurrently by the dashboard thread, so the hot fields are atomic.
struct WorkStats {
    std::string name;                  // "GPU mid", "DEC 1080p", "ENC 4k"
    std::atomic<uint64_t> frames{0};   // frames rendered / decoded / encoded
    std::atomic<uint64_t> bytes{0};    // work product moved (for throughput)
    std::atomic<uint64_t> alloc{0};    // peak buffer footprint, bytes

    WorkStats() = default;
    explicit WorkStats(std::string n) : name(std::move(n)) {}
};

// Cumulative DDR byte counters read from the memory-controller PMU
// (global to the SoC — the cross-block interference signal).
struct DdrSample {
    uint64_t read_bytes = 0;
    uint64_t write_bytes = 0;
};

} // namespace imx95
