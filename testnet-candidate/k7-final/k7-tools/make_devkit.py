#!/usr/bin/env python3
"""make_devkit.py <production header> <output header> [--tag K6] [--k6-legacy-comment]

Derive, deterministically, the devkit-wired variant of the QPOOL header that the owner runs on the testnet devkit.
The production header is never changed; the output is a separate file. Run it as

    python3 -I make_devkit.py base_K7.h QPoolK7.h --tag K7

It makes exactly four edits to the production text and nothing else:
  1. QPOOL_TICKS_PER_HOUR   3600 -> 2, with the "TESTNET ONLY" comment (windows become minutes);
  2. QPOOL_QPAYHUB_CONTRACT_INDEX   29 -> 50, with the "TEST-ONLY" explanation block (QPayhubStub wired at 50);
  3. the QPAY issuer identity in BuyAndBurnQpay -> the devkit test wallet (RDOIGJSE...), with its "TEST-ONLY" block;
  4. the rename: QPOOL_ -> QPOOL<tag>_ everywhere (code and comments), struct QPOOL -> QPOOL<tag>,
     struct QPOOL2 -> QPOOL<tag>2. Prose uses of the word QPOOL (not followed by _) stay as they are.
Each edit must find exactly the production text it expects, exactly once; otherwise nothing is written and the exit
code is 1 (a production header that no longer looks like the audited one is a reason to stop, not to guess). The
output is then re-read and checked (values set, no QPOOL_ left, no bare QPOOL in code, test wallet present).

The default tag is K6, the one the owner's devkit already runs. The comment texts are fixed (not derived from the
tag): "tools-bin/qpoolk6_qpay_issuer.json" and "CHANGES-SINCE-K6.md" name files that exist in the owner's devkit under
those names.

--k6-legacy-comment is for the fidelity test only. QPoolK6.h carries an older, wrong wording of the "NO minimum
payout" comment (845 QU and 80 percent; the 40/30/20/10 split makes it 1,690 QU and 40 percent, which is what the
production header says). By default that corrected paragraph is kept. With the flag it is replaced by the old
wording, so that the output of the K6 derivation is byte-identical to QPoolK6.h. Do not use the flag for a new build.

Only the Python standard library is used. Output is written to a temporary file next to the target and renamed.
"""
import hashlib
import os
import re
import sys

# ---- the production text this tool expects (and the devkit text it puts in its place) -------------------------

PROD_ISSUER_ROWS = ("QPAYNOWS", "WZMGHFEA", "EVJXGZAV", "SHABAZDD", "BDIHTEBO", "PCOGHRGB", "CYCUZOHC")
TEST_ISSUER_ROWS = ("RDOIGJSE", "SACHNDUK", "OBMYSPDD", "WWRDKXDP", "JQIKHJDV", "ZGDEOZGV", "LOGOBWHC")

TICKS_RX = re.compile(r"^constexpr uint64 QPOOL_TICKS_PER_HOUR( +)= 3600;\n", re.M)
TICKS_NEW = "constexpr uint64 QPOOL_TICKS_PER_HOUR{sp}= 2;   // TESTNET ONLY: shortens every vote and execute window\n"

QPAYHUB_RX = re.compile(r"^constexpr uint16 QPOOL_QPAYHUB_CONTRACT_INDEX = 29;[^\n]*\n", re.M)
QPAYHUB_NEW = (
    "// TEST-ONLY devkit substitution: the upstream testnet-candidate build hardcodes 29,\n"
    "// matching real QPAYHUB's assumed position on the mainnet index table it was designed\n"
    "// against. Index 29 in THIS devkit is QTREAT, and QPAYHUB doesn't exist here at all, so\n"
    "// this points at contracts/QPayhubStub.h instead (wired at index 50) -- exactly the\n"
    "// fallback CHANGES-SINCE-K6.md itself names: \"change QPOOL_QPAYHUB_CONTRACT_INDEX to a\n"
    "// test contract that books plain transfers.\" The upstream testnet-candidate branch is\n"
    "// untouched; this substitution exists only in this devkit-wired copy.\n"
    "constexpr uint16 QPOOL_QPAYHUB_CONTRACT_INDEX = 50;\n"
)

