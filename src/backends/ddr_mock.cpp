// SPDX-License-Identifier: BSD-3-Clause
// Mock DDR monitor: reports the estimated-traffic bus rather than real PMU
// counters, so bandwidth tracks the active (mock or real) workloads.

#include "backend.hpp"
#include "backends/traffic_estimate.hpp"

namespace imx95 {

namespace {

class MockDdr : public DdrMonitor {
public:
    bool init(std::string&) override { return true; }
    DdrSample sample() override { return traffic_estimate_read(); }
    void shutdown() override {}
};

} // namespace

std::unique_ptr<DdrMonitor> make_ddr_monitor() {
    return std::make_unique<MockDdr>();
}

const char* ddr_backend_name() { return "mock"; }

} // namespace imx95
