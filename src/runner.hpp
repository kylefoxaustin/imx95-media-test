// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <cstdint>
#include <string>

#include "config.hpp"

namespace imx95 {

enum class RunOutcome { Completed, Stopped, QuitApp };

// target_loops == 0 means continuous (run until Ctrl-C or the space-menu).
// Builds workloads from cfg, runs them in parallel, tears everything down
// cleanly, and prints the final per-workload + DDR report. Returns QuitApp if
// the user chose to quit the whole app.
//
// headless = false: interactive live dashboard (raw TTY, space-menu).
// headless = true:  no TTY — plain-text progress snapshots are logged
//   periodically and the run stops only on SIGINT/SIGTERM (or completion). Used
//   for detached/background runs whose output goes to a log file.
RunOutcome run_workloads(const Config& cfg, uint64_t target_loops, bool headless = false);

// Fork a detached background process (own session, stdio redirected to logpath)
// that runs the workloads headless. Returns immediately in the parent; prints
// the child PID. Stop a continuous detached run with `kill <pid>`.
bool run_detached(const Config& cfg, uint64_t target_loops, const std::string& logpath,
                  std::string& err);

// Installs SIGINT/SIGTERM handlers used for graceful shutdown. Call once.
void install_signal_handlers();

} // namespace imx95