ISSUER_COMMENT = (
    "        // TEST-ONLY devkit substitution: the real QPAY asset (issuer\n"
    "        // QPAYNOWSWZMGHFEAEVJXGZAVSHABAZDDBDIHTEBOPCOGHRGBCYCUZOHCVLXG) is a mainnet\n"
    "        // token this devnet cannot issue or trade against. Substituted with a devnet\n"
    "        // test token issued under the same name (QPOOL_QPAY_ASSET_NAME is unchanged) by\n"
    "        // a TEST-ONLY wallet this live test controls (tools-bin/qpoolk6_qpay_issuer.json)\n"
    "        // -- no private key needed since we issue it ourselves, unlike the real asset.\n"
)

# The corrected paragraph the production header carries above the QPAYHUB constant, and the older wording QPoolK6.h has.
PARA_PROD = (
    "// There is NO minimum payout: END_EPOCH splits whatever revenue exists. Revenue under 1,690 QU in an epoch pays no shareholder (its 40\n"
    "// percent divides to less than 1 QU per share); that part falls into the burn with the other leftovers. The 30 and 20 percent legs\n"
    "// are paid in full whatever the amount.\n"
)
PARA_K6_LEGACY = (
    "// There is NO minimum payout: END_EPOCH splits whatever revenue exists. Anything under 845 QU pays no shareholder (80 percent of it\n"
    "// divides to less than 1 QU per share) and is burned in full.\n"
)


def issuer_block(rows):
    """The nine lines of 'locals.asset.issuer = ID( ... );' for the given eight-letter rows."""
    body = ""
    for i, row in enumerate(rows):
        letters = ", ".join("_" + c for c in row)
        body += "            " + letters + ("," if i < len(rows) - 1 else "") + "\n"
    return "        locals.asset.issuer = ID(\n" + body + "        );\n"


class Stop(Exception):
    pass


def replace_once(text, old, new, what):
    n = text.count(old)
    if n != 1:
        raise Stop("%s: expected exactly 1 occurrence of the production text, found %d" % (what, n))
    return text.replace(old, new)


def sub_once(text, rx, repl, what):
    found = rx.findall(text)
    if len(found) != 1:
        raise Stop("%s: expected exactly 1 match of %s, found %d" % (what, rx.pattern[:70].replace("\n", "\\n"), len(found)))
    return rx.sub(repl, text, count=1)


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


def derive(prod, tag, legacy_comment, log):
    if re.search(r"QPOOLK[0-9]|TESTNET[ \t-]+ONLY|TEST-ONLY|RDOIGJSE", prod, re.I):
        raise Stop("the input already carries a devkit tag or devkit marker: it is not a production header")
    if "\r" in prod:
        raise Stop("the input has CR characters; this tool expects LF line endings and will not guess")
    t = prod

    if legacy_comment:
        t = replace_once(t, PARA_PROD, PARA_K6_LEGACY, "legacy comment")
        log.append("kept-wording switch: 'NO minimum payout' paragraph replaced by the older QPoolK6.h wording (fidelity test only)")

    m = TICKS_RX.search(prod)
    t = sub_once(t, TICKS_RX, lambda mm: TICKS_NEW.format(sp=mm.group(1)), "QPOOL_TICKS_PER_HOUR")
    log.append("QPOOL_TICKS_PER_HOUR 3600 -> 2 (input line %d)" % line_of(prod, m.start()))

    m = QPAYHUB_RX.search(prod)
    t = sub_once(t, QPAYHUB_RX, lambda mm: QPAYHUB_NEW, "QPOOL_QPAYHUB_CONTRACT_INDEX")
    log.append("QPOOL_QPAYHUB_CONTRACT_INDEX 29 -> 50 (input line %d)" % line_of(prod, m.start()))

    old_block = issuer_block(PROD_ISSUER_ROWS)
    pos = prod.find(old_block)
    t = replace_once(t, old_block, ISSUER_COMMENT + issuer_block(TEST_ISSUER_ROWS), "QPAY issuer in BuyAndBurnQpay")
    log.append("QPAY issuer QPAYNOWS... -> RDOIGJSE... (input line %d)" % line_of(prod, pos))

    t, n = re.subn(r"\bQPOOL_", "QPOOL%s_" % tag, t)
    log.append("renamed %d occurrence(s) of QPOOL_ to QPOOL%s_ (identifiers and comments)" % (n, tag))
    t = sub_once(t, re.compile(r"\bstruct QPOOL2\b"), "struct QPOOL%s2" % tag, "struct QPOOL2")
    t = sub_once(t, re.compile(r"\bstruct QPOOL\b(?=\s*:\s*public\s+ContractBase)"), "struct QPOOL%s" % tag, "struct QPOOL")
    log.append("struct QPOOL -> QPOOL%s, struct QPOOL2 -> QPOOL%s2" % (tag, tag))
    return t


