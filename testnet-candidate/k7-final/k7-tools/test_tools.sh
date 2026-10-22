#!/usr/bin/env bash
# test_tools.sh: checks release_gate.py and make_devkit.py. Prints PASS or FAIL per check, a summary, and exits
# nonzero if any check failed. Writes only under ../work/test_run (a scratch directory it recreates each run).
#
#   bash test_tools.sh
#
# The inputs are fixed paths relative to this file: ../base/base_K7.h (production), ../../review/QPoolK6.h (the
# devkit file the owner ran), ../../review/candidate_QPool.h (the earlier candidate) and
# ../base/base_K7.h.devkit_issuer_ids.txt (the devkit issuer letters).
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
K7="$(cd "$HERE/.." && pwd)"
SCRATCH="$(cd "$K7/.." && pwd)"
BASE="$K7/base/base_K7.h"
K6="$SCRATCH/review/QPoolK6.h"
CAND="$SCRATCH/review/candidate_QPool.h"
IDS="$K7/base/base_K7.h.devkit_issuer_ids.txt"
W="$K7/work/test_run"
GATE=(python3 -I "$HERE/release_gate.py")
MK=(python3 -I "$HERE/make_devkit.py")
HLP=(python3 -I "$HERE/test_helper.py")

for f in "$BASE" "$K6" "$CAND" "$IDS" "$HERE/release_gate.py" "$HERE/make_devkit.py" "$HERE/test_helper.py"; do
  [ -f "$f" ] || { echo "FAIL  missing input file: $f"; exit 2; }
done
case "$W" in */k7/work/test_run) ;; *) echo "refusing to use work dir $W"; exit 2;; esac
rm -rf "$W" && mkdir -p "$W" || exit 2

PASS=0
FAILN=0
pass() { printf 'PASS  %s\n' "$1"; PASS=$((PASS + 1)); }
fail() { printf 'FAIL  %s\n' "$1"; [ -n "${2:-}" ] && printf '        %s\n' "$2"; FAILN=$((FAILN + 1)); return 0; }
sha() { shasum -a 256 "$1" | cut -d' ' -f1; }

BASE_SHA_BEFORE="$(sha "$BASE")"
K6_SHA_BEFORE="$(sha "$K6")"

echo "== release_gate.py =="
"${GATE[@]}" "$BASE" > "$W/gate_base.out" 2>&1; rc=$?
if [ $rc -eq 0 ] && grep -q '^RELEASE GATE: PASS' "$W/gate_base.out"; then pass "gate PASSES base_K7.h (exit 0)"; else fail "gate PASSES base_K7.h" "exit $rc; see $W/gate_base.out"; fi

"${GATE[@]}" "$K6" > "$W/gate_k6.out" 2>&1; rc=$?
if [ $rc -eq 1 ] && grep -q '^RELEASE GATE: FAIL' "$W/gate_k6.out"; then pass "gate FAILS QPoolK6.h (exit 1)"; else fail "gate FAILS QPoolK6.h" "exit $rc; see $W/gate_k6.out"; fi

missing=""
for pat in '\[forbidden-text\] TESTNET ONLY' '\[forbidden-text\] TEST-ONLY' '\[forbidden-text\] QPOOLK6_' '\[forbidden-text\] QPoolK6' \
           '\[ticks-per-hour\]' '\[qpayhub-index\]' '\[qpay-issuer\] ID(...) spells the devkit test wallet' '\[qpay-issuer\] QPAY issuer row' \
           '\[struct-names\] expected exactly one .struct QPOOL2' '\[struct-names\] expected exactly one .struct QPOOL : public'; do
  grep -q "$pat" "$W/gate_k6.out" || missing="$missing | $pat"
