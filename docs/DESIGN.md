# Design

## Purpose

Give an engineer an easy way to apply a "sort of like a customer use case" mix
of heavy media/compute load on an i.MX95 and observe the effect on a *different*
hardware block. The primary observable is **DDR memory bandwidth**, because it
is the shared resource through which blocks interfere with one another.

## Module map

```
src/
  main.cpp          entry point
  menu.{hpp,cpp}    interactive menus (cooked tty): configure / run / load-save
  term.{hpp,cpp}    raw-mode tty + ANSI helpers for the live run dashboard
  config.{hpp,cpp}  Config (GPU level, decode res, encode res) + save/load
  stats.hpp         WorkStats (atomic live counters) + DdrSample
  backend.hpp       Workload / DdrMonitor interfaces + factory declarations
  runner.{hpp,cpp}  parallel run engine, live dashboard, signals, report
  backends_mock.cpp host/qemu backends (simulated)         [IMX95_TARGET=OFF]
  backends_real_*.cpp  real GLES / V4L2 / DDR-PMU backends  [IMX95_TARGET=ON]  (todo)
```

## Key interfaces

- **`Workload`** — one block's job. `step()` does one frame of work and bumps
  `stats().frames`; it must be short so stop requests stay responsive. The
  runner runs each workload on its own thread and counts loops
  (`frames_per_loop()`) to honour run-once / run-N.
- **`DdrMonitor`** — `sample()` returns cumulative read/write byte counters.
  The real impl wraps the i.MX9 DDR PMU via `perf_event_open`; the mock sums the
  traffic the active workloads declare.
- **Factories** (`make_gpu_workload`, `make_decode_workload`,
  `make_encode_workload`, `make_ddr_monitor`) are the seam between mock and
  real. Exactly one `backends_*.cpp` is linked per build.

## Run lifecycle

1. `run_workloads(cfg, target_loops)` builds the configured workloads + monitor.
2. `init()` each (acquire resources); on failure, tear down and bail.
3. Spawn one thread per workload; record `t0` and a baseline DDR sample.
4. Main thread enters raw mode and drives the dashboard at ~7 Hz: compute
   per-workload fps deltas and rolling DDR bandwidth, render, poll for `space`
   (pause menu) and check the interrupt flag.
5. Stop on: all workloads finished (finite run), `Ctrl-C`/`SIGTERM`, or the
   space-menu's *stop*/*quit*.
6. Signal stop, join threads, `shutdown()` each backend, print the report.

Graceful teardown is the single path out: the `RawMode` RAII object restores
the terminal on the way out even when a caught signal triggered the stop.

## Stats

- **Per workload:** frames, average fps, bytes of work product moved
  (throughput), and buffer footprint (allocated bytes).
- **DDR (global):** read / write / total bytes for the run, plus average and
  live GB/s. "Footprint (GB allocated)" and "traffic (GB/s, total GB moved)" are
  deliberately separate numbers and labelled as such.

## Roadmap

1. **(done)** Interactive CLI, parallel run engine, dashboard, graceful
   shutdown, stats, mock backends, host/qemu build.
2. **GPU real backend** — EGL (surfaceless/DRM) + GLES2/3 procedural scene with
   `low/mid/max` knobs (resolution, instance count, lights, shader length).
   Frame counter is exact; report GPU fps.
3. **VPU real backend** — V4L2 `mem2mem`: decode (feed Big Buck Bunny bitstream,
   dequeue frames, loop the clip) and encode (feed raw YUV, dequeue bitstream).
   Exact frame counts and bitrate.
4. **DDR real backend** — `perf_event_open` on the i.MX9 DDR PMU; convert
   read/write transaction counts × burst size to bytes.
5. **Camera as a "victim" workload** — reuse the i.MX95 ISI V4L2 capture path so
   you can watch capture fps degrade under GPU/VPU load (the camera-flow
   question stated in the brief).
6. **NPU (eIQ Neutron)** — a representative inference loop once 2–4 are solid.

## Hardware assumptions (verify against the BSP before writing real backends)

- GPU: Arm **Mali-G310**; EGL + OpenGL ES, surfaceless/DRM render (no
  compositor needed).
- VPU: **Wave** codec exposed as a V4L2 mem2mem device (`/dev/videoN`).
- DDR PMU: i.MX9 memory-controller perf PMU (e.g. `imx9_ddr*`) via
  `perf_event_open`.
- NPU: eIQ **Neutron**.
