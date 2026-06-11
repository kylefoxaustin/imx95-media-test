// SPDX-License-Identifier: BSD-3-Clause
#include "menu.hpp"

#include <cstdio>
#include <ctime>
#include <iostream>
#include <string>

#include "backend.hpp"
#include "config.hpp"
#include "runner.hpp"

#ifndef IMX95_VERSION
#define IMX95_VERSION "dev"
#endif

namespace imx95 {

namespace {

const char* kConfigPath = "imx95-test.cfg";

std::string read_line(const char* prompt) {
    std::fputs(prompt, stdout);
    std::fflush(stdout);
    std::string s;
    if (!std::getline(std::cin, s)) {
        // EOF (e.g. piped/closed stdin): behave like quit.
        return "q";
    }
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

void rule() { std::puts("--------------------------------------------------"); }

// Radio picker for the GPU level. Selection applies immediately and stays
// visible; 'b' returns. No submit/clear ceremony.
void pick_gpu(Config& cfg) {
    for (;;) {
        rule();
        std::printf("GPU workload   selected: %s        (b = back & save)\n", to_string(cfg.gpu));
        std::puts("  1) off");
        std::puts("  2) low   (light load)");
        std::puts("  3) mid   (moderate load)");
        std::puts("  4) max   (heavy - pegs the GPU; tune with IMX95_GPU_* env vars)");
        std::string c = read_line("> ");
        if (c == "1") cfg.gpu = GpuLevel::Off;
        else if (c == "2") cfg.gpu = GpuLevel::Low;
        else if (c == "3") cfg.gpu = GpuLevel::Mid;
        else if (c == "4") cfg.gpu = GpuLevel::Max;
        else if (c == "b" || c == "q") return;
    }
}

// Radio picker for a single VPU resolution (decode or encode). Picking a new
// resolution replaces the prior one, so "4k AND 1080p decode" is impossible.
void pick_res(VideoRes& slot, const char* title) {
    for (;;) {
        rule();
        std::printf("%s   selected: %s        (b = back & save)\n", title, to_string(slot));
        std::puts("  1) off");
        std::puts("  2) 720p");
        std::puts("  3) 1080p");
        std::puts("  4) 4k");
        std::string c = read_line("> ");
        if (c == "1") slot = VideoRes::Off;
        else if (c == "2") slot = VideoRes::R720p;
        else if (c == "3") slot = VideoRes::R1080p;
        else if (c == "4") slot = VideoRes::R4k;
        else if (c == "b" || c == "q") return;
    }
}

void configure_menu(Config& cfg) {
    for (;;) {
        rule();
        std::puts("Configure workloads              (b = back, keeps selections)");
        std::printf("  1) GPU ........... %s\n", to_string(cfg.gpu));
        std::printf("  2) VPU decode .... %s\n", to_string(cfg.dec));
        std::printf("  3) VPU encode .... %s\n", to_string(cfg.enc));
        std::puts("  c) Clear all");
        std::string c = read_line("> ");
        if (c == "1") pick_gpu(cfg);
        else if (c == "2") pick_res(cfg.dec, "VPU decode");
        else if (c == "3") pick_res(cfg.enc, "VPU encode");
        else if (c == "c") cfg.clear();
        else if (c == "b" || c == "q") return;
    }
}

void list_selected(const Config& cfg) {
    std::puts("Ready to run:");
    if (cfg.gpu != GpuLevel::Off) std::printf("  - GPU %s\n", to_string(cfg.gpu));
    if (cfg.dec != VideoRes::Off) std::printf("  - VPU decode %s\n", to_string(cfg.dec));
    if (cfg.enc != VideoRes::Off) std::printf("  - VPU encode %s\n", to_string(cfg.enc));
}

// Prompt for a loop count; returns the count, or -1 if the user backed out.
long ask_loop_count() {
    for (;;) {
        std::string s = read_line("How many loops? (number, b = back) > ");
        if (s == "b" || s == "q" || s.empty()) return -1;
        try {
            long n = std::stol(s);
            if (n > 0) return n;
        } catch (...) {
        }
        std::puts("Please enter a positive number, or b to go back.");
    }
}

std::string default_log_name() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    char buf[64];
    std::strftime(buf, sizeof(buf), "imx95-run-%Y%m%d-%H%M%S.log", &tmv);
    return buf;
}

// Configure a detached background run that logs to a file, then launch it.
void detached_run_menu(const Config& cfg) {
    uint64_t loops = 0;
    for (;;) {
        rule();
        std::puts("Detached run - runs in the background, logs to a file (terminal stays free)");
        std::puts("  1) Continuous   (stop later with: kill <pid>)");
        std::puts("  2) Once");
        std::puts("  3) Fixed count...");
        std::puts("  b) Back");
        std::string c = read_line("> ");
        if (c == "1") { loops = 0; break; }
        else if (c == "2") { loops = 1; break; }
        else if (c == "3") {
            long n = ask_loop_count();
            if (n < 0) continue;
            loops = static_cast<uint64_t>(n);
            break;
        } else if (c == "b" || c == "q") {
            return;
        }
    }
    std::string def = default_log_name();
    std::string lp = read_line(("Log file [" + def + "] > ").c_str());
    if (lp.empty()) lp = def;
    std::string err;
    if (!run_detached(cfg, loops, lp, err)) std::printf("Detached run failed: %s\n", err.c_str());
}

// Returns true if the user chose to quit the whole app.
bool ask_fixed_count_and_run(const Config& cfg) {
    for (;;) {
        std::string s = read_line("How many loops? (number, b = back) > ");
        if (s == "b" || s == "q" || s.empty()) return false;
        try {
            long n = std::stol(s);
            if (n > 0) return run_workloads(cfg, static_cast<uint64_t>(n)) == RunOutcome::QuitApp;
        } catch (...) {
        }
        std::puts("Please enter a positive number, or b to go back.");
    }
}

// Returns true if the user chose to quit the whole app.
bool run_menu(const Config& cfg) {
    if (!cfg.any()) {
        std::puts("\nNothing configured yet — choose 1) Configure workloads first.\n");
        return false;
    }
    for (;;) {
        rule();
        list_selected(cfg);
        std::puts("Run mode:                        (b = back)");
        std::puts("  1) Continuous (until Ctrl-C / space-menu quit)");
        std::puts("  2) Once (each workload one full pass)");
        std::puts("  3) Fixed count...");
        std::puts("  4) Detached -> log file (runs in background, terminal stays free)");
        std::string c = read_line("> ");
        RunOutcome out = RunOutcome::Completed;
        if (c == "1") out = run_workloads(cfg, 0);
        else if (c == "2") out = run_workloads(cfg, 1);
        else if (c == "3") { if (ask_fixed_count_and_run(cfg)) return true; else continue; }
        else if (c == "4") { detached_run_menu(cfg); return false; }
        else if (c == "b" || c == "q") return false;
        else continue;
        if (out == RunOutcome::QuitApp) return true;
        return false;  // back to top menu after a run
    }
}

void loadsave_menu(Config& cfg) {
    for (;;) {
        rule();
        std::printf("Load / Save config   (file: %s)        (b = back)\n", kConfigPath);
        std::puts("  1) Save current config");
        std::puts("  2) Load config");
        std::string c = read_line("> ");
        std::string err;
        if (c == "1") {
            if (cfg.save(kConfigPath, err)) std::printf("Saved to %s\n", kConfigPath);
            else std::printf("Save failed: %s\n", err.c_str());
        } else if (c == "2") {
            if (cfg.load(kConfigPath, err)) std::printf("Loaded: %s\n", cfg.summary().c_str());
            else std::printf("Load failed: %s\n", err.c_str());
        } else if (c == "b" || c == "q") {
            return;
        }
    }
}

} // namespace

void run_app() {
    install_signal_handlers();
    Config cfg;
    std::printf("\n== i.MX95 Media Test Framework v%s ==   (backends: gpu:%s vpu:%s ddr:%s)\n",
                IMX95_VERSION, gpu_backend_name(), vpu_backend_name(), ddr_backend_name());

    for (;;) {
        rule();
        std::printf("config:  %s\n", cfg.summary().c_str());
        std::puts("  1) Configure workloads");
        std::puts("  2) Run");
        std::puts("  3) Load / Save config");
        std::puts("  q) Quit");
        std::string c = read_line("> ");
        if (c == "1") configure_menu(cfg);
        else if (c == "2") { if (run_menu(cfg)) break; }
        else if (c == "3") loadsave_menu(cfg);
        else if (c == "q") break;
    }
    std::puts("\nBye.");
}

} // namespace imx95
