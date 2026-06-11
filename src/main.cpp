// SPDX-License-Identifier: BSD-3-Clause
//
// imx95-media-test: an interactive CLI harness that runs heavy, tunable
// GPU/VPU (later NPU) workloads on the NXP i.MX95 to measure how one hardware
// block's load affects another — chiefly via global DDR memory bandwidth.

#include "menu.hpp"

int main() {
    imx95::run_app();
    return 0;
}
