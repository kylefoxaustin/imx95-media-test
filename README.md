# imx95-media-test

An interactive, single-binary command-line harness for stressing the **NXP
i.MX95** media/compute blocks — **GPU, VPU, NPU** — and measuring how loading one
block affects another.

The goal is not to benchmark a block in isolation. It answers the question an SoC
integrator actually asks:

> *If I max out the GPU and run a 4K VPU decode, what does that do to my other
> blocks, and to total DDR bandwidth?*

You pick some heavy workloads, run them together (or alone), and get per-workload
throughput plus the **global DDR memory-traffic** numbers that expose cross-block
interference.

![menu](docs/images/menu.png)

During a run, a live dashboard shows each workload's fps and the rolling DDR
bandwidth; stop any time for a final report:

![live dashboard](docs/images/dashboard.png)

## Status — all four blocks run on real i.MX95 silicon

GPU, VPU, NPU, and the DDR monitor are **all validated on real i.MX95 hardware**
(an i.MX95 EVK, `lf-6.12.49_2.2.0` BSP). Each subsystem's backend is selected
independently at build time, and a full **mock** build runs the entire UI on any
Linux host.

| Subsystem | mock | real backend | on hardware |
|-----------|:----:|------|------|
| **GPU** | ✅ | `gles` — EGL + GLES2, headless via GBM (host Mesa **and** i.MX95 Mali) | ✅ working |
| **VPU** | ✅ | `v4l2` — V4L2 mem2mem decode + encode; H.264 on the i.MX95 Wave VPU | ✅ working |
| **NPU** | ✅ | `bench` — eIQ Neutron via `benchmark_model` + delegate | ✅ working&nbsp;\* |
| **DDR** | ✅ | `pmu` — i.MX9 DDR perf counters via `perf_event_open` | ✅ working |

> **\* NPU success is BSP-dependent.** The model must be neutron-converted with the
> converter version that **matches your board's firmware**, or the driver rejects
> it. This is the one real gotcha on this SoC — if your NPU row comes up red, that's
> almost always why. The full recipe (on-target *or* host conversion) is in
> **[Running on an i.MX95 → NPU model](#npu-model-match-the-neutron-converter-to-your-bsp)**.

## Quick start — try the UI in 30 seconds (no i.MX95 needed)

The all-mock build runs the complete interface anywhere. It simulates the
workloads so you can learn the menus, run loop, and reporting before touching
hardware.

**Prerequisites:** CMake ≥ 3.16 and a C++17 compiler (`g++` or `clang`).

```sh
git clone https://github.com/kylefoxaustin/imx95-media-test
cd imx95-media-test
cmake -S . -B build
cmake --build build -j
./build/imx95-test
```

Then drive the menu (it mirrors the screenshots above):

1. `1` → **Configure workloads**.
2. `1` → GPU → `3` (mid) → `b`. Then `2` → VPU decode → `3` (1080p) → `b`. Then
   `b` back to the main menu. Selections apply instantly and are always shown;
   `b` goes back everywhere — no dead ends.
3. `2` → **Run** → `1` continuous, `2` once, `3` a fixed loop count, or `4`
   **detached → log file** (runs in the background so the terminal stays free).
4. Watch the live dashboard. Press **space** for a menu (resume / stop & report /
   quit), or **Ctrl-C** to stop gracefully. Either way you get a per-workload +
   DDR **run report**.

## Features

- **Four workload blocks** — GPU (Mali/GLES), VPU (Wave decode + encode), NPU
  (Neutron), and a global DDR-bandwidth monitor.
- **Run alone or in parallel**, in **continuous / once / fixed-count / detached**
  modes.
- **Live dashboard** (per-block fps + rolling DDR), graceful `Ctrl-C`/space-menu
  stop, and a per-run **report**.
- **Detached background runs** logged to a file and **managed in-app** (list/stop,
  no shell needed).
- **On-target NPU model prep** (`n) Prepare NPU model`) — converts a quantized
  `.tflite` on the board itself, **gated on a converter↔firmware build-stamp
  match** so it can't produce microcode the driver rejects.
- **Built-in help** (`h`, paginated) and **self-diagnostics** (`c) Check system`).
- **Single self-contained ~24 MB binary** — `dlopen`s the platform GPU/codec libs,
  talks V4L2 directly, embeds its own test video; **cross-compiles with no BSP
  sysroot**.
- **Full mock build** runs the whole UI on any Linux host / `qemu-imx95`.

### Know your board: `c) Check system`

Not sure what your target supports? Press **`c`** — it probes each block and tells
you what's runnable here (and *why* something isn't) before you run anything:

