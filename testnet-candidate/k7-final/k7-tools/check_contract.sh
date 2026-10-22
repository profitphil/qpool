#!/bin/bash
# check_contract.sh -- does a QPOOL header compile (and start) as a real Qubic contract?
#
#   usage: check_contract.sh [--compile-only] [--keep] [--warnings] [--timeout SEC] <path-to-QPool-header>
#
#   exit 0  PASS    every stage that ran succeeded
#   exit 1  FAIL    the header is at fault (errors are printed with file:line inside the header)
#   exit 2  ERROR   the harness itself is broken or could not judge the header (nothing is claimed about the header)
#   exit 3  usage
#
# What it does (all of it on a scratch copy of the core, see HARNESS.md; the core checkout is never touched):
#   1. copies the header into a per-invocation overlay as src/contracts/QPool.h of the scratch core
#      (struct QPOOL / QPOOL2, contract index 30, wired in the scratch contract_def.h)
#   2. COMPILE  compiles tools/probe_qpool.cpp, which includes the core's contract testing scaffolding
#      (test/contract_testing.h -> contract_def.h -> the header) and instantiates the contract with
#      INIT_CONTRACT + callSystemProcedure(INITIALIZE). Real code generation (-c, -O0), not -fsyntax-only,
#      with -Wall -Wextra -Wpedantic -Wcast-align. Only diagnostics attributable to the header are printed.
#   3. LINK + SMOKE (unless --compile-only): links an x86_64 binary and runs it under Rosetta (arch -x86_64):
#      INITIALIZE, then every registered user function once with an all-zero input. Runs are serialised
#      machine-wide by a lock (a run needs about 1.4 GB of RAM); compiles are fully parallel.
#
# Safe to run several times concurrently: every invocation builds in its own mktemp directory; the shared scratch
# core and gtest library are read-only after their one-time creation (guarded by a lock).
set -u

TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
K7="$(cd "$TOOLS/.." && pwd)"
SCRATCH="${QPOOL_SCRATCH_CORE:-$K7/core-scratch}"
WORKROOT="${QPOOL_HARNESS_WORK:-$K7/work/check_contract}"
CXX="${QPOOL_CXX:-clang++}"
COMPILE_TIMEOUT=300
RUN_TIMEOUT=300

DO_RUN=1; KEEP=0; WARN_ARG=""
HEADER=""
while [ $# -gt 0 ]; do
    case "$1" in
        --compile-only) DO_RUN=0 ;;
        --keep) KEEP=1 ;;
        --warnings) WARN_ARG="--warnings" ;;
        --timeout) shift; RUN_TIMEOUT="${1:-300}"; COMPILE_TIMEOUT="${1:-300}" ;;
        -h|--help) sed -n '2,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "unknown option: $1" >&2; exit 3 ;;
        *) if [ -n "$HEADER" ]; then echo "only one header allowed" >&2; exit 3; fi; HEADER="$1" ;;
    esac
    shift
done
[ -n "$HEADER" ] || { echo "usage: check_contract.sh [--compile-only] [--keep] [--warnings] <header>" >&2; exit 3; }
[ -f "$HEADER" ] && [ -r "$HEADER" ] || { echo "cannot read header: $HEADER" >&2; exit 3; }
HEADER="$(cd "$(dirname "$HEADER")" && pwd)/$(basename "$HEADER")"

now() { date +%s; }
T0=$(now)
say() { printf '%s\n' "$*"; }

# ---------------------------------------------------------------------------------------------- helpers
run_with_timeout() {  # run_with_timeout SECONDS cmd args...   (macOS has no timeout(1))
    local t="$1"; shift
    "$@" &
    local pid=$!
    ( sleep "$t" & sp=$!
      trap 'kill "$sp" 2>/dev/null; exit 0' TERM
      wait "$sp"
      kill -TERM "$pid" 2>/dev/null; sleep 3; kill -KILL "$pid" 2>/dev/null ) >/dev/null 2>&1 &
    local watcher=$!
    wait "$pid"; local rc=$?
    kill -TERM "$watcher" 2>/dev/null; wait "$watcher" 2>/dev/null
    return $rc
}