done
if [ -z "$missing" ]; then pass "gate names every K6 defect with file:line (text markers, ticks, QPAYHUB index, issuer, struct names)"; else fail "gate output for QPoolK6.h lacks:" "$missing"; fi
if grep -Eq 'QPoolK6\.h:[0-9]+: FAIL \[ticks-per-hour\]' "$W/gate_k6.out" && grep -Eq 'QPoolK6\.h:[0-9]+: FAIL \[qpayhub-index\]' "$W/gate_k6.out" \
   && grep -Eq 'QPoolK6\.h:[0-9]+: FAIL \[qpay-issuer\]' "$W/gate_k6.out"; then pass "gate problem lines carry a file:line for the three constants"; else fail "gate problem lines carry file:line"; fi

"${GATE[@]}" "$CAND" > "$W/gate_candidate.out" 2>&1; rc=$?
if [ $rc -eq 0 ] && grep -q '^RELEASE GATE: PASS' "$W/gate_candidate.out"; then pass "gate PASSES candidate_QPool.h (production-valued; exit 0)"; else fail "gate PASSES candidate_QPool.h" "exit $rc; see $W/gate_candidate.out"; fi

"${GATE[@]}" "$W/does_not_exist.h" > "$W/gate_missing.out" 2>&1; rc=$?
if [ $rc -eq 2 ]; then pass "gate exits 2 on an unreadable file"; else fail "gate exits 2 on an unreadable file" "exit $rc"; fi

grep -q 'QPOOL_QX_CONTRACT_INDEX *= 1 ' "$W/gate_base.out" && grep -q 'QPOOL_QSWAP_CONTRACT_INDEX *= 13 ' "$W/gate_base.out" \
  && grep -q 'QPOOL_CREATE_POOL_FEE *= 10,000,000' "$W/gate_base.out" && grep -q 'QPOOL_VOTE_WINDOW *= 259,200' "$W/gate_base.out" \
  && grep -q "decodes to the ASCII name 'QPAY'" "$W/gate_base.out" && grep -q '29=QPAYHUB' "$W/gate_base.out" \
  && grep -q '^-- Embedded contract-name table' "$W/gate_base.out" \
  && pass "gate prints the proposal-day checklist (QX, QSWAP, fees, windows, QPAY name, name table)" \
  || fail "gate prints the proposal-day checklist" "see $W/gate_base.out"

echo "== release_gate.py negative controls (each mutated copy of base_K7.h must fail for the stated reason) =="
while read -r name rule desc; do
  out="$W/mut_$name.h"
  if ! "${HLP[@]}" mutate "$name" "$BASE" "$out" 2> "$W/mut_$name.err"; then fail "mutation $name did not apply" "$(cat "$W/mut_$name.err")"; continue; fi
  "${GATE[@]}" "$out" > "$W/mut_$name.out" 2>&1; rc=$?
  if [ "${rule#warn:}" != "$rule" ]; then
    w="${rule#warn:}"
    if [ $rc -eq 0 ] && grep -q "WARN $w" "$W/mut_$name.out"; then pass "gate PASSES with a WARN on $w (information only): $desc"; else fail "gate PASSES with a WARN on $w: $desc" "exit $rc; see $W/mut_$name.out"; fi
  elif [ "$rule" = "ok" ]; then
    if [ $rc -eq 0 ]; then pass "gate still PASSES: $desc"; else fail "gate still PASSES: $desc" "exit $rc; see $W/mut_$name.out"; fi
  else
    if [ $rc -eq 1 ] && grep -q "FAIL \[$rule\]" "$W/mut_$name.out"; then pass "gate FAILS [$rule]: $desc"; else fail "gate FAILS [$rule]: $desc" "exit $rc; see $W/mut_$name.out"; fi
  fi
done < <("${HLP[@]}" list-mutations)

