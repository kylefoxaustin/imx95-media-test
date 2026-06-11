// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <linux/videodev2.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace imx95 {

// Thin wrapper over a V4L2 mem2mem codec device (MMAP, one plane per buffer —
// enough for NV12 raw and elementary coded streams). Transparently supports
// both the single-planar and multi-planar APIs, chosen per device at open():
// the kernel `vicodec` test driver is single-planar, the i.MX95 Wave VPU is
// multi-planar, and the same workload code drives either via Side::{Output,
// Capture}.

enum class Side { Output, Capture };

// dqbuf() result codes.
enum { DQ_ERR = -1, DQ_AGAIN = -2 };

struct V4l2Buf {
    void* start = nullptr;
    size_t length = 0;
};

class V4l2M2m {
public:
    ~V4l2M2m() { close_dev(); }

    bool open_dev(const std::string& path, std::string& err);
    void close_dev();
    int fd() const { return fd_; }
    bool multiplanar() const { return mplane_; }

    // Scan /dev/videoN for an m2m device whose OUTPUT supports `out_fourcc` and
    // CAPTURE supports `cap_fourcc` (fourcc 0 means "any uncompressed format").
    // Returns the path, or "" if none.
    static std::string find_device(uint32_t out_fourcc, uint32_t cap_fourcc);

    bool has_format(Side s, uint32_t fourcc) const;
    uint32_t first_raw_format(Side s) const;  // first uncompressed fmt, or 0

    bool set_format(Side s, uint32_t fourcc, int w, int h, uint32_t sizeimage, std::string& err);
    bool get_format(Side s, int& w, int& h, uint32_t& sizeimage, std::string& err);

    // Allocate + mmap `count` buffers for a side. Returns granted count or -1.
    int setup_buffers(Side s, int count, std::string& err);
    V4l2Buf& buf(Side s, int idx);
    int buf_count(Side s) const;

    bool streamon(Side s, std::string& err);
    bool streamoff(Side s);

    bool qbuf(Side s, int idx, uint32_t bytesused, std::string& err);
    int dqbuf(Side s, uint32_t& bytesused, std::string& err);  // index, or DQ_AGAIN / DQ_ERR

    bool subscribe_source_change(std::string& err);
    uint32_t dqevent();  // V4L2_EVENT_* type, or 0

    bool wait(int timeout_ms, bool& cap_ready, bool& out_ready, bool& event);

    uint32_t buf_type(Side s) const;

private:
    std::vector<V4l2Buf>& side(Side s);
    const std::vector<V4l2Buf>& side(Side s) const;

    int fd_ = -1;
    bool mplane_ = false;
    std::vector<V4l2Buf> out_;
    std::vector<V4l2Buf> cap_;
};

uint32_t codec_fourcc(const std::string& name);  // "h264"|"hevc"|"fwht" -> V4L2_PIX_FMT_*

} // namespace imx95