lock_acquire() {  # lock_acquire DIR MAXWAIT : mkdir lock with stale-owner detection
    local dir="$1" maxwait="$2" waited=0
    while ! mkdir "$dir" 2>/dev/null; do
        local owner; owner="$(cat "$dir/pid" 2>/dev/null || true)"
        if [ -n "$owner" ] && ! kill -0 "$owner" 2>/dev/null; then rm -rf "$dir"; continue; fi
        [ "$waited" -ge "$maxwait" ] && return 1
        sleep 1; waited=$((waited + 1))
    done
    echo $$ > "$dir/pid"
    return 0
}
lock_release() { rm -rf "$1"; }

WORK=""
cleanup() {
    [ -n "${HELD_RUN_LOCK:-}" ] && lock_release "$HELD_RUN_LOCK"
    if [ -n "$WORK" ] && [ "$KEEP" = 0 ]; then rm -rf "$WORK"; fi
}
trap cleanup EXIT
trap 'cleanup; exit 130' INT TERM

# ---------------------------------------------------------------------------------------------- prechecks
say "check_contract: $HEADER"
if ! grep -Eq '^[[:space:]]*struct[[:space:]]+QPOOL2[[:space:]]*(\{|$)' "$HEADER" \
   || ! grep -Eq '^[[:space:]]*struct[[:space:]]+QPOOL[[:space:]]*:[[:space:]]*public[[:space:]]+ContractBase' "$HEADER"; then
    say "  the header does not declare 'struct QPOOL2' and 'struct QPOOL : public ContractBase' (the names contract_def.h wires)"
    say "FAIL"
    exit 1
fi

if [ ! -e "$SCRATCH/.qpool_scratch_ok" ]; then
    say "  scratch core missing, creating it (one-time)..."
    bash "$TOOLS/setup_scratch.sh" >&2 || { say "ERROR: could not create the scratch core"; exit 2; }
fi
command -v "$CXX" >/dev/null 2>&1 || { say "ERROR: $CXX not found"; exit 2; }

mkdir -p "$WORKROOT"
WORK="$(mktemp -d "$WORKROOT/chk.XXXXXX")" || { say "ERROR: cannot create work dir"; exit 2; }
mkdir -p "$WORK/ov/contracts"
cp "$HEADER" "$WORK/ov/contracts/QPool.h"
OVERLAY="$WORK/ov/contracts/QPool.h"

INC=(-I"$WORK/ov" -I"$K7/gtest/include" -I"$SCRATCH/lib/platform_common" -I"$SCRATCH/lib/platform_os"
     -I"$SCRATCH/lib/platform_efi" -I"$SCRATCH/src" -I"$SCRATCH" -I"$SCRATCH/test")
# Flags mirror the core's own test build (cmake/CompilerSetup.cmake) except: x86_64 target on any host,
# BMI/LZCNT/POPCNT/FMA on (the core's code needs them for real code generation), no -Werror, no AVX-512
# (Rosetta cannot run it; the AVX-512 code paths are guarded by __AVX512F__ and unused by the probe).
CXXFLAGS=(-std=c++20 -target x86_64-apple-macos13 -fshort-wchar -O0
          -mavx -mavx2 -mbmi -mbmi2 -mlzcnt -mpopcnt -mfma -mrdrnd
          -include "$TOOLS/qpool_compat.h" "${INC[@]}")
DIAGFLAGS=(-Wall -Wextra -Wpedantic -Wcast-align -Wno-unused-parameter -Wno-cast-function-type-mismatch -fno-caret-diagnostics -fno-color-diagnostics -ferror-limit=200)

# ---------------------------------------------------------------------------------------------- 1. compile
STAGE_T=$(now)
if [ "$DO_RUN" = 1 ]; then PROBE_OUT="$WORK/probe_qpool.o"; else PROBE_OUT="/dev/null"; fi
run_with_timeout "$COMPILE_TIMEOUT" "$CXX" "${CXXFLAGS[@]}" "${DIAGFLAGS[@]}" -c -o "$PROBE_OUT" \
    "$TOOLS/probe_qpool.cpp" 2> "$WORK/compile.diag"