echo "== make_devkit.py =="
"${MK[@]}" "$BASE" "$W/k6_legacy.h" --tag K6 --k6-legacy-comment > "$W/mk_k6_legacy.out" 2>&1; rc=$?
if [ $rc -eq 0 ] && cmp -s "$W/k6_legacy.h" "$K6"; then
  pass "FIDELITY: make_devkit(base_K7.h, --tag K6 --k6-legacy-comment) is BYTE-IDENTICAL to QPoolK6.h (sha256 $(sha "$K6"))"
else
  fail "FIDELITY: byte-identical to QPoolK6.h with --k6-legacy-comment" "exit $rc; $(cmp "$W/k6_legacy.h" "$K6" 2>&1 | head -1)"
fi

"${MK[@]}" "$BASE" "$W/k6_default.h" --tag K6 > "$W/mk_k6_default.out" 2>&1; rc=$?
if [ $rc -ne 0 ]; then
  fail "default make_devkit(base_K7.h, --tag K6) runs" "exit $rc"
elif cmp -s "$W/k6_default.h" "$K6"; then
  pass "default make_devkit(base_K7.h, --tag K6) is byte-identical to QPoolK6.h (no difference at all)"
else
  if "${HLP[@]}" known-hunk "$W/k6_default.h" "$K6" > "$W/known_hunk.out" 2>&1; then
    pass "default output (no flag) is NOT byte-identical to QPoolK6.h: it differs in exactly one comment paragraph (3 lines vs 2), nothing else"
    sed 's/^/        /' "$W/known_hunk.out"
  else
    fail "default output differs from QPoolK6.h in more than the known comment paragraph" "see $W/known_hunk.out"
  fi
fi

"${MK[@]}" "$BASE" "$W/k6_legacy_again.h" --tag K6 --k6-legacy-comment > /dev/null 2>&1
"${MK[@]}" "$BASE" "$W/k6_default_again.h" --tag K6 > /dev/null 2>&1
if cmp -s "$W/k6_legacy.h" "$W/k6_legacy_again.h" && cmp -s "$W/k6_default.h" "$W/k6_default_again.h"; then pass "deterministic: two runs give identical bytes (both modes)"; else fail "deterministic: two runs give identical bytes"; fi

"${MK[@]}" "$BASE" "$W/k7_default.h" --tag K7 > "$W/mk_k7.out" 2>&1; rc=$?
"${MK[@]}" "$BASE" "$W/k7_legacy.h" --tag K7 --k6-legacy-comment > /dev/null 2>&1
if [ $rc -eq 0 ] && sed -e 's/QPOOLK6/QPOOLK7/g' "$K6" | cmp -s - "$W/k7_legacy.h"; then
  pass "the tag is the only thing that differs: QPoolK6.h with QPOOLK6 -> QPOOLK7 equals the K7-tag output (legacy comment mode)"
else
  fail "the tag is the only thing that differs between K6 and K7 outputs" "exit $rc"
fi

if "${HLP[@]}" three-edits "$BASE" "$W/k7_default.h" K7 > "$W/three_edits.out" 2>&1; then
  pass "exactly three edit sites: undoing the rename in the K7 output leaves only the ticks, QPAYHUB and QPAY issuer edits against base_K7.h"
  sed 's/^/        /' "$W/three_edits.out"
else
  fail "exactly three edit sites differ from base_K7.h" "see $W/three_edits.out"
fi

if "${HLP[@]}" issuer-ids "$IDS" "$W/k6_legacy.h" > "$W/issuer_ids.out" 2>&1; then pass "devkit issuer letters in the output equal base_K7.h.devkit_issuer_ids.txt (56 letters)"; else fail "devkit issuer letters equal the txt file" "see $W/issuer_ids.out"; fi

"${GATE[@]}" "$W/k7_default.h" > "$W/gate_k7_devkit.out" 2>&1; rc=$?
if [ $rc -eq 1 ] && grep -q 'FAIL \[forbidden-text\] QPOOLK7_' "$W/gate_k7_devkit.out" && grep -q 'FAIL \[forbidden-text\] TESTNET ONLY' "$W/gate_k7_devkit.out" \
   && grep -q 'FAIL \[qpay-issuer\] ID(...) spells the devkit test wallet' "$W/gate_k7_devkit.out"; then
  pass "gate FAILS a freshly derived K7 devkit header (tag, markers and test wallet all reported)"
