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

**First thing to do on a new board:** start the app and press **`c`** (Check
system). It probes GPU/VPU/NPU/DDR and reports what's runnable and why — a fast
way to confirm the board before configuring a run.

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
for your board** (see the conversion routes below). Point the harness at it one
of two ways:

```sh
# A) Auto-detect: drop the converted .tflite next to the binary and just run.
#    The harness picks the one .tflite that actually carries Neutron ops
#    (by content, so the name is irrelevant; plain .tflite files are ignored;
#    if several converted models are present it asks you to pin one).
./imx95-test

# B) Explicit (always wins; required if multiple converted models are present):
IMX95_NPU_MODEL=/path/model_neutron.tflite ./imx95-test
# optional: IMX95_NPU_BENCH=<benchmark_model>  IMX95_NPU_DELEGATE=<.so>  IMX95_NPU_RUNS=500
```

**The converter version must match your board's BSP** — a mismatch makes
`libNeutronDriver.so` segfault at model-prepare (`privateNeutronModelPrepareLegacy`)
even though the delegate partitions the graph (`1 node delegated`). To find the
matching converter:

1. Identify your BSP release: `cat /etc/os-release` + the installed `neutron`
   package version, or match your board to a branch of NXP's
   [`nxp-imx/neutron`](https://github.com/nxp-imx/neutron) repo (its firmware +
   driver bytes match the board exactly).
2. Map that release's date to the eIQ **converter SDK quarter**. Example: the EVK
   here is `lf-6.12.49_2.2.0` (Q4 2025) → eIQ **SDK 25-12**.
3. Convert on an x86 host with that quarter's converter (from
   `https://eiq.nxp.com/repository`):

```py
import neutron_converter_SDK_25_12.neutron_converter as nc   # <- the quarter that matches your BSP
open("m_neutron.tflite","wb").write(bytes(
    nc.convertModel(list(open("m_quant.tflite","rb").read()), "imx95")))
```

**Or convert online** with the [eIQ AI Hub](https://eiq.nxp.com/ai-hub) (cloud,
NXP sign-in): *AI Toolkit → Optimize & convert → NeutronConversion*, choosing
**Target `imx95`** and the **Flavor (version)** matching your board's BSP. Same
rule — the flavor must match your firmware build, so verify with `c) Check system`
after deploying. Inputs: TFLite / ONNX / PyTorch.

> **Verified:** on `lf-6.12.49_2.2.0`, MobileNet converted with SDK **25-12** runs
> at **~1.7 ms/inference (≈32× over CPU)** — `1 node delegated`, `EXIT=0`. The
> harness probes the model at startup and fails with guidance if the converter is
> the wrong version; `c) Check system` also reports the firmware build stamp.

### Converting on the target (when the BSP ships the converter)

Some BSPs include `libNeutronConverter.so` on the rootfs. Converting *on the
board with its own converter* guarantees the microcode matches the board's
driver — the cleanest fix for version skew.

**From the harness — menu `n) Prepare NPU model` (preferred).** It reads the
firmware and on-board converter build stamps and only converts when they
**match**; on a mismatch (or no converter present) it refuses and tells you to
convert on a host, so it never converts into the driver's segfault. It caches the
result (named with the firmware stamp), times the compile (printing `Converted in
N s`), and offers to set `IMX95_NPU_MODEL` for the session. `c) Check system`
reports the same alignment. Optional env: `IMX95_NPU_TARGET` (default `imx95`).

**Standalone helper.** `tools/neutron-convert-on-target.cpp` is a tiny aarch64
tool that does the same `dlopen` + `converter::convertModel(bytes, target)` call
outside the menu (handy for scripting/CI):

```sh
aarch64-linux-gnu-g++ -O2 -o neutron-convert-on-target tools/neutron-convert-on-target.cpp -ldl
# on the board:
./neutron-convert-on-target --list                                  # list targets
./neutron-convert-on-target model_quant.tflite model_neutron.tflite imx95
```

> The `--lib` flag points at the converter `.so`. **Caveat (the menu enforces
> this for you):** the converter must still match the *running* firmware. On a
> multi-image board, a converter on a second partition can belong to a different
> (older) eIQ release than the live firmware — converting with it then still
> segfaults. Confirm both come from the same eIQ build (compare the `clang
> version` build stamps).

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
