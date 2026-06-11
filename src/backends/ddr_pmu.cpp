// SPDX-License-Identifier: BSD-3-Clause
//
// Real DDR bandwidth monitor: the i.MX9 DDR performance monitor exposed as a
// Linux perf PMU (driver fsl_imx9_ddr_perf, sysfs name imx9_ddr<N>). This is
// the headline cross-block-interference metric — it is global to the SoC, so it
// shows one block stealing memory bandwidth from another regardless of which
// block is busy.
//
// Read/write bandwidth comes from the "beat" counters:
//   i.MX95: read = eddrtq_pm_rd_beat_filt{0,1,2} (summed), write = ..._wr_beat_filt
//   i.MX93: read = eddrtq_pm_rd_beat_filt,        write = ..._wr_trans (transactions)
// Event configs are read from sysfs (events/<name>) so we adapt to either SoC.
// bytes = beats * IMX95_DDR_BEAT_BYTES (default 32; confirm against the RM).
//
// If the PMU is absent or perf_event_open is denied (e.g. on a dev host, or
// perf_event_paranoid too high), we fall back to the shared traffic-estimate
// bus — the same source the mock monitor uses — so -DIMX95_DDR=pmu runs
// everywhere and degrades cleanly.

#include <linux/perf_event.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <unistd.h>
#include <sys/syscall.h>
#include <vector>

#include "backend.hpp"
#include "backends/traffic_estimate.hpp"

namespace imx95 {

namespace {

const char* env_or(const char* k, const char* d) {
    const char* v = std::getenv(k);
    return (v && *v) ? v : d;
}

std::string read_file(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return "";
    char buf[256];
    size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    buf[n] = 0;
    return buf;
}

// Parse "event=0x49" (or "...,event=0x0249,...") -> config value.
bool parse_event_config(const std::string& s, uint64_t& cfg) {
    auto p = s.find("event=");
    if (p == std::string::npos) return false;
    cfg = std::strtoull(s.c_str() + p + 6, nullptr, 0);
    return true;
}

int first_cpu(const std::string& mask) {  // "0" | "0-3" | "0,2" -> first int
    return mask.empty() ? 0 : static_cast<int>(std::strtol(mask.c_str(), nullptr, 10));
}

long perf_open(perf_event_attr* attr, int cpu) {
    return syscall(__NR_perf_event_open, attr, /*pid*/ -1, cpu, /*group*/ -1, /*flags*/ 0UL);
}

class DdrPmu : public DdrMonitor {
public:
    bool init(std::string& err) override {
        (void)err;
        if (!open_pmu()) {
            std::printf("DDR PMU (imx9_ddr) unavailable — using estimated traffic instead.\n");
            real_ = false;
        }
        return true;  // always usable; falls back to the estimate bus
    }

    DdrSample sample() override {
        if (!real_) return traffic_estimate_read();
        DdrSample s;
        s.read_bytes = sum(rd_fds_) * beat_;
        s.write_bytes = sum(wr_fds_) * beat_;
        return s;
    }

    void shutdown() override {
        for (int fd : rd_fds_) ::close(fd);
        for (int fd : wr_fds_) ::close(fd);
        rd_fds_.clear();
        wr_fds_.clear();
    }

private:
    static uint64_t read_one(int fd) {
        uint64_t v = 0;
        if (::read(fd, &v, sizeof(v)) != static_cast<ssize_t>(sizeof(v))) return 0;
        return v;
    }
    static uint64_t sum(const std::vector<int>& fds) {
        uint64_t t = 0;
        for (int fd : fds) t += read_one(fd);
        return t;
    }

    bool open_pmu() {
        beat_ = std::strtoull(env_or("IMX95_DDR_BEAT_BYTES", "32"), nullptr, 0);
        std::string base = "/sys/bus/event_source/devices/";
        std::string name = env_or("IMX95_DDR_PMU", "");
        if (name.empty()) {  // discover the first imx9_ddr* PMU
            DIR* d = opendir(base.c_str());
            if (d) {
                for (dirent* e; (e = readdir(d));)
                    if (std::strncmp(e->d_name, "imx9_ddr", 8) == 0) { name = e->d_name; break; }
                closedir(d);
            }
        }
        if (name.empty()) return false;
        std::string dev = base + name + "/";

        uint32_t type = static_cast<uint32_t>(std::strtoul(read_file(dev + "type").c_str(), nullptr, 10));
        if (!type) return false;
        int cpu = first_cpu(read_file(dev + "cpumask"));

        // Match beat events by name (works for i.MX93 and i.MX95); fall back to
        // transaction events if no beat events are exposed.
        std::vector<std::string> rd, wr, rd_t, wr_t;
        std::string edir = dev + "events/";
        if (DIR* d = opendir(edir.c_str())) {
            for (dirent* e; (e = readdir(d));) {
                std::string n = e->d_name;
                if (n.find("rd_beat") != std::string::npos) rd.push_back(n);
                else if (n.find("wr_beat") != std::string::npos) wr.push_back(n);
                else if (n.find("rd_trans") != std::string::npos) rd_t.push_back(n);
                else if (n.find("wr_trans") != std::string::npos) wr_t.push_back(n);
            }
            closedir(d);
        }
        if (rd.empty()) rd = rd_t;
        if (wr.empty()) wr = wr_t;
        if (rd.empty() && wr.empty()) return false;

        auto open_set = [&](const std::vector<std::string>& names, std::vector<int>& fds) {
            for (const auto& n : names) {
                uint64_t cfg;
                if (!parse_event_config(read_file(edir + n), cfg)) continue;
                perf_event_attr attr{};
                attr.type = type;
                attr.size = sizeof(attr);
                attr.config = cfg;
                attr.disabled = 0;
                attr.inherit = 0;
                long fd = perf_open(&attr, cpu);
                if (fd >= 0) fds.push_back(static_cast<int>(fd));
            }
        };
        open_set(rd, rd_fds_);
        open_set(wr, wr_fds_);

        real_ = !rd_fds_.empty() || !wr_fds_.empty();
        if (!real_) return false;
        std::printf("DDR PMU: %s (type=%u cpu=%d) — %zu read + %zu write beat counters, %llu B/beat\n",
                    name.c_str(), type, cpu, rd_fds_.size(), wr_fds_.size(),
                    static_cast<unsigned long long>(beat_));
        return true;
    }

    bool real_ = false;
    uint64_t beat_ = 32;
    std::vector<int> rd_fds_, wr_fds_;
};

} // namespace

std::unique_ptr<DdrMonitor> make_ddr_monitor() { return std::make_unique<DdrPmu>(); }

const char* ddr_backend_name() { return "pmu"; }

} // namespace imx95
