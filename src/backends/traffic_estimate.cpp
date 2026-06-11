// SPDX-License-Identifier: BSD-3-Clause
#include "traffic_estimate.hpp"

#include <atomic>

namespace imx95 {

namespace {
std::atomic<uint64_t> g_read{0};
std::atomic<uint64_t> g_write{0};
}

void traffic_estimate_add(uint64_t read_bytes, uint64_t write_bytes) {
    g_read.fetch_add(read_bytes, std::memory_order_relaxed);
    g_write.fetch_add(write_bytes, std::memory_order_relaxed);
}

DdrSample traffic_estimate_read() {
    return {g_read.load(std::memory_order_relaxed), g_write.load(std::memory_order_relaxed)};
}

} // namespace imx95
