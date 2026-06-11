// SPDX-License-Identifier: BSD-3-Clause
//
// Real VPU workloads via the V4L2 stateful mem2mem codec uAPI (no GStreamer).
//
//   encode: synthetic raw frames -> OUTPUT queue; coded frames <- CAPTURE queue
//   decode: coded frames -> OUTPUT queue; raw frames <- CAPTURE queue
//
// Both are *self-sourcing*, so no media files need to be uploaded to the board:
//   - encode feeds procedurally generated frames;
//   - decode bootstraps in init() by running a throwaway encode of a few frames
//     into memory, then loops that bitstream through the decoder.
//
// Codec is selectable via IMX95_VPU_CODEC (h264 | hevc | fwht); device nodes are
// auto-discovered or pinned with IMX95_VPU_{ENCODE,DECODE}_DEV. FWHT + the
// `vicodec` test driver let the whole path be validated on a host without a VPU
// (single-planar), while the i.MX95 Wave VPU (multi-planar) uses the same code.

#include <linux/videodev2.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "backend.hpp"
#include "backends/traffic_estimate.hpp"
#include "backends/v4l2_m2m.hpp"

namespace imx95 {

namespace {

constexpr int kNumBuf = 6;
constexpr int kBootstrapFrames = 60;  // coded frames the decoder loops over

const char* env_or(const char* key, const char* dflt) {
    const char* v = std::getenv(key);
    return (v && *v) ? v : dflt;
}

void resolution(VideoRes r, int& w, int& h) {
    switch (r) {
        case VideoRes::R720p:  w = 1280; h = 720;  break;
        case VideoRes::R1080p: w = 1920; h = 1080; break;
        case VideoRes::R4k:    w = 3840; h = 2160; break;
        default:               w = 1280; h = 720;  break;
    }
}

// Fill a buffer with a moving pattern so successive frames differ (gives the
// encoder real work). Pixel correctness is irrelevant to a throughput test.
void fill_synthetic(uint8_t* p, size_t len, uint64_t frame) {
    uint8_t base = static_cast<uint8_t>(frame * 3);
    for (size_t i = 0; i < len; ++i)
        p[i] = static_cast<uint8_t>(base + (i * 7) + (i >> 8));
}

struct EncSetup {
    V4l2M2m dev;
    int w = 0, h = 0;
    uint32_t raw_fourcc = 0, coded_fourcc = 0;
    uint32_t coded_size = 0;
};

bool encoder_open(EncSetup& e, VideoRes res, uint32_t coded_fourcc, std::string& err) {
    resolution(res, e.w, e.h);
    e.coded_fourcc = coded_fourcc;
    e.coded_size = static_cast<uint32_t>(e.w) * e.h * 2;  // >= any raw/compressed frame
    if (e.coded_size < (512u << 10)) e.coded_size = 512u << 10;

    const char* dev = env_or("IMX95_VPU_ENCODE_DEV", "");
    std::string path = *dev ? dev : V4l2M2m::find_device(V4L2_PIX_FMT_NV12, coded_fourcc);
    if (path.empty()) path = V4l2M2m::find_device(0, coded_fourcc);  // any raw on OUTPUT
    if (path.empty()) { err = "no V4L2 encoder device found for the requested codec"; return false; }
    if (!e.dev.open_dev(path, err)) return false;

    e.raw_fourcc = e.dev.has_format(Side::Output, V4L2_PIX_FMT_NV12)
                       ? V4L2_PIX_FMT_NV12
                       : e.dev.first_raw_format(Side::Output);
    if (!e.raw_fourcc) { err = "encoder OUTPUT has no raw format"; return false; }

    if (!e.dev.set_format(Side::Capture, coded_fourcc, e.w, e.h, e.coded_size, err)) return false;
    if (!e.dev.set_format(Side::Output, e.raw_fourcc, e.w, e.h, 0, err)) return false;
    if (e.dev.setup_buffers(Side::Output, kNumBuf, err) < 0) return false;
    if (e.dev.setup_buffers(Side::Capture, kNumBuf, err) < 0) return false;
    for (int i = 0; i < e.dev.buf_count(Side::Capture); ++i)
        if (!e.dev.qbuf(Side::Capture, i, 0, err)) return false;
    if (!e.dev.streamon(Side::Output, err)) return false;
    if (!e.dev.streamon(Side::Capture, err)) return false;
    return true;
}

// ---- Encode workload --------------------------------------------------------

class V4l2Encoder : public Workload {
public:
    V4l2Encoder(VideoRes res, uint32_t codec) : res_(res), codec_(codec) {
        stats_.name = std::string("ENC ") + to_string(res);
    }

