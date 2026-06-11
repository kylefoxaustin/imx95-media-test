// SPDX-License-Identifier: BSD-3-Clause
#include "backends/v4l2_m2m.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace imx95 {

namespace {

int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

// Format enumeration helpers usable before an instance exists (find_device).
bool fmt_has(int fd, uint32_t type, uint32_t fourcc) {
    for (uint32_t i = 0;; ++i) {
        v4l2_fmtdesc d{};
        d.index = i;
        d.type = type;
        if (xioctl(fd, VIDIOC_ENUM_FMT, &d) != 0) return false;
        if (d.pixelformat == fourcc) return true;
    }
}

uint32_t fmt_first_raw(int fd, uint32_t type) {
    for (uint32_t i = 0;; ++i) {
        v4l2_fmtdesc d{};
        d.index = i;
        d.type = type;
        if (xioctl(fd, VIDIOC_ENUM_FMT, &d) != 0) return 0;
        if (!(d.flags & V4L2_FMT_FLAG_COMPRESSED)) return d.pixelformat;
    }
}

uint32_t device_caps(int fd) {
    v4l2_capability cap{};
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) != 0) return 0;
    return (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
}

}  // namespace

uint32_t codec_fourcc(const std::string& name) {
    if (name == "h264") return V4L2_PIX_FMT_H264;
    if (name == "hevc" || name == "h265") return V4L2_PIX_FMT_HEVC;
    if (name == "fwht") return V4L2_PIX_FMT_FWHT;
    return 0;
}

uint32_t V4l2M2m::buf_type(Side s) const {
    if (s == Side::Output)
        return mplane_ ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT;
    return mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
}

std::vector<V4l2Buf>& V4l2M2m::side(Side s) { return s == Side::Capture ? cap_ : out_; }
const std::vector<V4l2Buf>& V4l2M2m::side(Side s) const { return s == Side::Capture ? cap_ : out_; }
V4l2Buf& V4l2M2m::buf(Side s, int idx) { return side(s)[idx]; }
int V4l2M2m::buf_count(Side s) const { return static_cast<int>(side(s).size()); }

bool V4l2M2m::open_dev(const std::string& path, std::string& err) {
    fd_ = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0) { err = "open " + path + ": " + std::strerror(errno); return false; }
    mplane_ = device_caps(fd_) & V4L2_CAP_VIDEO_M2M_MPLANE;
    return true;
}

void V4l2M2m::close_dev() {
    auto unmap = [](std::vector<V4l2Buf>& v) {
        for (auto& b : v)
            if (b.start && b.start != MAP_FAILED) munmap(b.start, b.length);
        v.clear();
    };
    unmap(out_);
    unmap(cap_);
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

bool V4l2M2m::has_format(Side s, uint32_t fourcc) const { return fmt_has(fd_, buf_type(s), fourcc); }
uint32_t V4l2M2m::first_raw_format(Side s) const { return fmt_first_raw(fd_, buf_type(s)); }

bool V4l2M2m::set_format(Side s, uint32_t fourcc, int w, int h, uint32_t sizeimage,
                         std::string& err) {
    v4l2_format f{};
    f.type = buf_type(s);
    if (mplane_) {
        f.fmt.pix_mp.width = w;
        f.fmt.pix_mp.height = h;
        f.fmt.pix_mp.pixelformat = fourcc;
        f.fmt.pix_mp.num_planes = 1;
        f.fmt.pix_mp.field = V4L2_FIELD_NONE;
        if (sizeimage) f.fmt.pix_mp.plane_fmt[0].sizeimage = sizeimage;
    } else {
        f.fmt.pix.width = w;
        f.fmt.pix.height = h;
        f.fmt.pix.pixelformat = fourcc;
        f.fmt.pix.field = V4L2_FIELD_NONE;
        if (sizeimage) f.fmt.pix.sizeimage = sizeimage;
    }
    if (xioctl(fd_, VIDIOC_S_FMT, &f) != 0) {
        err = "S_FMT: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
}

bool V4l2M2m::get_format(Side s, int& w, int& h, uint32_t& sizeimage, std::string& err) {
    v4l2_format f{};
    f.type = buf_type(s);
    if (xioctl(fd_, VIDIOC_G_FMT, &f) != 0) {
        err = "G_FMT: " + std::string(std::strerror(errno));
        return false;
    }
    if (mplane_) {
        w = f.fmt.pix_mp.width;
        h = f.fmt.pix_mp.height;
        sizeimage = f.fmt.pix_mp.plane_fmt[0].sizeimage;
    } else {
        w = f.fmt.pix.width;
        h = f.fmt.pix.height;
        sizeimage = f.fmt.pix.sizeimage;
    }
    return true;
}

int V4l2M2m::setup_buffers(Side s, int count, std::string& err) {
    v4l2_requestbuffers rb{};
    rb.count = count;
    rb.type = buf_type(s);
    rb.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_REQBUFS, &rb) != 0) {
        err = "REQBUFS: " + std::string(std::strerror(errno));
        return -1;
    }
    auto& v = side(s);
    v.assign(rb.count, V4l2Buf{});
    for (uint32_t i = 0; i < rb.count; ++i) {
        v4l2_plane planes[VIDEO_MAX_PLANES]{};
        v4l2_buffer b{};
        b.type = buf_type(s);
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        if (mplane_) { b.length = 1; b.m.planes = planes; }
        if (xioctl(fd_, VIDIOC_QUERYBUF, &b) != 0) {
            err = "QUERYBUF: " + std::string(std::strerror(errno));
            return -1;
        }
        size_t len = mplane_ ? planes[0].length : b.length;
        off_t off = mplane_ ? planes[0].m.mem_offset : b.m.offset;
        void* p = mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, off);
        if (p == MAP_FAILED) { err = "mmap: " + std::string(std::strerror(errno)); return -1; }
        v[i].start = p;
        v[i].length = len;
    }
    return static_cast<int>(rb.count);
}

