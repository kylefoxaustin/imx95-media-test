// SPDX-License-Identifier: BSD-3-Clause
// Mock GPU workload: no GPU touched. Sleeps a plausible per-frame interval and
// bumps counters so the CLI/run-loop is exercised without silicon.

#include <chrono>
#include <thread>

#include "backend.hpp"
#include "backends/traffic_estimate.hpp"

namespace imx95 {

namespace {

class MockGpu : public Workload {
public:
    explicit MockGpu(GpuLevel lvl) {
        stats_.name = std::string("GPU ") + to_string(lvl);
        switch (lvl) {
            case GpuLevel::Low: w_ = 1280; h_ = 720;  frame_us_ = 1000000 / 480; break;
            case GpuLevel::Mid: w_ = 1920; h_ = 1080; frame_us_ = 1000000 / 160; break;
            case GpuLevel::Max: w_ = 3840; h_ = 2160; frame_us_ = 1000000 / 60;  break;
            default:            w_ = 1280; h_ = 720;  frame_us_ = 1000000 / 480; break;
        }
        bytes_per_frame_ = static_cast<uint64_t>(w_) * h_ * 4
                           * (lvl == GpuLevel::Max ? 6 : lvl == GpuLevel::Mid ? 4 : 2);
    }

    const char* kind() const override { return "GPU"; }
    uint64_t frames_per_loop() const override { return 600; }

    bool init(std::string&) override {
        stats_.alloc.store(static_cast<uint64_t>(w_) * h_ * 4 * 8);
        return true;
    }

    bool step() override {
        std::this_thread::sleep_for(std::chrono::microseconds(frame_us_));
        stats_.frames.fetch_add(1, std::memory_order_relaxed);
        stats_.bytes.fetch_add(bytes_per_frame_, std::memory_order_relaxed);
        traffic_estimate_add(bytes_per_frame_ * 6 / 10, bytes_per_frame_ * 4 / 10);
        return true;
    }

    void shutdown() override {}

private:
    int w_ = 0, h_ = 0;
    uint64_t frame_us_ = 0, bytes_per_frame_ = 0;
};

} // namespace

std::unique_ptr<Workload> make_gpu_workload(GpuLevel lvl) {
    return std::make_unique<MockGpu>(lvl);
}

const char* gpu_backend_name() { return "mock"; }

} // namespace imx95