    const char* kind() const override { return "ENC"; }
    uint64_t frames_per_loop() const override { return 300; }

    bool init(std::string& err) override {
        if (!encoder_open(e_, res_, codec_, err)) return false;
        for (int i = 0; i < e_.dev.buf_count(Side::Output); ++i) {  // prime raw inputs
            auto& b = e_.dev.buf(Side::Output, i);
            fill_synthetic(static_cast<uint8_t*>(b.start), b.length, frame_++);
            if (!e_.dev.qbuf(Side::Output, i, static_cast<uint32_t>(b.length), err)) return false;
        }
        stats_.alloc.store(static_cast<uint64_t>(e_.w) * e_.h * 3 / 2 * kNumBuf);
        return true;
    }

    bool step() override { return encode_one(nullptr); }

    void shutdown() override {
        e_.dev.streamoff(Side::Output);
        e_.dev.streamoff(Side::Capture);
        e_.dev.close_dev();
    }

    // Produce one coded frame. If sink != null, append a copy of it.
    bool encode_one(std::vector<std::vector<uint8_t>>* sink) {
        std::string err;
        for (int guard = 0; guard < 256; ++guard) {
            bool cap, out, ev;
            if (!e_.dev.wait(1000, cap, out, ev)) return false;
            if (out) {
                uint32_t used;
                int idx = e_.dev.dqbuf(Side::Output, used, err);
                if (idx >= 0) {
                    auto& b = e_.dev.buf(Side::Output, idx);
                    fill_synthetic(static_cast<uint8_t*>(b.start), b.length, frame_++);
                    e_.dev.qbuf(Side::Output, idx, static_cast<uint32_t>(b.length), err);
                }
            }
            if (cap) {
                uint32_t used = 0;
                int idx = e_.dev.dqbuf(Side::Capture, used, err);
                if (idx >= 0) {
                    if (sink) {
                        auto* p = static_cast<uint8_t*>(e_.dev.buf(Side::Capture, idx).start);
                        sink->emplace_back(p, p + used);
                    }
                    e_.dev.qbuf(Side::Capture, idx, 0, err);
                    stats_.frames.fetch_add(1, std::memory_order_relaxed);
                    stats_.bytes.fetch_add(used, std::memory_order_relaxed);
                    uint64_t raw = static_cast<uint64_t>(e_.w) * e_.h * 3 / 2;
                    traffic_estimate_add(raw, used);  // reads raw, writes coded
                    return true;
                }
            }
        }
        return false;
    }

    EncSetup& setup() { return e_; }

private:
    VideoRes res_;
    uint32_t codec_;
    EncSetup e_;
    uint64_t frame_ = 0;
};

// ---- Decode workload --------------------------------------------------------

class V4l2Decoder : public Workload {
public:
    V4l2Decoder(VideoRes res, uint32_t codec) : res_(res), codec_(codec) {
        stats_.name = std::string("DEC ") + to_string(res);
    }

    const char* kind() const override { return "DEC"; }
    uint64_t frames_per_loop() const override { return 300; }

