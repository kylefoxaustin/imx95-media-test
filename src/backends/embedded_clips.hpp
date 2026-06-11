// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <cstddef>
#include <cstdint>

#include "config.hpp"

namespace imx95 {

// Big Buck Bunny H.264 Annex-B clips compiled into the binary (when built with
// assets/clips present). Returns a pointer to the clip for `res` and true, or
// false if no clip is embedded for it. (c) Blender Foundation, CC-BY 3.0.
bool embedded_clip(VideoRes res, const uint8_t*& data, size_t& size);

} // namespace imx95
