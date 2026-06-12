#!/usr/bin/env bash
# Menu walkthrough / fuzz harness.
#
# Drives the CLI through (essentially) every menu option a user can pick and
# asserts the program never crashes, aborts, or trips a sanitizer. It builds two
# sanitized (ASan+UBSan) binaries:
#   * a full MOCK build  -> exercises all menu navigation + run/detached/load/save
#   * a BENCH NPU build   -> exercises NPU model resolution (auto-detect / explicit /
#                            ambiguous / none) using a fake benchmark_model stub
#
# Exit status is non-zero if any case fails. Run from the repo root:
#   tests/menu_walkthrough.sh
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 2

SAN_CXX="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"
export ASAN_OPTIONS="abort_on_error=1:detect_leaks=0:halt_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1:abort_on_error=1"

pass=0; fail=0; failed_names=()

build() {  # build <dir> <extra-cmake-args...>
  local dir="$1"; shift
  echo ">> building $dir ($*)"
  cmake -S . -B "$dir" -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_FLAGS="$SAN_CXX" "$@" >/dev/null 2>&1 \
    && cmake --build "$dir" -j >/dev/null 2>&1 \
    || { echo "BUILD FAILED for $dir"; exit 2; }
}

# crash/sanitizer fingerprints in combined stdout+stderr
CRASH_RE='AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:|SUMMARY: .*Sanitizer|Segmentation fault|core dumped|double free|heap-buffer-overflow|stack-buffer-overflow|heap-use-after-free|munmap_chunk|Assertion .* failed|terminate called'

# run_case <name> <bin> <timeout_s> <signal> <allowed_rc_csv> <input>
run_case() {
  local name="$1" bin="$2" tmo="$3" sig="$4" allowed="$5" input="$6"
  local work out rc
  work="$(mktemp -d)"
  out="$(cd "$work" && printf '%b' "$input" | timeout -s "$sig" -k 5 "$tmo" "$bin" 2>&1)"
  rc=$?
  # stop any detached children this case may have forked, then clean up
  pkill -f "$bin" 2>/dev/null
  rm -rf "$work"

  if printf '%s' "$out" | grep -qE "$CRASH_RE"; then
    echo "FAIL [$name]  (sanitizer/crash)"
    printf '%s\n' "$out" | grep -E "$CRASH_RE" | head -3 | sed 's/^/     /'
    fail=$((fail+1)); failed_names+=("$name"); return
  fi
  if [[ ",$allowed," != *",$rc,"* ]]; then
    echo "FAIL [$name]  (exit $rc, allowed: $allowed)"
    printf '%s\n' "$out" | tail -4 | sed 's/^/     /'
    fail=$((fail+1)); failed_names+=("$name"); return
  fi
  echo "ok   [$name]"
  pass=$((pass+1))
}

# ---------------------------------------------------------------------------
build build-asan -DIMX95_GPU=mock -DIMX95_VPU=mock -DIMX95_NPU=mock -DIMX95_DDR=mock
MOCK="$ROOT/build-asan/imx95-test"

echo; echo "== menu navigation (mock, sanitized) =="
# top-level + invalid input handling
run_case quit-immediately       "$MOCK" 10 TERM 0 'q\n'
run_case invalid-then-quit      "$MOCK" 10 TERM 0 'x\n\nzzz\nq\n'
run_case help-all-pages         "$MOCK" 10 TERM 0 'h\n\n\n\nq\n'
run_case help-early-stop        "$MOCK" 10 TERM 0 'h\nq\nq\n'
run_case check-system           "$MOCK" 20 TERM 0 'c\n\nq\n'
run_case prepare-npu-no-stack   "$MOCK" 10 TERM 0 'n\n\nq\n'

# configure: every GPU level, every dec/enc resolution, npu toggle, clear
run_case configure-all-options  "$MOCK" 15 TERM 0 \
  '1\n1\n1\n2\n3\n4\nb\n2\n1\n2\n3\n4\nb\n3\n1\n2\n3\n4\nb\n4\n4\n4\nc\nb\nq\n'

# load / save round-trip
run_case loadsave-roundtrip     "$MOCK" 15 TERM 0 '1\n1\n3\nb\nb\n4\n1\n2\nb\nq\n'

