// SPDX-License-Identifier: BSD-3-Clause
//
// Mock backends: no GPU/VPU/PMU touched. They sleep for a plausible per-frame
// interval and bump counters, so the whole CLI / run-loop / stats / shutdown
// path is exercised on a dev host or under qemu-imx95 (which has no GPU/VPU).
// The real backends (-DIMX95_TARGET=ON) implement the same factories.

#include "backend.hpp"

#include <atomic>
#include <chrono>
#include <thread>

namespace imx95 {

namespace {

// Shared, process-global DDR traffic accumulator. Mock workloads add their
// per-frame byte traffic here; the mock monitor reports it, so bandwidth
// tracks whatever is actually running — the point of the harness.
std::atomic<uint64_t> g_ddr_read{0};
std::atomic<uint64_t> g_ddr_write{0};

void add_ddr(uint64_t read_b, uint64_t write_b) {
    g_ddr_read.fetch_add(read_b, std::memory_order_relaxed);
    g_ddr_write.fetch_add(write_b, std::memory_order_relaxed);
}

void sleep_us(uint64_t us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

struct ResProfile {
    int w, h;
    uint64_t frame_us;  // simulated time to produce one frame
};

ResProfile decode_profile(VideoRes r) {
    switch (r) {
        case VideoRes::R720p:  return {1280,  720, 1000000 / 240};
        case VideoRes::R1080p: return {1920, 1080, 1000000 / 120};
        case VideoRes::R4k:    return {3840, 2160, 1000000 / 60};
        default:               return {0, 0, 1000000 / 60};
    }
}

ResProfile encode_profile(VideoRes r) {
    // Encode is heavier than decode at the same resolution.
    switch (r) {
        case VideoRes::R720p:  return {1280,  720, 1000000 / 120};
        case VideoRes::R1080p: return {1920, 1080, 1000000 / 60};
        case VideoRes::R4k:    return {3840, 2160, 1000000 / 30};
        default:               return {0, 0, 1000000 / 30};
    }
}

// ---- GPU --------------------------------------------------------------------

class MockGpu : public Workload {
public:
    explicit MockGpu(GpuLevel lvl) : lvl_(lvl) {
        stats_.name = std::string("GPU ") + to_string(lvl);
        switch (lvl) {
            case GpuLevel::Low: w_ = 1280; h_ = 720;  frame_us_ = 1000000 / 480; break;
            case GpuLevel::Mid: w_ = 1920; h_ = 1080; frame_us_ = 1000000 / 160; break;
            case GpuLevel::Max: w_ = 3840; h_ = 2160; frame_us_ = 1000000 / 60;  break;
            default:            w_ = 1280; h_ = 720;  frame_us_ = 1000000 / 480; break;
        }
        // Heavier levels touch more render targets per frame.
        bytes_per_frame_ = static_cast<uint64_t>(w_) * h_ * 4
                           * (lvl == GpuLevel::Max ? 6 : lvl == GpuLevel::Mid ? 4 : 2);
    }

    const char* kind() const override { return "GPU"; }
    uint64_t frames_per_loop() const override { return 600; }

    bool init(std::string&) override {
        stats_.alloc.store(static_cast<uint64_t>(w_) * h_ * 4 * 8);  // FBOs + textures
        return true;
    }

    bool step() override {
        sleep_us(frame_us_);
        stats_.frames.fetch_add(1, std::memory_order_relaxed);
        stats_.bytes.fetch_add(bytes_per_frame_, std::memory_order_relaxed);
        add_ddr(bytes_per_frame_ * 6 / 10, bytes_per_frame_ * 4 / 10);
        return true;  // renders continuously
    }

    void shutdown() override {}

private:
    GpuLevel lvl_;
    int w_ = 0, h_ = 0;
    uint64_t frame_us_ = 0, bytes_per_frame_ = 0;
};

// ---- VPU (decode / encode) --------------------------------------------------

class MockVpu : public Workload {
public:
    MockVpu(const char* kind, std::string name, ResProfile p, bool produces_bitstream)
        : kind_(kind), prof_(p), produces_bitstream_(produces_bitstream) {
        stats_.name = std::move(name);
        // ~NV12 raw frame size; encode output is the (smaller) bitstream.
        raw_bytes_ = static_cast<uint64_t>(prof_.w) * prof_.h * 3 / 2;
    }

    const char* kind() const override { return kind_; }
    uint64_t frames_per_loop() const override { return 300; }  // ~clip length

    bool init(std::string&) override {
        stats_.alloc.store(raw_bytes_ * 8);  // V4L2 buffer pool
        return true;
    }

    bool step() override {
        sleep_us(prof_.frame_us);
        stats_.frames.fetch_add(1, std::memory_order_relaxed);
        uint64_t product = produces_bitstream_ ? raw_bytes_ / 20 : raw_bytes_;
        stats_.bytes.fetch_add(product, std::memory_order_relaxed);
        // Decode reads bitstream, writes raw frames; encode the reverse.
        if (produces_bitstream_) add_ddr(raw_bytes_, raw_bytes_ / 20);
        else                     add_ddr(raw_bytes_ / 20, raw_bytes_);
        return true;  // loops the clip
    }

    void shutdown() override {}

private:
    const char* kind_;
    ResProfile prof_;
    bool produces_bitstream_;
    uint64_t raw_bytes_ = 0;
};

// ---- DDR monitor ------------------------------------------------------------

class MockDdr : public DdrMonitor {
public:
    bool init(std::string&) override { return true; }
    DdrSample sample() override {
        return {g_ddr_read.load(std::memory_order_relaxed),
                g_ddr_write.load(std::memory_order_relaxed)};
    }
    void shutdown() override {}
};

} // namespace

std::unique_ptr<Workload> make_gpu_workload(GpuLevel lvl) {
    return std::make_unique<MockGpu>(lvl);
}

std::unique_ptr<Workload> make_decode_workload(VideoRes res) {
    return std::make_unique<MockVpu>("DEC", std::string("DEC ") + to_string(res),
                                     decode_profile(res), /*bitstream*/ false);
}

std::unique_ptr<Workload> make_encode_workload(VideoRes res) {
    return std::make_unique<MockVpu>("ENC", std::string("ENC ") + to_string(res),
                                     encode_profile(res), /*bitstream*/ true);
}

std::unique_ptr<DdrMonitor> make_ddr_monitor() {
    return std::make_unique<MockDdr>();
}

const char* backend_name() { return "mock"; }

} // namespace imx95
