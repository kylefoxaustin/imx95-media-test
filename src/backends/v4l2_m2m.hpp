// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <linux/videodev2.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace imx95 {

// Thin wrapper over a V4L2 mem2mem device (MMAP, multi-planar, single plane per
// buffer — enough for NV12 raw and elementary coded streams). Shared by the
// stateful decode and encode workloads. Designed against the kernel V4L2
// stateful codec uAPI, so it drives both the i.MX95 Wave VPU and the virtual
// `vicodec` test driver from the same code.

struct V4l2Plane {
    void* start = nullptr;
    size_t length = 0;
};

struct V4l2Buf {
    V4l2Plane plane;  // we use one plane per buffer
};

// dqbuf() result codes.
enum { DQ_ERR = -1, DQ_AGAIN = -2 };

class V4l2M2m {
public:
    ~V4l2M2m() { close_dev(); }

    bool open_dev(const std::string& path, std::string& err);
    void close_dev();
    int fd() const { return fd_; }

    // Scan /dev/videoN for an m2m device whose OUTPUT supports `out_fourcc` and
    // whose CAPTURE supports `cap_fourcc`. Returns the path, or "" if none.
    static std::string find_device(uint32_t out_fourcc, uint32_t cap_fourcc);

    bool has_format(uint32_t type, uint32_t fourcc) const;
    uint32_t first_raw_format(uint32_t type) const;  // first uncompressed fmt, or 0

    bool set_format(uint32_t type, uint32_t fourcc, int w, int h, uint32_t sizeimage,
                    std::string& err);
    bool get_format(uint32_t type, int& w, int& h, uint32_t& sizeimage, std::string& err);

    // Allocate + mmap `count` buffers for a queue. Returns granted count or -1.
    int setup_buffers(uint32_t type, int count, std::string& err);
    V4l2Buf& buf(uint32_t type, int idx);
    int buf_count(uint32_t type) const;

    bool streamon(uint32_t type, std::string& err);
    bool streamoff(uint32_t type);

    bool qbuf(uint32_t type, int idx, uint32_t bytesused, std::string& err);
    // Returns buffer index, or DQ_AGAIN / DQ_ERR. Fills bytesused for CAPTURE.
    int dqbuf(uint32_t type, uint32_t& bytesused, std::string& err);

    bool subscribe_source_change(std::string& err);
    // Returns the V4L2_EVENT_* type, or 0 if none / error.
    uint32_t dqevent();

    // poll() the device; out-params say what is ready.
    bool wait(int timeout_ms, bool& cap_ready, bool& out_ready, bool& event);

private:
    std::vector<V4l2Buf>& side(uint32_t type);
    const std::vector<V4l2Buf>& side(uint32_t type) const;

    int fd_ = -1;
    std::vector<V4l2Buf> out_;  // V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
    std::vector<V4l2Buf> cap_;  // V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
};

uint32_t codec_fourcc(const std::string& name);  // "h264"|"hevc"|"fwht" -> V4L2_PIX_FMT_*

} // namespace imx95
