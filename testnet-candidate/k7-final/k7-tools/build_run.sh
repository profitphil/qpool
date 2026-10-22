#!/bin/bash
# build_run.sh <header> <tag> [--cancel] [gtest_filter]
# Compiles cancel_qx_test.cpp against the scratch core with <header> overlaid as src/contracts/QPool.h, links it with the support objects
# of check_contract.sh and runs it under Rosetta. Output in work/C/test/out/<tag>/.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
K7="$(cd "$HERE/../../.." && pwd)"
TOOLS="$K7/tools"
SCRATCH="$K7/core-scratch"
HEADER="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; TAG="$2"; shift 2
DEFS=""; FILTER="*"
if [ "${1:-}" = "--cancel" ]; then DEFS="-DQPOOL_HAS_CANCEL=1"; shift; fi
[ -n "${1:-}" ] && FILTER="$1"
OUT="$HERE/out/$TAG"; mkdir -p "$OUT/ov/contracts"
cp "$HEADER" "$OUT/ov/contracts/QPool.h"
INC=(-I"$OUT/ov" -I"$K7/gtest/include" -I"$SCRATCH/lib/platform_common" -I"$SCRATCH/lib/platform_os" -I"$SCRATCH/lib/platform_efi" -I"$SCRATCH/src" -I"$SCRATCH" -I"$SCRATCH/test")
CXXFLAGS=(-std=c++20 -target x86_64-apple-macos13 -fshort-wchar -O0 -mavx -mavx2 -mbmi -mbmi2 -mlzcnt -mpopcnt -mfma -mrdrnd -include "$TOOLS/qpool_compat.h" "${INC[@]}")
# support objects (cached, independent of the header)
SUP="$HERE/out/support"; mkdir -p "$SUP"
for pair in "$SCRATCH/test/common_def.cpp:common_def" "$SCRATCH/test/stdlib_impl.cpp:stdlib_impl" "$SCRATCH/lib/platform_os/processor.cpp:processor" "$SCRATCH/lib/platform_os/sleep.cpp:sleep" "$TOOLS/qpool_link_stubs.cpp:stubs"; do
  src="${pair%%:*}"; name="${pair##*:}"
  if [ ! -e "$SUP/$name.o" ]; then clang++ "${CXXFLAGS[@]}" -w -c -o "$SUP/$name.o" "$src" || { echo "support compile failed: $name"; exit 2; }; fi
done
echo "[build] compiling the test ($TAG)"
clang++ "${CXXFLAGS[@]}" $DEFS -Wno-unused-parameter -w -c -o "$OUT/test.o" "$HERE/cancel_qx_test.cpp" 2> "$OUT/compile.err"
if [ $? -ne 0 ]; then echo "COMPILE FAILED"; grep -E 'error' "$OUT/compile.err" | head -30; exit 1; fi
clang++ -target x86_64-apple-macos13 -o "$OUT/test.bin" "$OUT/test.o" "$SUP"/*.o "$K7/gtest/build/libgtest_x86_64.a" > "$OUT/link.out" 2>&1 || { echo "LINK FAILED"; head -30 "$OUT/link.out"; exit 1; }
echo "[run] $TAG filter=$FILTER"
arch -x86_64 "$OUT/test.bin" --gtest_filter="$FILTER" 2>&1 | tee "$OUT/run.out" | grep -v '^\[ *RUN\|^\[       OK' | tail -80
echo "exit: ${PIPESTATUS[0]}"