bool V4l2M2m::streamon(Side s, std::string& err) {
    uint32_t t = buf_type(s);
    if (xioctl(fd_, VIDIOC_STREAMON, &t) != 0) {
        err = "STREAMON: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
}

bool V4l2M2m::streamoff(Side s) {
    uint32_t t = buf_type(s);
    return xioctl(fd_, VIDIOC_STREAMOFF, &t) == 0;
}

bool V4l2M2m::qbuf(Side s, int idx, uint32_t bytesused, std::string& err) {
    v4l2_plane planes[VIDEO_MAX_PLANES]{};
    v4l2_buffer b{};
    b.type = buf_type(s);
    b.memory = V4L2_MEMORY_MMAP;
    b.index = idx;
    if (mplane_) {
        b.length = 1;
        b.m.planes = planes;
        planes[0].bytesused = bytesused;
        planes[0].length = static_cast<uint32_t>(side(s)[idx].length);
    } else {
        b.bytesused = bytesused;
        b.length = static_cast<uint32_t>(side(s)[idx].length);
    }
    if (xioctl(fd_, VIDIOC_QBUF, &b) != 0) {
        err = "QBUF: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
}

int V4l2M2m::dqbuf(Side s, uint32_t& bytesused, std::string& err) {
    v4l2_plane planes[VIDEO_MAX_PLANES]{};
    v4l2_buffer b{};
    b.type = buf_type(s);
    b.memory = V4L2_MEMORY_MMAP;
    if (mplane_) { b.length = 1; b.m.planes = planes; }
    if (xioctl(fd_, VIDIOC_DQBUF, &b) != 0) {
        if (errno == EAGAIN) return DQ_AGAIN;
        err = "DQBUF: " + std::string(std::strerror(errno));
        return DQ_ERR;
    }
    bytesused = mplane_ ? planes[0].bytesused : b.bytesused;
    return b.index;
}

bool V4l2M2m::subscribe_source_change(std::string& err) {
    v4l2_event_subscription sub{};
    sub.type = V4L2_EVENT_SOURCE_CHANGE;
    if (xioctl(fd_, VIDIOC_SUBSCRIBE_EVENT, &sub) != 0) {
        err = "SUBSCRIBE_EVENT: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
}

uint32_t V4l2M2m::dqevent() {
    v4l2_event ev{};
    if (xioctl(fd_, VIDIOC_DQEVENT, &ev) != 0) return 0;
    return ev.type;
}

bool V4l2M2m::wait(int timeout_ms, bool& cap_ready, bool& out_ready, bool& event) {
    pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN | POLLOUT | POLLPRI;
    cap_ready = out_ready = event = false;
    int r = poll(&pfd, 1, timeout_ms);
    if (r <= 0) return r == 0;  // 0 = timeout (not an error)
    cap_ready = pfd.revents & POLLIN;
    out_ready = pfd.revents & POLLOUT;
    event = pfd.revents & POLLPRI;
    return true;
}

std::string V4l2M2m::find_device(uint32_t out_fourcc, uint32_t cap_fourcc) {
    for (int i = 0; i < 64; ++i) {
        char path[32];
        std::snprintf(path, sizeof(path), "/dev/video%d", i);
        int fd = ::open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
        uint32_t c = device_caps(fd);
        bool mp = c & V4L2_CAP_VIDEO_M2M_MPLANE;
        bool sp = c & V4L2_CAP_VIDEO_M2M;
        bool match = false;
        if (mp || sp) {
            uint32_t outT = mp ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT;
            uint32_t capT = mp ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
            auto ok = [&](uint32_t t, uint32_t fcc) {
                return fcc ? fmt_has(fd, t, fcc) : fmt_first_raw(fd, t) != 0;
            };
            match = ok(outT, out_fourcc) && ok(capT, cap_fourcc);
        }
        ::close(fd);
        if (match) return path;
    }
    return "";
}

} // namespace imx95
