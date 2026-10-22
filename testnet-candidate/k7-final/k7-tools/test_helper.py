#!/usr/bin/env python3
"""test_helper.py: small helpers for test_tools.sh (not a deliverable tool on its own). Run with python3 -I.

  test_helper.py list-mutations                    one line per negative control: <name> <expected gate rule or ok> <description>
  test_helper.py mutate <name> <in> <out>          write the mutated copy of a production header (exit 3 if the edit did not apply)
  test_helper.py known-hunk <make_devkit default output> <QPoolK6.h>
                                                   exit 0 only if the two files differ in exactly the one known comment paragraph
  test_helper.py three-edits <production> <devkit output> <tag>
                                                   undo the rename in the devkit output and check that, against the production
                                                   header, exactly the three expected hunks remain (ticks, QPAYHUB, QPAY issuer)
  test_helper.py issuer-ids <issuer ids txt> <devkit header>
                                                   exit 0 only if the txt file's letters are the ID(...) letters in the header
"""
import difflib
import re
import sys

TICKS = "constexpr uint64 QPOOL_TICKS_PER_HOUR   = 3600;\n"
HUB_RX = re.compile(r"^constexpr uint16 QPOOL_QPAYHUB_CONTRACT_INDEX = 29;[^\n]*\n", re.M)
ISSUER_ROW4 = "            _S, _H, _A, _B, _A, _Z, _D, _D,\n"
ISSUER_FIRST = "        locals.asset.issuer = ID(\n"
TEST_ROWS = ("RDOIGJSE", "SACHNDUK", "OBMYSPDD", "WWRDKXDP", "JQIKHJDV", "ZGDEOZGV", "LOGOBWHC")
PROD_ROWS = ("QPAYNOWS", "WZMGHFEA", "EVJXGZAV", "SHABAZDD", "BDIHTEBO", "PCOGHRGB", "CYCUZOHC")


def once(t, old, new):
    if t.count(old) != 1:
        raise SystemExit("mutation did not apply: %r occurs %d times" % (old[:60], t.count(old)))
    return t.replace(old, new)


def issuer_block(rows):
    body = ""
    for i, row in enumerate(rows):
        body += "            " + ", ".join("_" + c for c in row) + ("," if i < len(rows) - 1 else "") + "\n"
    return "        locals.asset.issuer = ID(\n" + body + "        );\n"


def hub_to(t, new_line):
    if len(HUB_RX.findall(t)) != 1:
        raise SystemExit("mutation did not apply: QPAYHUB line")
    return HUB_RX.sub(lambda m: new_line, t, count=1)


def m_ticks_3601(t):
    return once(t, TICKS, "constexpr uint64 QPOOL_TICKS_PER_HOUR   = 3601;\n")


def m_ticks_2_silent(t):
    return once(t, TICKS, "constexpr uint64 QPOOL_TICKS_PER_HOUR   = 2;\n")


def m_ticks_missing(t):
    return once(t, TICKS, "")


def m_ticks_twice(t):
    return once(t, TICKS, TICKS + "constexpr uint64 QPOOL_TICKS_PER_HOUR   = 2;\n")


def m_ticks_masked(t):
    return once(t, TICKS, "constexpr uint64 QPOOL_TICKS_PER_HOUR   = 2;\n// constexpr uint64 QPOOL_TICKS_PER_HOUR   = 3600;\n")


def m_hub_50_silent(t):
    return hub_to(t, "constexpr uint16 QPOOL_QPAYHUB_CONTRACT_INDEX = 50;\n")


def m_hub_28(t):
    return hub_to(t, "constexpr uint16 QPOOL_QPAYHUB_CONTRACT_INDEX = 28;\n")


def m_hub_twice(t):
    return hub_to(t, "constexpr uint16 QPOOL_QPAYHUB_CONTRACT_INDEX = 29;\nconstexpr uint16 QPOOL_QPAYHUB_CONTRACT_INDEX = 50;\n")


def m_issuer_one_letter(t):
    return once(t, ISSUER_ROW4, "            _S, _H, _A, _B, _A, _Z, _D, _E,\n")


def m_issuer_test_silent(t):
    return once(t, issuer_block(PROD_ROWS), issuer_block(TEST_ROWS))


def m_issuer_short(t):
    return once(t, "            _C, _Y, _C, _U, _Z, _O, _H, _C\n", "            _C, _Y, _C, _U, _Z, _O, _H\n")


