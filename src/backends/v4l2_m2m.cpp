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
}  // namespace

uint32_t codec_fourcc(const std::string& name) {
    if (name == "h264") return V4L2_PIX_FMT_H264;
    if (name == "hevc" || name == "h265") return V4L2_PIX_FMT_HEVC;
    if (name == "fwht") return V4L2_PIX_FMT_FWHT;
    return 0;
}

bool V4l2M2m::open_dev(const std::string& path, std::string& err) {
    fd_ = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0) { err = "open " + path + ": " + std::strerror(errno); return false; }
    return true;
}

void V4l2M2m::close_dev() {
    auto unmap = [](std::vector<V4l2Buf>& v) {
        for (auto& b : v)
            if (b.plane.start && b.plane.start != MAP_FAILED) munmap(b.plane.start, b.plane.length);
        v.clear();
    };
    unmap(out_);
    unmap(cap_);
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

std::vector<V4l2Buf>& V4l2M2m::side(uint32_t type) {
    return type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? cap_ : out_;
}
const std::vector<V4l2Buf>& V4l2M2m::side(uint32_t type) const {
    return type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? cap_ : out_;
}
V4l2Buf& V4l2M2m::buf(uint32_t type, int idx) { return side(type)[idx]; }
int V4l2M2m::buf_count(uint32_t type) const { return static_cast<int>(side(type).size()); }

bool V4l2M2m::has_format(uint32_t type, uint32_t fourcc) const {
    for (uint32_t i = 0;; ++i) {
        v4l2_fmtdesc d{};
        d.index = i;
        d.type = type;
        if (xioctl(fd_, VIDIOC_ENUM_FMT, &d) != 0) return false;
        if (d.pixelformat == fourcc) return true;
    }
}

uint32_t V4l2M2m::first_raw_format(uint32_t type) const {
    for (uint32_t i = 0;; ++i) {
        v4l2_fmtdesc d{};
        d.index = i;
        d.type = type;
        if (xioctl(fd_, VIDIOC_ENUM_FMT, &d) != 0) return 0;
        if (!(d.flags & V4L2_FMT_FLAG_COMPRESSED)) return d.pixelformat;
    }
}

bool V4l2M2m::set_format(uint32_t type, uint32_t fourcc, int w, int h, uint32_t sizeimage,
                         std::string& err) {
    v4l2_format f{};
    f.type = type;
    f.fmt.pix_mp.width = w;
    f.fmt.pix_mp.height = h;
    f.fmt.pix_mp.pixelformat = fourcc;
    f.fmt.pix_mp.num_planes = 1;
    f.fmt.pix_mp.field = V4L2_FIELD_NONE;
    if (sizeimage) f.fmt.pix_mp.plane_fmt[0].sizeimage = sizeimage;
    if (xioctl(fd_, VIDIOC_S_FMT, &f) != 0) {
        err = "S_FMT: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
}

bool V4l2M2m::get_format(uint32_t type, int& w, int& h, uint32_t& sizeimage, std::string& err) {
    v4l2_format f{};
    f.type = type;
    if (xioctl(fd_, VIDIOC_G_FMT, &f) != 0) {
        err = "G_FMT: " + std::string(std::strerror(errno));
        return false;
    }
    w = f.fmt.pix_mp.width;
    h = f.fmt.pix_mp.height;
    sizeimage = f.fmt.pix_mp.plane_fmt[0].sizeimage;
    return true;
}

int V4l2M2m::setup_buffers(uint32_t type, int count, std::string& err) {
    v4l2_requestbuffers rb{};
    rb.count = count;
    rb.type = type;
    rb.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_REQBUFS, &rb) != 0) {
        err = "REQBUFS: " + std::string(std::strerror(errno));
        return -1;
    }
    auto& v = side(type);
    v.assign(rb.count, V4l2Buf{});
    for (uint32_t i = 0; i < rb.count; ++i) {
        v4l2_plane planes[VIDEO_MAX_PLANES]{};
        v4l2_buffer b{};
        b.type = type;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        b.length = 1;
        b.m.planes = planes;
        if (xioctl(fd_, VIDIOC_QUERYBUF, &b) != 0) {
            err = "QUERYBUF: " + std::string(std::strerror(errno));
            return -1;
        }
        size_t len = planes[0].length;
        void* p = mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, planes[0].m.mem_offset);
        if (p == MAP_FAILED) { err = "mmap: " + std::string(std::strerror(errno)); return -1; }
        v[i].plane.start = p;
        v[i].plane.length = len;
    }
    return static_cast<int>(rb.count);
}

bool V4l2M2m::streamon(uint32_t type, std::string& err) {
    if (xioctl(fd_, VIDIOC_STREAMON, &type) != 0) {
        err = "STREAMON: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
}

bool V4l2M2m::streamoff(uint32_t type) { return xioctl(fd_, VIDIOC_STREAMOFF, &type) == 0; }

bool V4l2M2m::qbuf(uint32_t type, int idx, uint32_t bytesused, std::string& err) {
    v4l2_plane planes[VIDEO_MAX_PLANES]{};
    v4l2_buffer b{};
    b.type = type;
    b.memory = V4L2_MEMORY_MMAP;
    b.index = idx;
    b.length = 1;
    b.m.planes = planes;
    planes[0].bytesused = bytesused;
    planes[0].length = side(type)[idx].plane.length;
    if (xioctl(fd_, VIDIOC_QBUF, &b) != 0) {
        err = "QBUF: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
}

int V4l2M2m::dqbuf(uint32_t type, uint32_t& bytesused, std::string& err) {
    v4l2_plane planes[VIDEO_MAX_PLANES]{};
    v4l2_buffer b{};
    b.type = type;
    b.memory = V4L2_MEMORY_MMAP;
    b.length = 1;
    b.m.planes = planes;
    if (xioctl(fd_, VIDIOC_DQBUF, &b) != 0) {
        if (errno == EAGAIN) return DQ_AGAIN;
        err = "DQBUF: " + std::string(std::strerror(errno));
        return DQ_ERR;
    }
    bytesused = planes[0].bytesused;
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
        v4l2_capability cap{};
        bool m2m = false;
        if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
            uint32_t c = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps
                                                                   : cap.capabilities;
            m2m = c & (V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_VIDEO_M2M);
        }
        bool match = false;
        if (m2m) {
            V4l2M2m probe;
            probe.fd_ = fd;
            match = probe.has_format(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, out_fourcc) &&
                    probe.has_format(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, cap_fourcc);
            probe.fd_ = -1;  // don't let probe's dtor close our fd
        }
        ::close(fd);
        if (match) return path;
    }
    return "";
}

} // namespace imx95
