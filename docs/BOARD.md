# Running on an i.MX95 board / EVK

The deploy artifact is one small binary plus no media files (the VPU workloads
are self-sourcing). How you produce that binary depends on your environment.

## Build

Pick whichever matches the EVK. All three produce the same full-real binary:
`-DIMX95_GPU=gles -DIMX95_VPU=v4l2 -DIMX95_DDR=pmu`.

### A. NXP/Yocto SDK cross-compile (recommended)

The SDK sysroot already has the Mali EGL/GLES libs, V4L2, and perf headers, and
its glibc matches the board — so the GLES backend links normally, no surprises.

```sh
source /opt/fsl-imx-.../environment-setup-armv8a-poky-linux   # sets CC/CXX/sysroot
cmake -S . -B build-target -DIMX95_GPU=gles -DIMX95_VPU=v4l2 -DIMX95_DDR=pmu
cmake --build build-target -j
# -> build-target/imx95-test   (upload this single file)
```

### B. Build natively on the EVK

If the board image has `g++` + `cmake` (and the Mali/V4L2 dev headers):

```sh
cmake -S . -B build -DIMX95_GPU=gles -DIMX95_VPU=v4l2 -DIMX95_DDR=pmu
cmake --build build -j
```

### C. Generic aarch64 cross, no SDK (produces an upload-and-run binary)

Works with just Ubuntu's `gcc-aarch64-linux-gnu` — no BSP sysroot needed —
because the GLES backend `dlopen`s the Mali libs at runtime (so nothing GPU is
linked) and the VPU/DDR backends use only kernel uAPI. `libstdc++`/`libgcc` are
linked statically; the binary needs only `libc`/`libm` (GLIBC ≤ 2.34, so it runs
on the i.MX95 BSP's newer glibc).

```sh
cmake -S . -B build-aarch64 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
      -DIMX95_GPU=gles -DIMX95_VPU=v4l2 -DIMX95_DDR=pmu
cmake --build build-aarch64 -j
aarch64-linux-gnu-strip build-aarch64/imx95-test   # ~1.1 MB
# -> upload build-aarch64/imx95-test to the board and run it
```

## Run

Run as **root** — the DDR PMU needs permission for system perf events and the
codec/GPU device nodes typically need it too:

```sh
./imx95-test                      # H.264 VPU by default
# useful overrides:
IMX95_VPU_CODEC=hevc ./imx95-test         # or h264 (default)
IMX95_DDR_BEAT_BYTES=32 ./imx95-test      # confirm against the i.MX95 RM
IMX95_VPU_ENCODE_DEV=/dev/videoN ./imx95-test   # pin nodes if auto-discovery misses
IMX95_VPU_DECODE_DEV=/dev/videoM
IMX95_VPU_STREAM=/path/clip.h264 ./imx95-test   # decode your own Annex-B stream
```

**Decode content:** H.264 decode uses the real Big Buck Bunny clips baked into
the binary (720p/1080p/4K, picked to match the menu resolution), so decode fps
is representative. Order of preference: `IMX95_VPU_STREAM` file → embedded clip →
synthetic fallback. (HEVC/FWHT have no embedded clip, so they use the synthetic
bitstream unless you pass `IMX95_VPU_STREAM`.)

GPU levels default to Mali-G310-appropriate loads. Tune any field live without
rebuilding (overrides the selected level):

```sh
# push "max" toward a true peg (4K, heavier shader):
IMX95_GPU_W=3840 IMX95_GPU_H=2160 IMX95_GPU_EXTRA=192 IMX95_GPU_PASSES=2 ./imx95-test
# knobs: IMX95_GPU_{W,H,SUBDIV,INSTANCES,LIGHTS,EXTRA,PASSES}
```

To run **without tying up the terminal** (e.g. while you use the board for
something else), choose **Run → 4) Detached → log file**: it forks a background
process that logs progress snapshots to a file, then drops you back at the menu —
where you can launch a second (foreground or detached) run concurrently. At the
log-file prompt, press **Enter** to accept the default name. A *once*/fixed run
finishes and writes its report to the log; a *continuous* run logs until stopped.

Stop a detached run from the **main menu → Detached runs** (lists each PID and
config; pick a number to stop it, or `a` for all) — no shell needed. It can also
be stopped from a shell with `kill <pid>` (printed when it starts). Either way it
writes a final report to its log. `tail -f <log>` to watch.

Suggested first run: **Configure** GPU mid + VPU decode 1080p + VPU encode 1080p,
**Run → Continuous**, watch the live DDR bandwidth, then `Ctrl-C` for the report.
Then re-run a single block alone and compare the DDR numbers — that delta is the
cross-block interference the tool exists to measure.

## NPU (eIQ Neutron) — `-DIMX95_NPU=bench`

The NPU workload loops inference through the platform's `benchmark_model` + the
Neutron delegate. It needs a quantized `.tflite` that has been **neutron-converted
for your board**, then:

```sh
IMX95_NPU_MODEL=/path/model_neutron.tflite ./imx95-test   # required
# optional: IMX95_NPU_BENCH=<benchmark_model>  IMX95_NPU_DELEGATE=<.so>  IMX95_NPU_RUNS=500
```

**Converting a model** (on an x86 host) with NXP's `neutron_converter_SDK_<qtr>`
(from `https://eiq.nxp.com/repository`):

```py
import neutron_converter_SDK_25_03.neutron_converter as nc
open("m_neutron.tflite","wb").write(bytes(
    nc.convertModel(list(open("m_quant.tflite","rb").read()), "imx95")))
```

> **Critical: the converter version must match the board's BSP.** The Neutron
> firmware/driver and the converter share a microcode format; a mismatch makes
> `libNeutronDriver.so` crash in model-prepare (segfault) even though the
> delegate partitions the graph. Pick the converter quarter from the firmware
> build date: `strings /lib/firmware/NeutronFirmware.elf | grep 'clang version'`.
> If no public converter matches an early/pre-release BSP, use the converter
> bundled with that exact BSP release. The harness probes the model on startup
> and fails with this guidance if it doesn't execute.

## Things to watch on first bring-up (please report back)

- **GPU/EGL headless:** the backend renders without a compositor by opening a
  DRM node via GBM (tries `EGL_MESA_platform_surfaceless` → GBM
  `/dev/dri/renderD128` → default display). If GPU init fails, check
  `ls /dev/dri/` and pin the node with `IMX95_DRM_DEVICE=/dev/dri/cardN`, or
  force a platform with `IMX95_EGL_PLATFORM=gbm|default`. (Weston does not need
  to be running — headless GBM is independent of it.)
- **VPU device nodes / codec:** if "no V4L2 encoder/decoder device found",
  check `ls /dev/video*` and which nodes are the Wave encoder vs decoder, then
  pin them with `IMX95_VPU_{ENCODE,DECODE}_DEV`.
- **DDR PMU:** if it prints "DDR PMU unavailable", confirm
  `ls /sys/bus/event_source/devices/ | grep imx9_ddr` and that you're root.
- **DDR beat size:** verify bytes-per-beat against the reference manual and set
  `IMX95_DDR_BEAT_BYTES` accordingly so the bandwidth figures are accurate.