CRC=$?
python3 -I "$TOOLS/filter_diag.py" "$WORK/compile.diag" "$OVERLAY" "$HEADER" $WARN_ARG > "$WORK/compile.report"
FRC=$?
cat "$WORK/compile.report"
if [ "$FRC" = 1 ]; then
    say "  stage compile: FAILED ($(( $(now) - STAGE_T )) s)"
    say "FAIL"
    exit 1
elif [ "$FRC" = 2 ]; then
    say "  stage compile: harness/core errors only, the header was not judged"
    PP="$(grep -n '^[[:space:]]*#' "$HEADER" | head -3)"
    [ -n "$PP" ] && say "  hint: the header contains preprocessor directives (a macro defined in the header can break core code that is compiled after it; contracts may not use '#'):" && say "$PP" | sed 's/^/    /'
    say "ERROR: the header was not judged: only core/scaffolding errors (see above). If the header is unchanged since a PASS, rebuild the scratch core with setup_scratch.sh --force; otherwise run lint_contract.py on it."
    exit 2
elif [ "$FRC" = 3 ] || [ "$CRC" != 0 ]; then
    # compiler failed without a parsable error (crash, timeout, bad flag)
    say "  stage compile: compiler exit status $CRC without an attributable diagnostic; last lines of its output:"
    tail -n 15 "$WORK/compile.diag" | sed 's/^/    /'
    say "ERROR: harness problem"
    exit 2
fi
say "  stage compile: ok ($(( $(now) - STAGE_T )) s)  [clang++ x86_64 -O0 real codegen, header instantiated as contract index 30 via test/contract_testing.h]"

if [ "$DO_RUN" = 0 ]; then
    say "  stages link and smoke run: NOT RUN (--compile-only)"
    say "PASS"
    exit 0
fi

# ---------------------------------------------------------------------------------------------- 2. link
STAGE_T=$(now)
GTEST_LIB="$K7/gtest/build/libgtest_x86_64.a"
if [ ! -e "$GTEST_LIB" ]; then
    GT_LOCK="$K7/gtest/.build.lock"
    lock_acquire "$GT_LOCK" 300 || { say "ERROR: gtest build lock timeout"; exit 2; }
    if [ ! -e "$GTEST_LIB" ]; then
        mkdir -p "$K7/gtest/build"
        GT_TMP="$(mktemp -d "$K7/gtest/build/tmp.XXXXXX")"
        ( cd "$GT_TMP" && "$CXX" -std=c++20 -target x86_64-apple-macos13 -O1 -w -I"$K7/gtest/include" -I"$K7/gtest" -c "$K7/gtest/src/gtest-all.cc" -o gtest-all.o \
          && "$CXX" -std=c++20 -target x86_64-apple-macos13 -O1 -w -I"$K7/gtest/include" -I"$K7/gtest" -c "$K7/gtest/src/gtest_main.cc" -o gtest_main.o \
          && ar rcs libgtest_x86_64.a gtest-all.o gtest_main.o ) >/dev/null 2>&1 \
          && mv "$GT_TMP/libgtest_x86_64.a" "$GTEST_LIB"
        rm -rf "$GT_TMP"
    fi
    lock_release "$GT_LOCK"
    [ -e "$GTEST_LIB" ] || { say "ERROR: could not build the x86_64 gtest library"; exit 2; }
fi

