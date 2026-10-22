#!/bin/bash
# setup_scratch.sh -- (re)build the patched scratch copy of the Qubic core used by check_contract.sh.
#
#   usage: setup_scratch.sh [--force]
#
# Reads /Volumes/phildata/Github/core (override with QPOOL_CORE_SRC) READ-ONLY and writes only to the
# scratch directory (default: <k7>/core-scratch, override with QPOOL_SCRATCH_CORE).
# Steps: rsync src/test/lib/cmake (no .git, no build output) -> normalise CRLF -> apply the mechanical
# portability edits (patch_core.py) -> make sure a QPOOL contract is wired at a free index in
# contract_def.h (the owner's checkout already has it at index 30; if it does not, the same lines are added).
set -euo pipefail

TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
K7="$(cd "$TOOLS/.." && pwd)"
CORE_SRC="${QPOOL_CORE_SRC:-/Volumes/phildata/Github/core}"
SCRATCH="${QPOOL_SCRATCH_CORE:-$K7/core-scratch}"

if [ -e "$SCRATCH/.qpool_scratch_ok" ] && [ "${1:-}" != "--force" ]; then
    echo "scratch core already prepared: $SCRATCH (use --force to rebuild)"
    exit 0
fi
[ -d "$CORE_SRC/src" ] || { echo "core checkout not found: $CORE_SRC" >&2; exit 2; }

# Concurrency guard: two setups must not run at once.
LOCK="$K7/.setup_scratch.lock"
if ! mkdir "$LOCK" 2>/dev/null; then
    echo "another setup_scratch.sh is running (lock $LOCK); waiting..." >&2
    for _ in $(seq 1 300); do [ -d "$LOCK" ] || break; sleep 1; done
    [ -e "$SCRATCH/.qpool_scratch_ok" ] && exit 0
    echo "gave up waiting for the other setup" >&2; exit 2
fi
trap 'rmdir "$LOCK" 2>/dev/null || true' EXIT

# Refuse to delete anything that is not clearly our own scratch directory.
case "$SCRATCH" in
    */core-scratch|*/core-scratch/) ;;
    *) echo "refusing to use scratch dir not named core-scratch: $SCRATCH" >&2; exit 2 ;;
esac
rm -rf "$SCRATCH.tmp"
mkdir -p "$SCRATCH.tmp"
rsync -a --exclude .git --exclude build --exclude '*.zip' --exclude '*.png' --exclude '*.svg' \
      --exclude benchmark_uefi --exclude tools --exclude 'test/data' \
      "$CORE_SRC/" "$SCRATCH.tmp/"

python3 -I "$TOOLS/patch_core.py" "$SCRATCH.tmp"

# Wire QPOOL (struct QPOOL / QPOOL2) into the scratch contract_def.h if the checkout does not already.
DEF="$SCRATCH.tmp/src/contract_core/contract_def.h"
if ! grep -q 'contracts/QPool.h' "$DEF"; then
    python3 -I - "$DEF" <<'PYEOF'
import re, sys
p = sys.argv[1]
s = open(p).read()
idx = max(int(m) for m in re.findall(r'#define \w+_CONTRACT_INDEX (\d+)', s))
idx += 1
block = ("#undef CONTRACT_INDEX\n#undef CONTRACT_STATE_TYPE\n#undef CONTRACT_STATE2_TYPE\n\n"
         "#define QPOOL_CONTRACT_INDEX %d\n#define CONTRACT_INDEX QPOOL_CONTRACT_INDEX\n"
         "#define CONTRACT_STATE_TYPE QPOOL\n#define CONTRACT_STATE2_TYPE QPOOL2\n"
         "#include \"contracts/QPool.h\"\n\n" % idx)
s = s.replace("// new contracts should be added above this line\n\n#ifdef INCLUDE_CONTRACT_TEST_EXAMPLES",
              block + "// new contracts should be added above this line\n\n#ifdef INCLUDE_CONTRACT_TEST_EXAMPLES", 1)
s = s.replace("    // new contracts should be added above this line\n#ifdef INCLUDE_CONTRACT_TEST_EXAMPLES\n    {\"TESTEXA\"",
              "    {\"QPOOL\", 236, 10000, sizeof(QPOOL::StateData)},\n    // new contracts should be added above this line\n#ifdef INCLUDE_CONTRACT_TEST_EXAMPLES\n    {\"TESTEXA\"", 1)
s = s.replace("    // new contracts should be added above this line\n#ifdef INCLUDE_CONTRACT_TEST_EXAMPLES\n    REGISTER_CONTRACT_FUNCTIONS_AND_PROCEDURES(TESTEXA);",
              "    REGISTER_CONTRACT_FUNCTIONS_AND_PROCEDURES(QPOOL);\n    // new contracts should be added above this line\n#ifdef INCLUDE_CONTRACT_TEST_EXAMPLES\n    REGISTER_CONTRACT_FUNCTIONS_AND_PROCEDURES(TESTEXA);", 1)
open(p, "w").write(s)
print("wired QPOOL at contract index %d" % idx)
PYEOF
fi
grep -q 'REGISTER_CONTRACT_FUNCTIONS_AND_PROCEDURES(QPOOL)' "$DEF" || { echo "QPOOL wiring incomplete in $DEF" >&2; exit 2; }

# A placeholder contract header is only needed so the tree is self-consistent; check_contract.sh always overlays its own.
date -u +%Y-%m-%dT%H:%M:%SZ > "$SCRATCH.tmp/.qpool_scratch_ok"
rm -rf "$SCRATCH"
mv "$SCRATCH.tmp" "$SCRATCH"
echo "scratch core ready: $SCRATCH ($(du -sh "$SCRATCH" | cut -f1))"