else
  fail "gate FAILS a freshly derived K7 devkit header" "exit $rc; see $W/gate_k7_devkit.out"
fi

"${GATE[@]}" "$W/k6_default.h" > /dev/null 2>&1; rc=$?
[ $rc -eq 1 ] && pass "gate FAILS the regenerated K6 devkit header" || fail "gate FAILS the regenerated K6 devkit header" "exit $rc"

echo "== make_devkit.py refusals (nothing may be written) =="
refuse() { # name expected-rc description command...
  local name="$1" want="$2" desc="$3"; shift 3
  "$@" > "$W/refuse_$name.out" 2>&1; local rc=$?
  if [ $rc -eq "$want" ]; then pass "refuses: $desc (exit $rc)"; else fail "refuses: $desc" "exit $rc, wanted $want; $(head -c 200 "$W/refuse_$name.out")"; fi
}
refuse already_devkit 1 "an input that is already a devkit header" "${MK[@]}" "$K6" "$W/r1.h" --tag K7
[ ! -e "$W/r1.h" ] && pass "no output file written for the devkit input" || fail "no output file written for the devkit input"
"${HLP[@]}" mutate ticks_3601 "$BASE" "$W/odd_ticks.h"
refuse odd_ticks 1 "a header whose TICKS line is not the audited text" "${MK[@]}" "$W/odd_ticks.h" "$W/r2.h"
[ ! -e "$W/r2.h" ] && pass "no output file written for the changed-TICKS input" || fail "no output file written for the changed-TICKS input"
"${HLP[@]}" mutate issuer_one_letter "$BASE" "$W/odd_issuer.h"
refuse odd_issuer 1 "a header whose QPAY issuer is not the production one" "${MK[@]}" "$W/odd_issuer.h" "$W/r3.h"
printf 'keep me\n' > "$W/existing_out.h"
refuse keeps_existing 1 "a bad input leaves an existing output file alone" "${MK[@]}" "$K6" "$W/existing_out.h"
[ "$(cat "$W/existing_out.h")" = "keep me" ] && pass "the existing output file was not touched" || fail "the existing output file was not touched"
cp "$BASE" "$W/same.h"
refuse same_path 2 "output path equal to the input path" "${MK[@]}" "$W/same.h" "$W/same.h"
[ "$(sha "$W/same.h")" = "$BASE_SHA_BEFORE" ] && pass "the input was not modified by the same-path refusal" || fail "the input was not modified by the same-path refusal"
refuse bad_tag_lower 2 "a lower-case tag" "${MK[@]}" "$BASE" "$W/r4.h" --tag k6
refuse bad_tag_digit 2 "a tag starting with a digit" "${MK[@]}" "$BASE" "$W/r5.h" --tag 6K
refuse missing_in 2 "a missing input file" "${MK[@]}" "$W/nope.h" "$W/r6.h"
refuse usage 2 "no arguments" "${MK[@]}"

echo "== inputs untouched =="
[ "$(sha "$BASE")" = "$BASE_SHA_BEFORE" ] && pass "base_K7.h is byte-for-byte unchanged by the run (sha256 $BASE_SHA_BEFORE)" || fail "base_K7.h unchanged"
[ "$(sha "$K6")" = "$K6_SHA_BEFORE" ] && pass "QPoolK6.h is byte-for-byte unchanged by the run" || fail "QPoolK6.h unchanged"

echo
echo "SUMMARY: $PASS passed, $FAILN failed"
if [ "$FAILN" -eq 0 ]; then echo "ALL CHECKS PASS"; exit 0; else echo "SOME CHECKS FAILED"; exit 1; fi