    bool init(std::string& err) override {
        // 1. Bootstrap a bitstream into memory (zero external assets).
        {
            V4l2Encoder enc(res_, codec_);
            if (!enc.init(err)) { err = "decode bootstrap encode: " + err; return false; }
            for (int i = 0; i < kBootstrapFrames; ++i)
                if (!enc.encode_one(&coded_)) break;
            coded_size_ = enc.setup().coded_size;
            enc.shutdown();
        }
        if (coded_.empty()) { err = "decode bootstrap produced no coded frames"; return false; }

        // 2. Open the decoder and stream the coded buffers in.
        resolution(res_, w_, h_);
        const char* dev = env_or("IMX95_VPU_DECODE_DEV", "");
        std::string path = *dev ? dev : V4l2M2m::find_device(codec_, V4L2_PIX_FMT_NV12);
        if (path.empty()) path = V4l2M2m::find_device(codec_, 0);
        if (path.empty()) { err = "no V4L2 decoder device found for the requested codec"; return false; }
        if (!dev_.open_dev(path, err)) return false;

        if (!dev_.set_format(Side::Output, codec_, w_, h_, coded_size_, err)) return false;
        if (!dev_.subscribe_source_change(err)) return false;
        if (dev_.setup_buffers(Side::Output, kNumBuf, err) < 0) return false;
        if (!dev_.streamon(Side::Output, err)) return false;

        // Feed coded frames until the driver signals the capture resolution.
        for (int i = 0; i < dev_.buf_count(Side::Output); ++i) feed_coded(i);
        for (int guard = 0; guard < 512 && !cap_ready_; ++guard) {
            bool cap, out, ev;
            if (!dev_.wait(1000, cap, out, ev)) return false;
            if (ev && dev_.dqevent() == V4L2_EVENT_SOURCE_CHANGE)
                if (!start_capture(err)) return false;
            if (out) {
                uint32_t used;
                int idx = dev_.dqbuf(Side::Output, used, err);
                if (idx >= 0) feed_coded(idx);
            }
        }
        if (!cap_ready_) { err = "decoder never signalled SOURCE_CHANGE"; return false; }
        stats_.alloc.store(static_cast<uint64_t>(w_) * h_ * 3 / 2 * kNumBuf);
        return true;
    }

    bool step() override {
        std::string err;
        for (int guard = 0; guard < 256; ++guard) {
            bool cap, out, ev;
            if (!dev_.wait(1000, cap, out, ev)) return false;
            if (ev) dev_.dqevent();  // drain (e.g. EOS); resolution already known
            if (out) {
                uint32_t used;
                int idx = dev_.dqbuf(Side::Output, used, err);
                if (idx >= 0) feed_coded(idx);
            }
            if (cap) {
                uint32_t used = 0;
                int idx = dev_.dqbuf(Side::Capture, used, err);
                if (idx >= 0) {
                    dev_.qbuf(Side::Capture, idx, 0, err);
                    stats_.frames.fetch_add(1, std::memory_order_relaxed);
                    uint64_t raw = static_cast<uint64_t>(w_) * h_ * 3 / 2;
                    stats_.bytes.fetch_add(raw, std::memory_order_relaxed);
                    traffic_estimate_add(coded_size_ / 10, raw);  // reads coded, writes raw
                    return true;
                }
            }
        }
        return false;
    }

    void shutdown() override {
        dev_.streamoff(Side::Output);
        dev_.streamoff(Side::Capture);
        dev_.close_dev();
    }

private:
    void feed_coded(int idx) {
        std::string err;
        const auto& src = coded_[coded_idx_];
        coded_idx_ = (coded_idx_ + 1) % coded_.size();
        auto& b = dev_.buf(Side::Output, idx);
        size_t n = src.size() < b.length ? src.size() : b.length;
        std::memcpy(b.start, src.data(), n);
        dev_.qbuf(Side::Output, idx, static_cast<uint32_t>(n), err);
    }

    bool start_capture(std::string& err) {
        uint32_t sz;
        if (!dev_.get_format(Side::Capture, w_, h_, sz, err)) return false;
        if (dev_.setup_buffers(Side::Capture, kNumBuf, err) < 0) return false;
        for (int i = 0; i < dev_.buf_count(Side::Capture); ++i)
            if (!dev_.qbuf(Side::Capture, i, 0, err)) return false;
        if (!dev_.streamon(Side::Capture, err)) return false;
        cap_ready_ = true;
        return true;
    }

    VideoRes res_;
    uint32_t codec_;
    V4l2M2m dev_;
    int w_ = 0, h_ = 0;
    uint32_t coded_size_ = 0;
    std::vector<std::vector<uint8_t>> coded_;
    size_t coded_idx_ = 0;
    bool cap_ready_ = false;
};

uint32_t selected_codec() {
    uint32_t c = codec_fourcc(env_or("IMX95_VPU_CODEC", "h264"));
    return c ? c : V4L2_PIX_FMT_H264;
}

} // namespace

std::unique_ptr<Workload> make_decode_workload(VideoRes res) {
    return std::make_unique<V4l2Decoder>(res, selected_codec());
}

std::unique_ptr<Workload> make_encode_workload(VideoRes res) {
    return std::make_unique<V4l2Encoder>(res, selected_codec());
}

const char* vpu_backend_name() { return "v4l2"; }

} // namespace imx95