def m_issuer_second_id(t):
    return t + "constexpr id QPOOL_SPARE_ID = ID(\n" + issuer_block(TEST_ROWS).split("\n", 1)[1].replace("        );", ");")


def m_text_testnet(t):
    return t + "// TESTNET ONLY: shortened windows\n"


def m_text_testonly(t):
    return t + "// test-only substitution\n"


def m_text_rdoigjse(t):
    return t + "// wallet RDOIGJSE\n"


def m_text_k7_ident(t):
    return t + "constexpr uint64 QPOOLK7_SPARE = 1;\n"


def m_text_k6_name(t):
    return t + "// built from qpoolk6\n"


def m_text_k9_tag(t):
    return t + "constexpr uint64 QPOOLK9_SPARE = 1;\n"


def m_struct_renamed(t):
    return once(t, "struct QPOOL : public ContractBase", "struct QPOOLX : public ContractBase")


def m_struct2_renamed(t):
    return once(t, "struct QPOOL2 {};", "struct QPOOL3 {};")


def m_window_short(t):
    return once(t, "constexpr uint64 QPOOL_VOTE_WINDOW      = 72 * QPOOL_TICKS_PER_HOUR; // 3 days",
                "constexpr uint64 QPOOL_VOTE_WINDOW      = 144; // 3 days")


def m_harmless(t):
    # a different comment and trailing whitespace on a comment line: must not trip the gate
    return once(t, "// QPOOL — vote-governed asset pooling.", "// QPOOL — vote-governed asset pooling. (harmless edit)  ")


MUTATIONS = (
    ("ticks_3601", "ticks-per-hour", "QPOOL_TICKS_PER_HOUR = 3601", m_ticks_3601),
    ("ticks_2_silent", "ticks-per-hour", "QPOOL_TICKS_PER_HOUR = 2 with no marker comment", m_ticks_2_silent),
    ("ticks_missing", "ticks-per-hour", "QPOOL_TICKS_PER_HOUR definition removed", m_ticks_missing),
    ("ticks_twice", "ticks-per-hour", "QPOOL_TICKS_PER_HOUR defined twice (3600 then 2)", m_ticks_twice),
    ("ticks_masked", "ticks-per-hour", "= 2 in code, a commented-out 3600 next to it", m_ticks_masked),
    ("hub_50_silent", "qpayhub-index", "QPAYHUB index 50 with no marker comment", m_hub_50_silent),
    ("hub_28", "qpayhub-index", "QPAYHUB index 28", m_hub_28),
    ("hub_twice", "qpayhub-index", "QPAYHUB index defined twice (29 then 50)", m_hub_twice),
    ("issuer_one_letter", "qpay-issuer", "one letter of the QPAY issuer changed", m_issuer_one_letter),
    ("issuer_test_silent", "qpay-issuer", "test wallet in BuyAndBurnQpay with no marker comment", m_issuer_test_silent),
    ("issuer_short", "qpay-issuer", "QPAY issuer one letter short", m_issuer_short),
    ("issuer_second_id", "qpay-issuer", "test wallet ID(...) added outside BuyAndBurnQpay", m_issuer_second_id),
    ("text_testnet", "forbidden-text", "a TESTNET ONLY comment added", m_text_testnet),
    ("text_testonly", "forbidden-text", "a lower-case test-only comment added", m_text_testonly),
    ("text_rdoigjse", "forbidden-text", "the text RDOIGJSE in a comment", m_text_rdoigjse),
    ("text_k7_ident", "forbidden-text", "a QPOOLK7_ identifier", m_text_k7_ident),
    ("text_k6_name", "forbidden-text", "lower-case qpoolk6 in a comment", m_text_k6_name),
    ("text_k9_tag", "forbidden-text", "a QPOOLK9_ identifier (a tag the spec list does not name)", m_text_k9_tag),
    ("struct_renamed", "struct-names", "struct QPOOL renamed QPOOLX", m_struct_renamed),
    ("struct2_renamed", "struct-names", "struct QPOOL2 renamed QPOOL3", m_struct2_renamed),
    ("window_short", "warn:QPOOL_VOTE_WINDOW", "QPOOL_VOTE_WINDOW = 144 with TICKS still 3600: gate PASSES but WARNs (information only)", m_window_short),
    ("harmless_edit", "ok", "positive control: a harmless comment edit must still PASS", m_harmless),
)


def rd(p):
    with open(p, "rb") as fh:
        return fh.read().decode("utf-8")