# run modes
run_case run-nothing-configured "$MOCK" 10 TERM 0 '2\nq\n'
run_case run-once               "$MOCK" 40 TERM 0 '1\n1\n3\nb\n2\n3\nb\n4\nb\n2\n2\nq\n'
run_case run-fixed-count        "$MOCK" 40 TERM 0 '1\n1\n2\nb\nb\n2\n3\n2\nq\n'
# continuous foreground only stops on SIGINT (headless has no key-stop)
run_case run-continuous-int     "$MOCK" 6 INT 0,124,130 '1\n1\n2\nb\nb\n2\n1\nq\n'

# detached: empty list, then launch a continuous run and stop it from the menu
run_case detached-empty         "$MOCK" 10 TERM 0 '3\nb\nq\n'
run_case detached-launch-stopall "$MOCK" 20 TERM 0 \
  '1\n1\n2\nb\nb\n2\n4\n1\n\n3\na\nb\nq\n'
# detached fixed-count once (self-terminates), custom log name
run_case detached-once-namedlog "$MOCK" 20 TERM 0 \
  '1\n1\n2\nb\nb\n2\n4\n2\nwalk.log\n3\nb\nq\n'

# ---------------------------------------------------------------------------
build build-asan-bench -DIMX95_GPU=mock -DIMX95_VPU=mock -DIMX95_NPU=bench -DIMX95_DDR=mock
BENCH="$ROOT/build-asan-bench/imx95-test"

# fake benchmark_model: prints a benchmark_model-style "count=N" line
FAKEBENCH="$(mktemp)"
cat > "$FAKEBENCH" <<'STUB'
#!/bin/sh
n=1; for a in "$@"; do case "$a" in --num_runs=*) n="${a#--num_runs=}";; esac; done
echo "INFO: inference timings ... count=$n ..."
STUB
chmod +x "$FAKEBENCH"
export IMX95_NPU_BENCH="$FAKEBENCH"

# model-resolution cases need controlled directories; run_case uses a fresh
# tmp cwd, but these need planted files, so drive them inline here.
npu_case() {  # npu_case <name> <expect_substr> <setup_cmds> <env...>
  local name="$1" expect="$2" setup="$3"; shift 3
  local work out rc
  work="$(mktemp -d)"; cp "$BENCH" "$work/imx95-test"
  ( cd "$work" && eval "$setup" )
  out="$(cd "$work" && printf 'c\n\nq\n' | timeout -s TERM -k 5 20 env "$@" ./imx95-test 2>&1)"
  rc=$?
  rm -rf "$work"
  if printf '%s' "$out" | grep -qE "$CRASH_RE"; then
    echo "FAIL [$name]  (sanitizer/crash)"; fail=$((fail+1)); failed_names+=("$name"); return; fi
  if [ "$rc" -ne 0 ]; then
    echo "FAIL [$name]  (exit $rc)"; fail=$((fail+1)); failed_names+=("$name"); return; fi
  if ! printf '%s' "$out" | grep -qF "$expect"; then
    echo "FAIL [$name]  (missing: $expect)"
    printf '%s\n' "$out" | grep -iE 'NPU' | sed 's/^/     /'
    fail=$((fail+1)); failed_names+=("$name"); return; fi
  echo "ok   [$name]"; pass=$((pass+1))
}

echo; echo "== NPU model resolution (bench, sanitized, fake benchmark) =="
CONV='NeutronGraph'  # marker that makes tflite_is_converted() true
npu_case npu-autodetect-one     "Neutron inference OK (auto-detected" \
  "printf '$CONV one'   > m_2512.tflite; printf 'plain' > a.tflite; printf 'plain' > b.tflite"
npu_case npu-ambiguous-multi    "multiple converted models found" \
  "printf '$CONV one' > m1.tflite; printf '$CONV two' > m2.tflite"
npu_case npu-none-converted     "no NPU model" \
  "printf 'plain' > a.tflite; printf 'plain' > b.tflite"
npu_case npu-explicit-env       "Neutron inference OK (IMX95_NPU_MODEL" \
  "printf '$CONV pin' > pinned.tflite" IMX95_NPU_BENCH="$FAKEBENCH" IMX95_NPU_MODEL=./pinned.tflite

rm -f "$FAKEBENCH"

# ---------------------------------------------------------------------------
echo
echo "================================================================"
echo "  PASS: $pass    FAIL: $fail"
if [ "$fail" -ne 0 ]; then
  printf '  failed: %s\n' "${failed_names[*]}"
  exit 1
fi
echo "  all menu paths + NPU resolution cases clean (ASan+UBSan)"
