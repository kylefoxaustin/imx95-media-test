// SPDX-License-Identifier: BSD-3-Clause
//
// Shared Neutron conversion + eIQ-alignment helpers. Always compiled (the menu
// uses it regardless of which NPU backend is built), so it links only libdl and
// touches the board's Neutron stack lazily — on a host it simply finds nothing
// and every entry point degrades gracefully.

#include "neutron_convert.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <glob.h>
#include <vector>

namespace imx95 {

std::string neutron_build_stamp(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return "";
    std::string s;
    char buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        s.append(buf, n);
        if (s.size() > (48u << 20)) break;  // cap (converter .so can be ~10 MB)
    }
    std::fclose(f);
    size_t cv = s.find("clang version");
    if (cv == std::string::npos) return "";
    size_t p = s.find("(b", cv);
    if (p == std::string::npos) return "";
    size_t e = s.find(' ', p);
    if (e == std::string::npos) return "";
    return s.substr(p + 1, e - p - 1);
}

namespace {

std::vector<std::string> glob_one(const char* pattern) {
    std::vector<std::string> out;
    glob_t g{};
    if (glob(pattern, 0, nullptr, &g) == 0)
        for (size_t i = 0; i < g.gl_pathc; ++i) out.push_back(g.gl_pathv[i]);
    globfree(&g);
    return out;
}

}  // namespace

NeutronAlign neutron_alignment() {
    NeutronAlign a;
    for (const char* p : {"/lib/firmware/NeutronFirmware.elf",
                          "/usr/lib/firmware/NeutronFirmware.elf"}) {
        a.firmware = neutron_build_stamp(p);
        if (!a.firmware.empty()) break;
    }
    // Search the usual rootfs spot first, then mounted partitions (a multi-image
    // board can carry a stale converter on a second partition — its stamp is what
    // tells us whether it actually matches the live firmware).
    for (const char* pat : {"/usr/lib/libNeutronConverter.so",
                            "/run/media/*/usr/lib/libNeutronConverter.so",
                            "/opt/*/libNeutronConverter.so"}) {
        for (const auto& path : glob_one(pat)) {
            std::string st = neutron_build_stamp(path);
            if (!st.empty()) {
                a.converter = st;
                a.converter_lib = path;
                break;
            }
        }
        if (!a.converter.empty()) break;
    }
    a.matched = !a.firmware.empty() && !a.converter.empty() && a.firmware == a.converter;
    return a;
}

bool tflite_is_converted(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string s;
    char buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        s.append(buf, n);
        if (s.size() > (64u << 20)) break;
    }
    std::fclose(f);
    // Neutron-converted models embed these custom-op names; a plain TFLite has none.
    return s.find("NeutronGraph") != std::string::npos ||
           s.find("NeutronMicrocode") != std::string::npos;
}

NeutronModelInfo neutron_model_info(const std::string& path) {
    NeutronModelInfo info;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return info;
    std::string s;
    char buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        s.append(buf, n);
        if (s.size() > (64u << 20)) break;
    }
    std::fclose(f);
    // The converter writes plain strings like "Version: 2.2.3+0X..." and
    // "Target: imx95". Pull the token after each key, stopping at the first char
    // that isn't part of a version/target (so the "+<hash>" and trailing binary
    // are dropped).
    auto extract = [&s](const char* key) -> std::string {
        size_t p = s.find(key);
        if (p == std::string::npos) return "";
        p += std::strlen(key);
        size_t e = p;
        auto ok = [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
        };
        while (e < s.size() && ok(s[e])) ++e;
        return s.substr(p, e - p);
    };
    info.version = extract("Version: ");
    info.target = extract("Target: ");
    return info;
}

bool neutron_convert_file(const std::string& lib, const std::string& in,
                          const std::string& out, const std::string& target,
                          std::string& err) {
    using ConvertFn = std::vector<unsigned char> (*)(const std::vector<unsigned char>&,
                                                     const std::string&);
    // mangled: converter::convertModel(vector<unsigned char> const&, string const&)
    static const char* kSym =
        "_ZN9converter12convertModelERKSt6vectorIhSaIhEERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE";

    void* h = dlopen(lib.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        const char* de = dlerror();
        err = std::string("dlopen converter (") + lib + "): " + (de ? de : "unknown error");
        return false;
    }
    auto convert = reinterpret_cast<ConvertFn>(dlsym(h, kSym));
    if (!convert) {
        err = "convertModel symbol not found in " + lib;
        return false;
    }

    FILE* f = std::fopen(in.c_str(), "rb");
    if (!f) { err = "cannot open input: " + in; return false; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(f); err = "empty/unreadable input: " + in; return false; }
    std::vector<unsigned char> data(static_cast<size_t>(sz));
    size_t got = std::fread(data.data(), 1, data.size(), f);
    std::fclose(f);
    if (got != data.size()) { err = "read error: " + in; return false; }

    std::vector<unsigned char> res;
    try {
        res = convert(data, target);  // the blocking graph->microcode compile
    } catch (const std::exception& e) {
        err = std::string("converter threw: ") + e.what();
        return false;
    } catch (...) {
        err = "converter aborted (bad target name?)";
        return false;
    }
    if (res.empty()) { err = "converter returned no data"; return false; }

    FILE* o = std::fopen(out.c_str(), "wb");
    if (!o) { err = "cannot write output: " + out; return false; }
    size_t wrote = std::fwrite(res.data(), 1, res.size(), o);
    std::fclose(o);
    if (wrote != res.size()) { err = "short write to " + out; return false; }
    return true;
}

} // namespace imx95