def wr(p, t):
    with open(p, "w", encoding="utf-8", newline="") as fh:
        fh.write(t)


def untag(t, tag):
    t = re.sub(r"\bQPOOL%s_" % tag, "QPOOL_", t)
    t = re.sub(r"\bstruct QPOOL%s2\b" % tag, "struct QPOOL2", t)
    return re.sub(r"\bstruct QPOOL%s\b" % tag, "struct QPOOL", t)


def hunks(a, b):
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    return [op for op in sm.get_opcodes() if op[0] != "equal"]


def main(argv):
    if len(argv) >= 2 and argv[1] == "list-mutations":
        for name, rule, desc, _ in MUTATIONS:
            print("%s %s %s" % (name, rule, desc))
        return 0
    if len(argv) == 5 and argv[1] == "mutate":
        for name, _, _, fn in MUTATIONS:
            if name == argv[2]:
                wr(argv[4], fn(rd(argv[3])))
                return 0
        print("unknown mutation " + argv[2], file=sys.stderr)
        return 3
    if len(argv) == 4 and argv[1] == "known-hunk":
        a, b = rd(argv[2]).split("\n"), rd(argv[3]).split("\n")
        ops = hunks(a, b)
        want_a = [
            "// There is NO minimum payout: END_EPOCH splits whatever revenue exists. Revenue under 1,690 QU in an epoch pays no shareholder (its 40",
            "// percent divides to less than 1 QU per share); that part falls into the burn with the other leftovers. The 30 and 20 percent legs",
            "// are paid in full whatever the amount.",
        ]
        want_b = [
            "// There is NO minimum payout: END_EPOCH splits whatever revenue exists. Anything under 845 QU pays no shareholder (80 percent of it",
            "// divides to less than 1 QU per share) and is burned in full.",
        ]
        print("hunks: %d" % len(ops))
        for tag, i1, i2, j1, j2 in ops:
            print("  %s lines %d-%d (derived) vs %d-%d (QPoolK6.h)" % (tag, i1 + 1, i2, j1 + 1, j2))
        if len(ops) == 1 and ops[0][0] == "replace" and a[ops[0][1]:ops[0][2]] == want_a and b[ops[0][3]:ops[0][4]] == want_b:
            print("the only difference is the 'NO minimum payout' comment: corrected wording (1,690 QU, 40 percent) vs older wording (845 QU, 80 percent)")
            return 0
        return 1
    if len(argv) == 5 and argv[1] == "three-edits":
        prod, dev, tag = rd(argv[2]).split("\n"), untag(rd(argv[3]), argv[4]).split("\n"), argv[4]
        ops = hunks(prod, dev)
        t0 = prod.index(TICKS.rstrip("\n"))
        h0 = [i for i, ln in enumerate(prod) if ln.startswith("constexpr uint16 QPOOL_QPAYHUB_CONTRACT_INDEX = 29;")]
        s0 = [i for i, ln in enumerate(prod) if ln == ISSUER_FIRST.rstrip("\n")]
        if len(h0) != 1 or len(s0) != 1:
            print("cannot locate the regions in the production header")
            return 1
        regions = (("ticks", t0, t0 + 1), ("qpayhub", h0[0], h0[0] + 1), ("issuer", s0[0], s0[0] + 9))
        touched = set()
        stray = 0
        print("hunks after undoing the rename: %d" % len(ops))
        for t, i1, i2, j1, j2 in ops:
            home = [n for n, a, b in regions if a <= i1 and i2 <= b]
            print("  %-7s production lines %d-%d -> %d line(s): %s" % (t, i1 + 1, i2, j2 - j1, home[0] if home else "OUTSIDE THE THREE EDIT SITES"))
            if home:
                touched.add(home[0])
            else:
                stray += 1
        print("edit sites touched: %s; hunks outside the three edit sites: %d" % (", ".join(sorted(touched)), stray))
        return 0 if (stray == 0 and touched == {"ticks", "qpayhub", "issuer"}) else 1
    if len(argv) == 4 and argv[1] == "issuer-ids":
        want = "".join(re.findall(r"_([A-Z])", rd(argv[2])))
        t = rd(argv[3])
        m = re.search(r"locals\.asset\.issuer = ID\(([^)]*)\)", t)
        got = "".join(re.findall(r"_([A-Z])", m.group(1))) if m else ""
        print("txt letters : %s" % want)
        print("header ID() : %s" % got)
        return 0 if (len(want) == 56 and want == got) else 1
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
