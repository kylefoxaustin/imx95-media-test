// SPDX-License-Identifier: BSD-3-Clause
// Mock NPU workload: simulates a quantized-CNN inference loop.

#include <chrono>
#include <thread>

#include "backend.hpp"
#include "backends/traffic_estimate.hpp"

namespace imx95 {

namespace {

class MockNpu : public Workload {
public:
    MockNpu() { stats_.name = "NPU"; }
    const char* kind() const override { return "NPU"; }
    uint64_t frames_per_loop() const override { return 500; }

    bool init(std::string&) override {
        stats_.alloc.store(4u << 20);  // ~MobileNet weights
        return true;
    }

    bool step() override {
        std::this_thread::sleep_for(std::chrono::microseconds(1000000 / 300));  // ~300 inf/s
        stats_.frames.fetch_add(1, std::memory_order_relaxed);
        uint64_t bytes = (4u << 20) + 224 * 224 * 3;  // weights + input
        stats_.bytes.fetch_add(1000, std::memory_order_relaxed);
        traffic_estimate_add(bytes, 4096);
        return true;
    }

    void shutdown() override {}
};

} // namespace

std::unique_ptr<Workload> make_npu_workload() { return std::make_unique<MockNpu>(); }

const char* npu_backend_name() { return "mock"; }

CheckResult npu_check() { return {true, "mock backend (simulated NPU)"}; }

} // namespace imx95
