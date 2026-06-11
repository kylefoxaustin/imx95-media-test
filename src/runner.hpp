// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <cstdint>

#include "config.hpp"

namespace imx95 {

enum class RunOutcome { Completed, Stopped, QuitApp };

// target_loops == 0 means continuous (run until Ctrl-C or the space-menu).
// Builds workloads from cfg, runs them in parallel, draws the live dashboard,
// tears everything down cleanly, and prints the final per-workload + DDR
// report. Returns QuitApp if the user chose to quit the whole app.
RunOutcome run_workloads(const Config& cfg, uint64_t target_loops);

// Installs SIGINT/SIGTERM handlers used for graceful shutdown. Call once.
void install_signal_handlers();

} // namespace imx95
