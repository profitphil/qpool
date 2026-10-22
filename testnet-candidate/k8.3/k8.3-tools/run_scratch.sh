#!/bin/bash
# run_scratch.sh <header> <probe.cpp> : compile+link+run a scratch probe against the harness's scratch core (same flags as check_contract.sh).
set -u
HDR="$1"; PROBE="$2"
K7=/private/tmp/claude-501/-Volumes-phildata-Github-qpool/bf5af256-2a64-4ee8-b625-cdaa0f93d688/scratchpad/k7
TOOLS="$K7/tools"; SCRATCH="$K7/core-scratch"
OUT="$K7/work/N3/build"; mkdir -p "$OUT/ov/contracts"
cp "$HDR" "$OUT/ov/contracts/QPool.h"
INC=(-I"$OUT/ov" -I"$K7/gtest/include" -I"$SCRATCH/lib/platform_common" -I"$SCRATCH/lib/platform_os" -I"$SCRATCH/lib/platform_efi" -I"$SCRATCH/src" -I"$SCRATCH" -I"$SCRATCH/test")
CXXFLAGS=(-std=c++20 -target x86_64-apple-macos13 -fshort-wchar -O0 -mavx -mavx2 -mbmi -mbmi2 -mlzcnt -mpopcnt -mfma -mrdrnd -include "$TOOLS/qpool_compat.h" "${INC[@]}" -w)
OBJS=()
# a private copy of test/common_def.cpp without the two epoch ASSERTs of its notifyContractOfIncomingTransfer: they compare the epoch with QPOOL's
# construction epoch (236) and the test contracts' IPO epochs are 137 and 154. Nothing else is changed; the core checkout and core-scratch are not touched.
mkdir -p "$OUT/patched"
grep -v "ASSERT(system.epoch" "$SCRATCH/test/common_def.cpp" > "$OUT/patched/common_def_patched.cpp"
for src in "$OUT/patched/common_def_patched.cpp" "$SCRATCH/test/stdlib_impl.cpp" "$SCRATCH/lib/platform_os/processor.cpp" "$SCRATCH/lib/platform_os/sleep.cpp" "$TOOLS/qpool_link_stubs.cpp"; do
  o="$OUT/$(basename "$src" .cpp).o"
  if [ ! -e "$o" ]; then clang++ "${CXXFLAGS[@]}" -c -o "$o" "$src" || exit 2; fi
  OBJS+=("$o")
done
clang++ "${CXXFLAGS[@]}" -c -o "$OUT/probe.o" "$PROBE" 2> "$OUT/compile.err" || { grep -E "error" "$OUT/compile.err" | head -30; exit 1; }
clang++ -target x86_64-apple-macos13 -o "$OUT/probe.bin" "$OUT/probe.o" "${OBJS[@]}" "$K7/gtest/build/libgtest_x86_64.a" 2> "$OUT/link.err" || { head -30 "$OUT/link.err"; exit 1; }
LOCK="$K7/work/check_contract/.smoke_run.lock"; mkdir -p "$K7/work/check_contract"
waited=0
while ! mkdir "$LOCK" 2>/dev/null; do
  owner="$(cat "$LOCK/pid" 2>/dev/null || true)"
  if [ -n "$owner" ] && ! kill -0 "$owner" 2>/dev/null; then rm -rf "$LOCK"; continue; fi
  [ "$waited" -ge 900 ] && { echo "lock timeout"; exit 2; }
  sleep 1; waited=$((waited+1))
done
echo $$ > "$LOCK/pid"; trap 'rm -rf "$LOCK"' EXIT
arch -x86_64 "$OUT/probe.bin" "${@:3}" 2>&1
