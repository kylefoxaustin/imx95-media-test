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

### C. Generic aarch64 cross (no SDK)

Possible but fragile: Ubuntu's `aarch64-linux-gnu-g++` has no Mali libs (the GLES
backend would need to `dlopen` them instead of linking — not yet wired) and its
glibc may be newer than the board's. Prefer A or B. Ask if you need this path
and it can be added.

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
```

Suggested first run: **Configure** GPU mid + VPU decode 1080p + VPU encode 1080p,
**Run → Continuous**, watch the live DDR bandwidth, then `Ctrl-C` for the report.
Then re-run a single block alone and compare the DDR numbers — that delta is the
cross-block interference the tool exists to measure.

## Things to watch on first bring-up (please report back)

- **GPU/EGL headless:** the backend tries `EGL_MESA_platform_surfaceless` then
  `eglGetDisplay(EGL_DEFAULT_DISPLAY)`. If EGL init fails with no compositor
  running, the Mali build may need a GBM/DRM display path — send the exact
  "Failed to init GPU ..." message and we'll add it.
- **VPU device nodes / codec:** if "no V4L2 encoder/decoder device found",
  check `ls /dev/video*` and which nodes are the Wave encoder vs decoder, then
  pin them with `IMX95_VPU_{ENCODE,DECODE}_DEV`.
- **DDR PMU:** if it prints "DDR PMU unavailable", confirm
  `ls /sys/bus/event_source/devices/ | grep imx9_ddr` and that you're root.
- **DDR beat size:** verify bytes-per-beat against the reference manual and set
  `IMX95_DDR_BEAT_BYTES` accordingly so the bandwidth figures are accurate.
