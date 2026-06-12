// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <string>

namespace imx95 {

enum class GpuLevel { Off, Low, Mid, Max };
enum class VideoRes { Off, R720p, R1080p, R4k };

const char* to_string(GpuLevel);
const char* to_string(VideoRes);

// The set of workloads the user has configured to run.
//
// VPU decode and encode are independent (you can run both at once), but each
// is single-resolution: picking 4k decode replaces any prior decode choice.
struct Config {
    GpuLevel gpu = GpuLevel::Off;
    VideoRes dec = VideoRes::Off;
    VideoRes enc = VideoRes::Off;
    bool npu = false;  // loop NPU inference (eIQ Neutron)

    bool any() const {
        return gpu != GpuLevel::Off || dec != VideoRes::Off || enc != VideoRes::Off || npu;
    }
    void clear() { gpu = GpuLevel::Off; dec = VideoRes::Off; enc = VideoRes::Off; npu = false; }

    // One-line summary for headers, e.g. "[GPU mid]  [DEC 1080p]  [ENC 4k]".
    std::string summary() const;

    bool save(const std::string& path, std::string& err) const;
    bool load(const std::string& path, std::string& err);
};

} // namespace imx95
