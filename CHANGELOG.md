# Changelog

All notable changes to **imx95-media-test** are documented here. Format loosely
follows [Keep a Changelog](https://keepachangelog.com/); versions are the git
tags (`vX.Y.Z`), each with a matching GitHub release carrying the prebuilt
aarch64 binary.

## [v1.5.3] — 2026-06-12
### Fixed
- **VPU probe verifies the codec node, not just its advertised caps.** Running
  v1.5.2 on qemu95 (via Holobench) surfaced a false positive: the probe matched
  `/dev/video` nodes by `QUERYCAP` (M2M caps) + `ENUM_FMT` (codec format), so a
  node that only *advertises* the capability — a qemu Wave registration stub, or
  a mis-identified capture node — showed up as a usable codec. `vpu_check()` now
  calls `V4l2M2m::verify_codec_node()`, which `S_FMT`s the OUTPUT (input-data)
  queue and `REQBUFS(1)`/`REQBUFS(0)` on it (the same sequence the real workload
  init runs). A node that can't allocate a codec input queue is reported as
  `advertised only — REQBUFS failed` and doesn't count as runnable.
### Notes
- Correct on real i.MX95 (a true Wave codec allocates fine — validated on a b307
  board: `VPU [ok]` + a real 720p decode at 489.7 fps). The probe is a preflight
  reachability + buffer-allocation check; the actual Run (timeout-bounded) is the
  real transcode test.

## [v1.5.2] — 2026-06-12
### Changed
- **In-binary `h) Help` brought up to date with the NPU workflow** — documents
  auto-detect (drop a converted `.tflite` beside the binary), the three ways to
  get a matching model (menu `n`, x86 host converter, eIQ AI Hub), and that
  `c) Check system` reports the model + its converter version. "WHAT IT DOES" now
  mentions the NPU alongside GPU/VPU. Docs-in-binary only; no functional change.

## [v1.5.1] — 2026-06-12
### Added
- **Converter version display.** `c) Check system` and the auto-detect line now
  report the model's converter version + target, parsed from the flatbuffer
  (e.g. `converter 2.2.3, target imx95`) via `neutron_model_info()`.
### Fixed
- **Actionable multi-model error** — shows *how* to pin one
  (`IMX95_NPU_MODEL=./model.tflite ./imx95-test`) and lists the candidates.
- **Missing vs unconverted model** — the run-menu note now distinguishes a
  missing pinned file ("will fail to init") from a present-but-unconverted one
  ("will run on CPU"), which it previously conflated.
- `c) Check system` hides the "no on-board converter" advisory when the NPU is
  already green.

## [v1.5.0] — 2026-06-11
### Added
- **NPU model auto-detection** — with `IMX95_NPU_MODEL` unset, the harness finds
  a neutron-converted `.tflite` next to the binary (or in the cwd) by *content*,
  so the filename is irrelevant and plain `.tflite` files are ignored. Exactly
  one converted model → used; two or more → asks you to pin one; explicit env
  always wins.
- **Menu walkthrough test** (`tests/menu_walkthrough.sh`) — ASan+UBSan harness
  driving every menu option + the NPU model-resolution cases (19/19 clean).
### Fixed
- **Reliable NPU probe** — `run_batch()` no longer trusts `pclose()`'s exit
  status (the harness sets `SIGCHLD=SIG_IGN` for detached-run reaping, which made
  `pclose()` return `-1/ECHILD` even on a clean exit). Success is now judged on
  the inference count — a converter/firmware mismatch crashes at model-prepare
  before any count is printed.

## [v1.4.0] — 2026-06-11
### Added
- **On-target NPU model conversion** (`n) Prepare NPU model`) — converts a
  quantized `.tflite` on the board via its own `libNeutronConverter.so`, **gated
  on a converter↔firmware build-stamp match** so it refuses (with the host-
  conversion recipe) rather than producing microcode the driver would reject.
  Shared module `neutron_convert.cpp` (`neutron_alignment()`,
  `tflite_is_converted()`, `neutron_convert_file()`); progress dots + timing +
  firmware-stamped output cache; `c) Check system` + Run cross-links.

## [v1.3.0] — 2026-06-11
### Added
- **NPU working end-to-end — all four blocks on real i.MX95 silicon.** Root cause
  of the prior segfault was a converter↔firmware microcode mismatch; the fix is
  to map the board's BSP (`lf-6.12.49_2.2.0` → eIQ **SDK 25-12**). MobileNet then
  runs at ~1.7 ms/inf (≈32× over CPU), `1 node delegated`, `EXIT=0`.

## [v1.2.2] — 2026-06-11
### Added
- `c) Check system` surfaces the NPU firmware↔converter version mismatch (the
  eIQ build-stamp comparison).
- On-target Neutron converter tool (`tools/neutron-convert-on-target.cpp`) +
  documentation of the eIQ-alignment gotcha.

## [v1.2.1] — 2026-06-11
### Changed
- Comprehensive README with screenshots and explicit (at-the-time) NPU status.

## [v1.2.0] — 2026-06-11
### Added
- **`c) Check system`** preflight diagnostics — probes each block and reports
  what's runnable on the target, and why something isn't.

## [v1.1.0] — 2026-06-11
### Added
- **NPU (eIQ Neutron) as the fourth workload block** (`-DIMX95_NPU=bench`),
  driving `benchmark_model` + the Neutron delegate as a managed subprocess.

## [v1.0.3] — 2026-06-11
### Added
- Built-in paginated **help** (`h`); refreshed README screenshots.

## [v1.0.2] — 2026-06-11
### Added
- Manage detached runs **in-app** (list/stop, no shell needed); clearer detached UX.

## [v1.0.1] — 2026-06-11
### Added
- **Detached background runs** (`fork`/`setsid`) with progress logging to a file.

## [v1.0.0] — 2026-06-11
### Added
- Initial release: interactive single-binary CLI harness for the i.MX95
  GPU + VPU media blocks with a global DDR-bandwidth monitor; continuous / once /
  fixed-count run modes, live dashboard, per-run report, graceful `Ctrl-C`/space
  stop; per-subsystem `mock`/real backends (`gles` GPU, `v4l2` VPU, `pmu` DDR),
  embedded Big Buck Bunny clips, cross-compiles with no BSP sysroot.

[v1.5.3]: https://github.com/kylefoxaustin/imx95-media-test/releases/tag/v1.5.3
[v1.5.2]: https://github.com/kylefoxaustin/imx95-media-test/releases/tag/v1.5.2
[v1.5.1]: https://github.com/kylefoxaustin/imx95-media-test/releases/tag/v1.5.1
[v1.5.0]: https://github.com/kylefoxaustin/imx95-media-test/releases/tag/v1.5.0
[v1.4.0]: https://github.com/kylefoxaustin/imx95-media-test/releases/tag/v1.4.0
[v1.3.0]: https://github.com/kylefoxaustin/imx95-media-test/releases/tag/v1.3.0
[v1.2.2]: https://github.com/kylefoxaustin/imx95-media-test/releases/tag/v1.2.2
[v1.2.1]: https://github.com/kylefoxaustin/imx95-media-test/releases/tag/v1.2.1
[v1.2.0]: https://github.com/kylefoxaustin/imx95-media-test/releases/tag/v1.2.0
[v1.1.0]: https://github.com/kylefoxaustin/imx95-media-test/releases/tag/v1.1.0
[v1.0.3]: https://github.com/kylefoxaustin/imx95-media-test/releases/tag/v1.0.3
[v1.0.2]: https://github.com/kylefoxaustin/imx95-media-test/releases/tag/v1.0.2
[v1.0.1]: https://github.com/kylefoxaustin/imx95-media-test/releases/tag/v1.0.1
[v1.0.0]: https://github.com/kylefoxaustin/imx95-media-test/releases/tag/v1.0.0
