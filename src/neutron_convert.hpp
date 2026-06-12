// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <string>

// On-target Neutron model conversion + eIQ alignment checks, shared by the menu
// ("n) Prepare NPU model"), the NPU backend's check, and the standalone
// tools/neutron-convert-on-target.cpp. Converting on the board with its OWN
// converter guarantees the microcode matches the driver/firmware — but ONLY if
// the converter present actually belongs to the running firmware's eIQ build, so
// every path here is gated on a build-stamp match (see NeutronAlign::matched).

namespace imx95 {

// eIQ build stamp embedded in a Neutron binary (firmware/converter), parsed from
// its "clang version ... (bNNN_YYYY.MM.DD ...)" string, e.g. "b307_2025.02.05".
// Returns "" if the file is missing or carries no stamp.
std::string neutron_build_stamp(const std::string& path);

// Snapshot of this board's converter <-> firmware alignment. A converted model
// only executes if the converter that produced it matches the running firmware;
// `matched` is the green light for on-target conversion.
struct NeutronAlign {
    std::string firmware;       // running firmware build stamp ("" if not found)
    std::string converter;      // on-board converter build stamp ("" if none)
    std::string converter_lib;  // path to libNeutronConverter.so ("" if none)
    bool matched = false;       // firmware & converter both present and equal
};
NeutronAlign neutron_alignment();

// True if `path` is a .tflite that already carries Neutron custom ops (i.e. it
// has been neutron-converted). A plain quantized model returns false.
bool tflite_is_converted(const std::string& path);

// Converter metadata a neutron-converted model embeds in its flatbuffer
// ("Version: 2.2.3+...", "Target: imx95"). Empty fields if absent/plain.
struct NeutronModelInfo {
    std::string version;  // converter semver, e.g. "2.2.3" (hash suffix stripped)
    std::string target;   // e.g. "imx95"
};
NeutronModelInfo neutron_model_info(const std::string& path);

// Convert `in` -> `out` for `target` (e.g. "imx95") using the board's converter
// at `lib` (dlopen'd). Blocking: the caller shows liveness around it. Returns
// false + err on failure. NOT gated here — the caller MUST confirm
// NeutronAlign::matched first, or the produced microcode can crash the driver.
bool neutron_convert_file(const std::string& lib, const std::string& in,
                          const std::string& out, const std::string& target,
                          std::string& err);

} // namespace imx95
