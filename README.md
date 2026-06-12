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

## Status — what works today

GPU, VPU, and the DDR monitor are **running on real i.MX95 silicon** (an i.MX95
EVK). Each subsystem's backend is selected independently at build time, and a
full **mock** build runs the entire UI on any Linux host.

| Subsystem | mock | real backend | on hardware |
|-----------|:----:|------|------|
| **GPU** | ✅ | `gles` — EGL + GLES2, headless via GBM (host Mesa **and** i.MX95 Mali) | ✅ working |
| **VPU** | ✅ | `v4l2` — V4L2 mem2mem decode + encode; H.264 on the i.MX95 Wave VPU | ✅ working |
| **DDR** | ✅ | `pmu` — i.MX9 DDR perf counters via `perf_event_open` | ✅ working |
| **NPU** | ✅ | `bench` — eIQ Neutron via `benchmark_model` + delegate | ⚠️ **see below** |

> ### ⚠️ NPU inference does not run end-to-end yet
>
> The Neutron NPU is **fully wired into the harness** and the delegate **offloads
> the model on hardware** (`1 node delegated`) — but actually *executing* it
> currently **crashes inside NXP's Neutron driver** (`libNeutronDriver.so`,
> segfault at model-prepare, `privateNeutronModelPrepareLegacy`).
>
> **Root cause:** the neutron-converter's microcode format must match the board's
> BSP firmware/driver *exactly*. No public converter version we tried (eIQ SDK
> `25-03` … `26-03`) matches this early **walnascar** image (firmware stamped
> Feb 2025), and the driver embeds no version to pin the right one. We ruled out
> zero-copy (`NEUTRON_ENABLE_ZERO_COPY=0`, still crashes) and silicon revision
> (rev 2.0, aligned).
>
> **This is an NXP BSP/converter alignment issue, not a harness bug.** The NPU
> block lights up automatically via `IMX95_NPU_MODEL=<working .tflite>` the moment
> a board-matched model executes — **zero code changes needed.** The likely fixes
> are a converter bundled with this exact BSP (ideally one that runs *on the
> board*), or a newer aligned BSP image. See [`docs/BOARD.md`](docs/BOARD.md).

Also on the roadmap: an optional on-screen GPU window (currently headless).

## Features

- **Four workload blocks** — GPU (Mali/GLES), VPU (Wave decode + encode), NPU
  (Neutron), and a global DDR-bandwidth monitor.
- **Run alone or in parallel**, in **continuous / once / fixed-count / detached**
  modes.
- **Live dashboard** (per-block fps + rolling DDR), graceful `Ctrl-C`/space-menu
  stop, and a per-run **report**.
- **Detached background runs** logged to a file and **managed in-app** (list/stop,
  no shell needed).
- **Built-in help** (`h`, paginated) and **self-diagnostics** (`c) Check system`).
- **Single self-contained ~24 MB binary** — `dlopen`s the platform GPU/codec libs,
  talks V4L2 directly, embeds its own test video; **cross-compiles with no BSP
  sysroot**.
- **Full mock build** runs the whole UI on any Linux host / `qemu-imx95`.

## Quick start — try the UI in 30 seconds (no i.MX95 needed)

The all-mock build runs the complete interface anywhere. It simulates the
workloads so you can learn the menus, run loop, and reporting before touching
hardware.

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

### Know your board: `c) Check system`

Not sure what your target supports? Press **`c`** — it probes each block and tells
you what's runnable here (and *why* something isn't) before you run anything:

![check system](docs/images/check.png)

### Built-in help: `h`

A paginated quick-help screen that pauses between pages rather than scrolling off:

![built-in help](docs/images/help.png)

### Detached runs

Run the harness in the background while the terminal stays free — start one with
**Run → 4**, then list and stop them from **main menu → Detached runs** (by
number, or `a` for all; no shell needed):

![managing detached runs](docs/images/detached.png)

On real hardware the experience is identical — you just build with the real
backends (see **Running on an i.MX95** below).

## What it exercises

| Block | Workload | Levels |
|-------|----------|--------|
| **GPU** (Arm Mali-G310) | Procedural EGL/GLES scene, complexity scaled by resolution × geometry × lights × shader cost | `low` / `mid` / `max` |
| **VPU** (Wave codec, V4L2 mem2mem) | Decode and/or encode | `720p` / `1080p` / `4k`, decode and encode independently |
| **NPU** (eIQ Neutron) | Looped quantized-TFLite inference via the Neutron delegate | on / off (⚠️ pending BSP-matched model, see above) |

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
- **DDR is not the bottleneck** at these loads (~5–8 GB/s under a full mix, well
  under the LPDDR ceiling) — the VPU engine is the limiter, not memory.

That cross-block insight — invisible from specs — is exactly what the harness is
for.

## Running on an i.MX95

The deploy artifact is a single binary — see **[`docs/BOARD.md`](docs/BOARD.md)**
for the full build + run + bring-up guide. The short version cross-compiles a
ready-to-upload binary with a generic aarch64 toolchain (no BSP sysroot — the GLES
backend `dlopen`s the Mali libs at runtime):

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
`IMX95_GPU_{W,H,SUBDIV,INSTANCES,LIGHTS,EXTRA,PASSES}`, `IMX95_DDR_BEAT_BYTES`,
`IMX95_DRM_DEVICE`, `IMX95_NPU_MODEL`. Details in `docs/BOARD.md`.

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