![check system](docs/images/check.png)

### Built-in help: `h`

A paginated quick-help screen that pauses between pages rather than scrolling off:

![built-in help](docs/images/help.png)

### Prepare an NPU model on the board: `n`

The NPU needs a `.tflite` neutron-converted for *this* board's BSP. If the board
ships its own converter, **`n) Prepare NPU model`** does it locally — and guards
you with a build-stamp check (the converter-vs-BSP story is in
[Running on an i.MX95 → NPU model](#npu-model-match-the-neutron-converter-to-your-bsp)).
The build stamps shown below are illustrative:

```text
> n
Prepare NPU model — convert a quantized .tflite for THIS board
  firmware build .... b425_2025.10.xx
  on-board converter  b425_2025.10.xx   [/usr/lib/libNeutronConverter.so]

  converter MATCHES firmware -> safe to convert on this board.

Input .tflite to convert (b = back) > mobilenet_v1_quant.tflite
Will write: mobilenet_v1_quant.neutron-b425_2025.10.xx.tflite   (target 'imx95')
Convert on this board now? [y/N] > y
converting......... 
Converted in 8.7 s -> mobilenet_v1_quant.neutron-b425_2025.10.xx.tflite
Use this model for NPU runs this session? [Y/n] > y
```

If the on-board converter's stamp **doesn't** match the firmware (or there's no
converter at all), `n` **refuses and points you to host conversion** instead of
producing microcode the driver would reject — the version mismatch is the whole
reason a converted model can segfault at model-prepare. `c) Check system` reports
the same alignment up front. The converted file is cached (named with the
firmware stamp), so a re-run reuses it instead of recompiling.

### Detached runs — launch in the background, monitor from the menu

Long soaks shouldn't tie up your terminal. A **detached** run forks into the
background, logs to a file, and hands the menu straight back — so you can launch
more runs (each contends on the real hardware) or just walk away.

**Start one:**

1. Configure your workloads — e.g. `1` GPU → `4` max → `b`, then `2` VPU decode →
   `4` 4k → `b`, then `b`.
2. **`2` Run → `4`** (Detached → log file).
3. Pick a mode: `1` continuous, `2` once, or `3` a fixed count.
4. Press **Enter** to accept the default log name (`imx95-run-<timestamp>.log`),
   or type your own.

It prints the PID + log path and drops you back at the menu:

```text
Detached run started — PID 1353, logging to: run.log
  continuous; stop it from the main menu (Detached runs) or:  kill 1353
```

**Watch it** from a shell: `tail -f run.log` — it writes a one-line snapshot every
~2 s and a full report when it stops:

```text
[t=00:06] | GPU max 8.0fps 49f | DEC 4k 99.5fps 612f | DDR 6.84 GB/s
```

**Monitor / stop from the menu** — main menu → **`3` Detached runs** lists every
run you launched this session (PID, alive/exited, config, log path). Enter its
number to stop one, or `a` to stop all — no shell, no remembering PIDs. Stopping a
run makes it write its final report to its log and exit:

![managing detached runs](docs/images/detached.png)

On real hardware the experience is identical — you just build with the real
backends (see **[Running on an i.MX95](#running-on-an-imx95)** below).

## What it exercises

| Block | Workload | Levels |
|-------|----------|--------|
| **GPU** (Arm Mali-G310) | Procedural EGL/GLES scene, complexity scaled by resolution × geometry × lights × shader cost | `low` / `mid` / `max` |
| **VPU** (Wave codec, V4L2 mem2mem) | Decode and/or encode | `720p` / `1080p` / `4k`, decode and encode independently |
| **NPU** (eIQ Neutron) | Looped quantized-TFLite inference via the Neutron delegate | on / off (needs a BSP-matched model — convert on-target via `n` or on a host, see below) |

Decode and encode are independent (run both at once), but each is
single-resolution. The GPU level is a single choice.

## What the workloads look like

The GPU workload renders a procedural lit-sphere scene whose cost scales across
the levels — resolution, geometry, light count, and per-pixel shader work all
increase (defaults tuned for the Mali-G310, overridable with `IMX95_GPU_*`):

![GPU scene at low / mid / max](docs/images/gpu_scene.png)

VPU **decode** runs real **Big Buck Bunny** content (baked into the binary at
720p/1080p/4K) so its frame rate is representative — synthetic frames compress to
almost nothing and decode unrealistically fast:

![VPU decode input — Big Buck Bunny](docs/images/vpu_decode.png)

(VPU **encode** is fed procedurally generated raw frames; for an encode
throughput test the pixel content is irrelevant, only that frames change. GPU
frames captured headless with `IMX95_GPU_DUMP=<path.ppm>`.)

## What it found on real silicon

Running the blocks together on an i.MX95 EVK surfaced behavior you won't get from
a datasheet:

- **The GPU is isolated.** GPU `mid` holds ~66–70 fps whether alone or under full
  VPU load — Mali is a separate engine and is compute-bound (~1–1.5 GB/s DDR).
- **The Wave VPU time-shares one engine between encode and decode.** Run a decode
  and an encode together and they converge to *identical* fps (e.g. 1080p
  dec+enc → ~157/157 fps), with decode dropping ~25–50%. That's a single
  time-sliced codec engine, not independent enc/dec hardware.
- **The Neutron NPU is largely isolated too.** MobileNet inference holds ~554
  inf/s under a full GPU `mid` + 1080p-decode load vs ~590 standalone (~6%, much
  of that the harness re-launching the runner per batch) — like the GPU, a
  separate engine that neither steals nor cedes much.
- **DDR is not the bottleneck** at these loads (~5–11 GB/s under a full mix, well
  under the LPDDR ceiling) — the VPU engine is the limiter, not memory.

**Capstone — all four blocks maxed at once, on the hardest board.** On an
early-walnascar `b307_2025.02.05` EVK (the one board *no* public converter quarter
could run), a model converted via the [eIQ AI Hub](https://eiq.nxp.com/ai-hub)
(neutron-converter **2.2.3**) drove the whole SoC flat-out — GPU `max`, **4K
decode + 4K encode**, and NPU, simultaneously:

| Block | Maxed together | Alone | Read |
|---|---|---|---|
| GPU `max` | 7.8 fps | ~8 fps | isolated, compute-bound |
| DEC 4k | **43.6 fps** | ~100 fps | ↓56% — shares the Wave engine |
| ENC 4k | **43.6 fps** | ~61 fps | ↓28% — *converges* with decode |
| NPU | **341.5 fps** | ~353 fps | ↓3% — own engine, barely flinches |
| DDR | **13.6 GB/s** (1.05 TB / 78 s) | — | 4K codec is the memory driver |

The 4K decode and encode collapsing to the *exact same* 43.6 fps is the single
time-sliced Wave engine, proven the hard way; the NPU holding station is Neutron's
isolation. That cross-block insight — invisible from specs — is exactly what the
harness is for.

## Running on an i.MX95

The deploy artifact is a single binary — see **[`docs/BOARD.md`](docs/BOARD.md)**
for the full build + run + bring-up guide. The short version cross-compiles a
ready-to-upload binary with a generic aarch64 toolchain (no BSP sysroot — the GLES
backend `dlopen`s the Mali libs at runtime):

**Prerequisites:** an aarch64 cross toolchain (e.g. `sudo apt install
g++-aarch64-linux-gnu`). `scripts/fetch-assets.sh` additionally needs `curl` and
`ffmpeg` to fetch + transcode the test clips.

```sh
scripts/fetch-assets.sh        # fetch + transcode the Big Buck Bunny clips (once)
cmake -S . -B build-aarch64 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
      -DIMX95_GPU=gles -DIMX95_VPU=v4l2 -DIMX95_NPU=bench -DIMX95_DDR=pmu
cmake --build build-aarch64 -j
aarch64-linux-gnu-strip build-aarch64/imx95-test
# upload build-aarch64/imx95-test to the board and: sudo ./imx95-test
```

Or grab the prebuilt binary from the [latest release](https://github.com/kylefoxaustin/imx95-media-test/releases).

Run as **root** (the DDR PMU and codec/GPU nodes need it). On a new board, press
**`c`** first to confirm what's runnable. Handy overrides:
`IMX95_VPU_CODEC=h264|hevc`, `IMX95_VPU_STREAM=<file.h264>`,
`IMX95_GPU_{W,H,SUBDIV,INSTANCES,LIGHTS,EXTRA,PASSES}`, `IMX95_GPU_DUMP=<path.ppm>`,
`IMX95_DDR_BEAT_BYTES`, `IMX95_DRM_DEVICE`, `IMX95_NPU_MODEL`. Details in
`docs/BOARD.md`.

### NPU model: match the neutron-converter to your BSP

The Neutron NPU runs a quantized TFLite model **neutron-converted with the
converter version that matches your board's BSP**. A *mismatched* converter
makes the driver segfault at model-prepare even though the delegate partitions
the graph — so the version pairing is what matters. **Same build stamp on the
converter and the running firmware ⇒ it works**, wherever the converter runs.

**Three ways to get a matching model:**

1. **On the board — menu `n) Prepare NPU model`** (easiest *if* the board ships
   a converter). The harness reads the firmware and on-board converter build
   stamps; if they match it converts your `.tflite` right there (one keystroke),
   caches it, and offers to use it. **If they don't match it refuses** and
   points you to another route — so it never converts straight into the segfault.
2. **On an x86 host**, with the eIQ converter SDK quarter that matches your BSP.
   Find the pairing from NXP's open-source [`nxp-imx/neutron`](https://github.com/nxp-imx/neutron)
   repo: the branch named like your BSP (`lf-<kver>_<rel>`) carries the matching
   firmware + driver, and the converter quarter maps to that release's date. On
   the EVK here, `lf-6.12.49_2.2.0` (Q4 2025) → eIQ **SDK 25-12**.
3. **Online — [eIQ AI Hub](https://eiq.nxp.com/ai-hub) (cloud, NXP sign-in).** The
   AI Toolkit's *Optimize & convert* runs the Neutron conversion in the browser:
   choose **Target `imx95`** and the **Flavor (version) that matches your board's
   BSP**, then download the converted `.tflite`. Inputs: TFLite / ONNX / PyTorch.

Whichever route, the **flavor/version must match your board's firmware build** —
deploy, then confirm with `c) Check system`. A MobileNet converted for this BSP
runs at **~1.7 ms/inf (≈32× over CPU)**.

**Deploying a host-converted model — no env var, no menu step needed.** Upload
the converted `.tflite` into the **same directory as the binary** on the target
and just run it; the harness auto-detects it:

- It scans the binary's folder (and the working directory) and picks the one
  `.tflite` that is **actually neutron-converted** — detected by *content*, so
  **the filename doesn't matter** (rename it however you like).
- Plain / un-converted `.tflite` files in the same folder are **ignored**.
- If **two or more** converted models are present it won't guess — it asks you
  to pin one with `IMX95_NPU_MODEL=<path>`.
- Setting `IMX95_NPU_MODEL=<path>` always wins if you prefer to be explicit.

`c) Check system` shows which model it resolved and whether it actually ran.
Full recipe + conversion snippet in [`docs/BOARD.md`](docs/BOARD.md).

## Build options

Backends are chosen per subsystem (`mock` default):

```sh
cmake -S . -B build -DIMX95_GPU=gles -DIMX95_VPU=v4l2 -DIMX95_NPU=bench -DIMX95_DDR=pmu
```

- **`-DIMX95_GPU=gles`** — headless EGL + GLES2 (Mesa on a host, Mali on target).
- **`-DIMX95_VPU=v4l2`** — V4L2 stateful mem2mem codec; bakes in the BBB clips
  when `assets/clips/*.h264` exist (run `scripts/fetch-assets.sh`).
- **`-DIMX95_NPU=bench`** — drives the platform's `benchmark_model` + Neutron
  delegate over `IMX95_NPU_MODEL` (see the NPU note above).
- **`-DIMX95_DDR=pmu`** — i.MX9 DDR PMU via `perf_event_open`.

### Exercise the V4L2 codec path on a host (no VPU)

The kernel's virtual stateful codec `vicodec` speaks the same uAPI as the Wave
VPU, so the decode/encode plumbing can be validated on a dev box:

```sh
sudo modprobe vicodec                        # creates /dev/videoN codec nodes
cmake -S . -B build-vpu -DIMX95_VPU=v4l2 && cmake --build build-vpu -j
IMX95_VPU_CODEC=fwht ./build-vpu/imx95-test  # vicodec uses the FWHT codec
```

### Testing the CLI

`tests/menu_walkthrough.sh` builds two **ASan+UBSan** mock/bench binaries and
drives the program through essentially every menu option — configure (all GPU
levels + VPU resolutions + NPU toggle + clear), all run modes (once / fixed /
continuous-via-SIGINT), detached launch/stop, load/save, help, check-system, the
`n` model-prep paths, and NPU model resolution (auto-detect / ambiguous / none /
explicit) — asserting no crash, abort, or sanitizer trip. Run it from the repo
root; it exits non-zero on any failure.

### DDR bandwidth notes (`-DIMX95_DDR=pmu`)

Reads `fsl_imx9_ddr_perf` (sysfs `imx9_ddr0`), summing the read/write **beat**
counters (`eddrtq_pm_rd_beat_filt*` / `..._wr_beat_filt` on i.MX95). Needs root
(or `perf_event_paranoid <= 0`); falls back to an estimate when absent. Bytes =
beats × `IMX95_DDR_BEAT_BYTES` (default **32** — confirm against the RM).

## Design at a glance

- **Single self-contained binary.** It talks to the VPU directly via V4L2
  `mem2mem` ioctls (no GStreamer) and `dlopen`s the platform EGL/GLES libraries
  that ship on any i.MX95 BSP image — so it links no GPU libraries and needs no
  extra packages on the target.
- **Per-subsystem backend abstraction.** Every workload implements a small
  `Workload` interface; the DDR monitor implements `DdrMonitor`. Exactly one
  `mock` or real implementation is compiled per subsystem
  (`-DIMX95_GPU/VPU/NPU/DDR`), so real backends land incrementally and the CLI is
  developed/CI-tested without silicon.
- **DDR memory traffic is the headline metric.** Read from the i.MX9 DDR PMU
  (`perf_event_open`) on hardware. It is global to the SoC, which is exactly why
  it reveals one block stealing bandwidth from another.

See [`docs/DESIGN.md`](docs/DESIGN.md) for the architecture, module map, and
roadmap.

## Media assets

VPU decode uses **Big Buck Bunny** (© Blender Foundation, CC-BY 3.0).
`scripts/fetch-assets.sh` downloads the source and transcodes three
native-resolution H.264 Annex-B clips into `assets/clips/` (gitignored); the build
bakes them into the binary, so there is still only one file to deploy.

## Maintainer

Created and maintained by **Kyle Fox** ([@kylefoxaustin](https://github.com/kylefoxaustin)).

## License

BSD-3-Clause, © 2026 Kyle Fox. See [`LICENSE`](LICENSE). Bundled Big Buck Bunny
frames are CC-BY 3.0, © Blender Foundation.
