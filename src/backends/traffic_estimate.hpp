// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <cstdint>

#include "stats.hpp"

namespace imx95 {

// Process-global "estimated DDR traffic" bus. Workloads (mock or real) that
// cannot measure their true memory traffic add an estimate here; the *mock*
// DDR monitor reports the running total, so its bandwidth number tracks
// whatever is actually running. The real DDR-PMU monitor reads hardware
// counters instead and ignores this bus entirely.
void      traffic_estimate_add(uint64_t read_bytes, uint64_t write_bytes);
DdrSample traffic_estimate_read();

} // namespace imx95