def verify(out, tag):
    """Re-read the derived text; every expectation is checked on what will actually be written."""
    bad = []

    def need(cond, msg):
        if not cond:
            bad.append(msg)

    need(re.search(r"^constexpr uint64 QPOOL%s_TICKS_PER_HOUR +=" % tag, out, re.M) is not None
         and re.search(r"QPOOL%s_TICKS_PER_HOUR +=\s*2;" % tag, out) is not None, "TICKS_PER_HOUR is not 2")
    need(re.search(r"^constexpr uint16 QPOOL%s_QPAYHUB_CONTRACT_INDEX = 50;$" % tag, out, re.M) is not None, "QPAYHUB index is not 50")
    need(issuer_block(TEST_ISSUER_ROWS) in out, "the test wallet block is missing")
    need(issuer_block(PROD_ISSUER_ROWS) not in out, "the production issuer block is still present")
    need(re.search(r"\bQPOOL_", out) is None, "an untagged QPOOL_ identifier is left")
    need(len(re.findall(r"^struct QPOOL%s2 \{\};$" % tag, out, re.M)) == 1, "struct QPOOL%s2 is not exactly once" % tag)
    need(len(re.findall(r"^struct QPOOL%s : public ContractBase$" % tag, out, re.M)) == 1, "struct QPOOL%s is not exactly once" % tag)
    for i, ln in enumerate(out.split("\n")):
        code = ln.split("//", 1)[0]
        if re.search(r"\bQPOOL2?\b", code):
            bad.append("line %d: a bare QPOOL/QPOOL2 in code would be left untagged: %s" % (i + 1, ln.strip()[:80]))
    return bad


def main(argv):
    args = argv[1:]
    tag, legacy, pos = "K6", False, []
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--tag" and i + 1 < len(args):
            tag = args[i + 1]
            i += 2
        elif a.startswith("--tag="):
            tag = a[len("--tag="):]
            i += 1
        elif a == "--k6-legacy-comment":
            legacy = True
            i += 1
        elif a in ("-h", "--help"):
            print(__doc__)
            return 0
        else:
            pos.append(a)
            i += 1
    if len(pos) != 2 or not re.fullmatch(r"[A-Z][A-Z0-9]{0,7}", tag):
        print("usage: python3 -I make_devkit.py <production header> <output header> [--tag K6] [--k6-legacy-comment]\n"
              "       (the tag is 1 to 8 characters, A-Z and 0-9, starting with a letter)", file=sys.stderr)
        return 2
    src, dst = pos
    try:
        if os.path.exists(dst) and os.path.samefile(src, dst):
            print("make_devkit: refusing to overwrite the production header %s" % src, file=sys.stderr)
            return 2
        with open(src, "rb") as fh:
            prod = fh.read().decode("utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        print("make_devkit: cannot read %s: %s" % (src, exc), file=sys.stderr)
        return 2
    log = []
    try:
        out = derive(prod, tag, legacy, log)
        bad = verify(out, tag)
        if bad:
            raise Stop("self-check of the derived text failed: " + "; ".join(bad))
    except Stop as exc:
        print("make_devkit: STOP, nothing written: %s" % exc, file=sys.stderr)
        return 1
    tmp = dst + ".tmp"
    try:
        with open(tmp, "w", encoding="utf-8", newline="") as fh:
            fh.write(out)
        os.replace(tmp, dst)
    except OSError as exc:
        print("make_devkit: cannot write %s: %s" % (dst, exc), file=sys.stderr)
        return 2
    print("make_devkit: tag %s: %s -> %s" % (tag, src, dst))
    for line in log:
        print("  " + line)
    print("  input  sha256 %s (%d bytes)" % (hashlib.sha256(prod.encode("utf-8")).hexdigest(), len(prod.encode("utf-8"))))
    print("  output sha256 %s (%d bytes)" % (hashlib.sha256(out.encode("utf-8")).hexdigest(), len(out.encode("utf-8"))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
