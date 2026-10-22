#!/bin/bash
# selftest.sh [base-header] -- proves the two tools can FAIL, not only pass.
# Builds fresh broken copies of the base header (make_negative.py) in a temp dir, runs lint_contract.py and
# check_contract.sh on the base and on every broken copy, and compares each exit status with the expected one.
# Exit 0 only if every expectation holds. Takes about a minute (three smoke runs are serialised by a lock).
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS="$(cd "$HERE/.." && pwd)"
K7="$(cd "$TOOLS/.." && pwd)"
BASE="${1:-$K7/base/base_K7.h}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/qpool-selftest.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
python3 -I "$HERE/make_negative.py" "$BASE" "$TMP" > /dev/null || { echo "could not build negative copies"; exit 2; }

fails=0
expect() {  # expect LABEL EXPECTED_EXIT cmd...
    local label="$1" want="$2"; shift 2
    "$@" > "$TMP/out.txt" 2>&1
    local got=$?
    if [ "$got" = "$want" ]; then printf 'ok    %-52s exit %s (expected %s)\n' "$label" "$got" "$want"
    else printf 'WRONG %-52s exit %s (expected %s)\n' "$label" "$got" "$want"; sed 's/^/        /' "$TMP/out.txt" | head -15; fails=$((fails + 1)); fi
}

expect "lint   base"                         0 python3 -I "$TOOLS/lint_contract.py" "$BASE"
expect "lint   forbidden constructs"         1 python3 -I "$TOOLS/lint_contract.py" "$TMP/base_lint_violations.h"
expect "lint   type error copy (lint-clean)" 0 python3 -I "$TOOLS/lint_contract.py" "$TMP/base_type_error.h"
expect "check  base"                         0 "$TOOLS/check_contract.sh" "$BASE"
expect "check  one type error"               1 "$TOOLS/check_contract.sh" --compile-only "$TMP/base_type_error.h"
expect "check  template static_assert error" 1 "$TOOLS/check_contract.sh" --compile-only "$TMP/base_template_error.h"
expect "check  compiles but crashes at run"  1 "$TOOLS/check_contract.sh" "$TMP/base_smoke_crash.h"
expect "check  compile-only on crash copy"   0 "$TOOLS/check_contract.sh" --compile-only "$TMP/base_smoke_crash.h"
if [ "$fails" = 0 ]; then echo "SELFTEST PASS"; exit 0; fi
echo "SELFTEST FAIL ($fails)"; exit 1