SUPPORT_FLAGS=("${CXXFLAGS[@]}" -w)
OBJS=("$WORK/probe_qpool.o")
build_obj() {  # build_obj src out
    "$CXX" "${SUPPORT_FLAGS[@]}" -c -o "$2" "$1" 2>> "$WORK/support.diag" || return 1
    OBJS+=("$2")
}
: > "$WORK/support.diag"
build_obj "$SCRATCH/test/common_def.cpp" "$WORK/common_def.o" \
 && build_obj "$SCRATCH/test/stdlib_impl.cpp" "$WORK/stdlib_impl.o" \
 && build_obj "$SCRATCH/lib/platform_os/processor.cpp" "$WORK/processor.o" \
 && build_obj "$SCRATCH/lib/platform_os/sleep.cpp" "$WORK/sleep.o" \
 && build_obj "$TOOLS/qpool_link_stubs.cpp" "$WORK/stubs.o"
SRC_RC=$?
if [ "$SRC_RC" != 0 ]; then
    python3 -I "$TOOLS/filter_diag.py" "$WORK/support.diag" "$OVERLAY" "$HEADER" > "$WORK/support.report"; FRC=$?
    cat "$WORK/support.report"
    if [ "$FRC" = 1 ]; then say "  stage link (support objects): FAILED"; say "FAIL"; exit 1; fi
    say "ERROR: harness problem while compiling the link support objects"
    tail -n 10 "$WORK/support.diag" | sed 's/^/    /'
    exit 2
fi
"$CXX" -target x86_64-apple-macos13 -o "$WORK/probe.bin" "${OBJS[@]}" "$GTEST_LIB" > "$WORK/link.out" 2>&1
LRC=$?
if [ "$LRC" != 0 ]; then
    # Undefined symbols that name the contract are the header's fault (it uses a QPI function the core does not define).
    if grep -E '^ +"' "$WORK/link.out" | grep -Eq 'QPOOL|QPool'; then
        say "undefined symbol(s) naming the contract:"
        grep -E '^ +"' "$WORK/link.out" | grep -E 'QPOOL|QPool' | sort -u | head -20 | sed 's/^/  /'
        say "  stage link: FAILED"
        say "FAIL"
        exit 1
    fi
    say "  stage link: failed without naming the contract; linker output:"
    head -n 20 "$WORK/link.out" | sed 's/^/    /'
    say "ERROR: harness problem"
    exit 2
fi
say "  stage link: ok ($(( $(now) - STAGE_T )) s)"

# ---------------------------------------------------------------------------------------------- 3. smoke run
if ! arch -x86_64 /usr/bin/true >/dev/null 2>&1; then
    say "  stage smoke run: NOT RUN (Rosetta 2 / arch -x86_64 unavailable)"
    say "PASS (compile and link only; the header was NOT executed)"
    exit 0
fi
STAGE_T=$(now)
RUN_LOCK="$WORKROOT/.smoke_run.lock"
lock_acquire "$RUN_LOCK" 900 || { say "ERROR: timed out waiting for the smoke-run lock"; exit 2; }
HELD_RUN_LOCK="$RUN_LOCK"
run_with_timeout "$RUN_TIMEOUT" arch -x86_64 "$WORK/probe.bin" > "$WORK/run.out" 2>&1
RRC=$?
lock_release "$RUN_LOCK"; HELD_RUN_LOCK=""
if [ "$RRC" = 0 ] && grep -q '^\[  PASSED  \] 1 test' "$WORK/run.out" && grep -q 'QPOOL_HARNESS_SMOKE functions_called=' "$WORK/run.out"; then
    N="$(sed -n 's/.*QPOOL_HARNESS_SMOKE functions_called=\([0-9]*\).*/\1/p' "$WORK/run.out" | head -1)"
    say "  stage smoke run: ok ($(( $(now) - STAGE_T )) s)  [Rosetta x86_64: INITIALIZE + $N registered user functions called once with zeroed input]"
    say "PASS"
    exit 0
fi
say "  stage smoke run: FAILED (exit status $RRC$( [ "$RRC" -gt 128 ] && echo ", signal $((RRC - 128))" ))"
grep -E 'Failure|Expected|Which is|Actual|ASSERT|abort|returned an error|\[  FAILED' "$WORK/run.out" | head -20 | sed 's/^/    /'
say "  last lines of the run output:"
tail -n 12 "$WORK/run.out" | sed 's/^/    /'
say "FAIL"
exit 1
