// SPDX-License-Identifier: BSD-3-Clause
#include "config.hpp"

#include <fstream>
#include <sstream>

namespace imx95 {

const char* to_string(GpuLevel l) {
    switch (l) {
        case GpuLevel::Off: return "off";
        case GpuLevel::Low: return "low";
        case GpuLevel::Mid: return "mid";
        case GpuLevel::Max: return "max";
    }
    return "off";
}

const char* to_string(VideoRes r) {
    switch (r) {
        case VideoRes::Off:    return "off";
        case VideoRes::R720p:  return "720p";
        case VideoRes::R1080p: return "1080p";
        case VideoRes::R4k:    return "4k";
    }
    return "off";
}

static GpuLevel gpu_from(const std::string& s) {
    if (s == "low") return GpuLevel::Low;
    if (s == "mid") return GpuLevel::Mid;
    if (s == "max") return GpuLevel::Max;
    return GpuLevel::Off;
}

static VideoRes res_from(const std::string& s) {
    if (s == "720p")  return VideoRes::R720p;
    if (s == "1080p") return VideoRes::R1080p;
    if (s == "4k")    return VideoRes::R4k;
    return VideoRes::Off;
}

std::string Config::summary() const {
    if (!any()) return "nothing configured";
    std::ostringstream os;
    bool first = true;
    auto add = [&](const std::string& s) {
        if (!first) os << "  ";
        os << s;
        first = false;
    };
    if (gpu != GpuLevel::Off) add(std::string("[GPU ") + to_string(gpu) + "]");
    if (dec != VideoRes::Off) add(std::string("[DEC ") + to_string(dec) + "]");
    if (enc != VideoRes::Off) add(std::string("[ENC ") + to_string(enc) + "]");
    if (npu) add("[NPU]");
    return os.str();
}

bool Config::save(const std::string& path, std::string& err) const {
    std::ofstream f(path, std::ios::trunc);
    if (!f) { err = "cannot open " + path + " for writing"; return false; }
    f << "gpu=" << to_string(gpu) << "\n"
      << "dec=" << to_string(dec) << "\n"
      << "enc=" << to_string(enc) << "\n"
      << "npu=" << (npu ? "1" : "0") << "\n";
    if (!f) { err = "write failed on " + path; return false; }
    return true;
}

bool Config::load(const std::string& path, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "cannot open " + path; return false; }
    Config tmp;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "gpu") tmp.gpu = gpu_from(v);
        else if (k == "dec") tmp.dec = res_from(v);
        else if (k == "enc") tmp.enc = res_from(v);
        else if (k == "npu") tmp.npu = (v == "1" || v == "on" || v == "true");
    }
    *this = tmp;
    return true;
}

} // namespace imx95
