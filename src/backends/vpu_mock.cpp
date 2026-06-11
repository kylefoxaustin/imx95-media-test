// SPDX-License-Identifier: BSD-3-Clause
// Mock VPU workloads (decode / encode): no VPU touched.

#include <chrono>
#include <thread>

#include "backend.hpp"
#include "backends/traffic_estimate.hpp"

namespace imx95 {

namespace {

struct ResProfile {
    int w, h;
    uint64_t frame_us;
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
    switch (r) {
        case VideoRes::R720p:  return {1280,  720, 1000000 / 120};
        case VideoRes::R1080p: return {1920, 1080, 1000000 / 60};
        case VideoRes::R4k:    return {3840, 2160, 1000000 / 30};
        default:               return {0, 0, 1000000 / 30};
    }
}

class MockVpu : public Workload {
public:
    MockVpu(const char* kind, std::string name, ResProfile p, bool produces_bitstream)
        : kind_(kind), prof_(p), produces_bitstream_(produces_bitstream) {
        stats_.name = std::move(name);
        raw_bytes_ = static_cast<uint64_t>(prof_.w) * prof_.h * 3 / 2;  // ~NV12
    }

    const char* kind() const override { return kind_; }
    uint64_t frames_per_loop() const override { return 300; }  // ~clip length

    bool init(std::string&) override {
        stats_.alloc.store(raw_bytes_ * 8);  // V4L2 buffer pool
        return true;
    }

    bool step() override {
        std::this_thread::sleep_for(std::chrono::microseconds(prof_.frame_us));
        stats_.frames.fetch_add(1, std::memory_order_relaxed);
        uint64_t product = produces_bitstream_ ? raw_bytes_ / 20 : raw_bytes_;
        stats_.bytes.fetch_add(product, std::memory_order_relaxed);
        if (produces_bitstream_) traffic_estimate_add(raw_bytes_, raw_bytes_ / 20);
        else                     traffic_estimate_add(raw_bytes_ / 20, raw_bytes_);
        return true;
    }

    void shutdown() override {}

private:
    const char* kind_;
    ResProfile prof_;
    bool produces_bitstream_;
    uint64_t raw_bytes_ = 0;
};

} // namespace

std::unique_ptr<Workload> make_decode_workload(VideoRes res) {
    return std::make_unique<MockVpu>("DEC", std::string("DEC ") + to_string(res),
                                     decode_profile(res), /*bitstream*/ false);
}

std::unique_ptr<Workload> make_encode_workload(VideoRes res) {
    return std::make_unique<MockVpu>("ENC", std::string("ENC ") + to_string(res),
                                     encode_profile(res), /*bitstream*/ true);
}

const char* vpu_backend_name() { return "mock"; }

} // namespace imx95
