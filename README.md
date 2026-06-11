# imx95-media-test

An interactive, single-binary command-line harness for exercising **NXP i.MX95**
media/compute blocks — **GPU** and **VPU** today, **NPU** later — and measuring
how loading one block affects another.

The goal is not to benchmark a block in isolation. It is to answer questions an
SoC integrator actually asks:

> *If I max out the GPU and run a 4K VPU encode, what does that do to my camera
> pipeline, and to total DDR bandwidth?*

You pick some heavy workloads, run them together (or alone), and at the end you
get per-workload throughput plus the **global DDR memory-traffic** numbers that
expose cross-block interference.

> **Status:** the full interactive CLI, parallel run engine, live dashboard,
> graceful shutdown, and stats reporting work today. Each subsystem's backend is
> selected independently at build time:
>
> | Subsystem | `mock` | real |
> |-----------|:------:|------|
> | GPU | ✅ | ✅ `gles` — EGL + GLES2, builds & runs on host Mesa **and** i.MX95 Mali |
> | VPU | ✅ | ⏳ `v4l2` (next) |
> | DDR | ✅ | ⏳ `pmu` (i.MX9 DDR perf counters) |
>
> Mock backends run anywhere (host, CI, `qemu-imx95`, which has no GPU/VPU). The
> GLES backend is *real* and portable, so the GPU code path is validated on a
> dev host before it ever touches silicon.

## What it exercises

| Block | Workload | Levels |
|-------|----------|--------|
| **GPU** (Arm Mali-G310) | Procedural EGL/GLES scene, complexity scaled by resolution × geometry × lights × shader cost | `low` / `mid` / `max` |
| **VPU** (Wave codec, V4L2 mem2mem) | Decode and/or encode | `720p` / `1080p` / `4k`, decode and encode independently |
| **NPU** (eIQ Neutron) | TBD | — |

Decode and encode are independent (run both at once), but each is
single-resolution. The GPU level is a single choice.

## Design at a glance

- **Single self-contained binary.** It talks to the VPU directly via V4L2
  `mem2mem` ioctls (no GStreamer) and `dlopen`s the platform EGL/GLES libraries
  that ship on any i.MX95 BSP image. No extra packages required on the target.
  (A truly *static* GPU binary is impossible — the Mali userspace driver is a
  vendor blob loaded at runtime — so "single binary" means *one executable we
  ship that uses the platform's media libs.*)
- **Backend abstraction.** Every workload implements a small `Workload`
  interface; the DDR monitor implements `DdrMonitor`. The host build links
  **mock** implementations; a target build (`-DIMX95_TARGET=ON`) links the
  **real** ones. This is what lets the CLI be developed and CI-tested without
  silicon.
- **DDR memory traffic is the headline metric.** Read from the i.MX9 DDR
  performance monitor (`perf_event_open`) on real hardware. It is global to the
  SoC, which is exactly why it reveals one block stealing bandwidth from
  another.

See [`docs/DESIGN.md`](docs/DESIGN.md) for the architecture and roadmap.

## Build

All-mock build (runs anywhere, including under qemu-imx95):

```sh
cmake -S . -B build
cmake --build build -j
./build/imx95-test
```

Pick real backends per subsystem with `-DIMX95_GPU=`, `-DIMX95_VPU=`,
`-DIMX95_DDR=`. The GLES GPU backend needs `egl` + `glesv2` (Mesa on a host;
the Mali libs on an i.MX95 image) and runs headless:

```sh
cmake -S . -B build-gles -DIMX95_GPU=gles   # real GPU, mock VPU/DDR
cmake --build build-gles -j
./build-gles/imx95-test
```

For an i.MX95 target, configure with the Yocto BSP toolchain file and the same
`-DIMX95_GPU=gles` (plus `-DIMX95_VPU=v4l2 -DIMX95_DDR=pmu` once those land).

## Using it

```text
== i.MX95 Media Test Framework ==   (backend: mock)
config:  nothing configured
  1) Configure workloads
  2) Run
  3) Load / Save config
  q) Quit
```

- **Configure** toggles each block on/off and sets its level/resolution.
  Selections apply immediately and are always shown; `b` goes back everywhere —
  no dead ends.
- **Run** offers *continuous* (until `Ctrl-C` or the space-menu), *once*, or a
  *fixed loop count*. During a run a live dashboard shows per-workload fps and
  the rolling DDR bandwidth; press **space** for a menu (resume / stop & report
  / quit) or **Ctrl-C** to stop gracefully. Either way you get a final report:

```text
==== Run report ====
backend: mock   mode: continuous   elapsed: 12.41 s   ended: stopped
  GPU mid    frames       1984   avg   159.9 fps   moved 65.78 GB   footprint 0.07 GB
  DEC 1080p  frames       1489   avg   120.0 fps   moved 4.63 GB    footprint 0.02 GB
  DDR        read 72.1 GB   write 51.4 GB   total 123.5 GB   avg 9.95 GB/s
====================
```

## Media assets

Decode workloads use **Big Buck Bunny** (© Blender Foundation, CC-BY 3.0),
which is available pre-encoded at 720p/1080p/4K. Clips are **fetched on demand**
by `scripts/fetch-assets.sh`, never committed to the repo.

## License

BSD-3-Clause. See [`LICENSE`](LICENSE).
