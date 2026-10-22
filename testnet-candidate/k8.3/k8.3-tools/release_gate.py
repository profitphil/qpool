#!/usr/bin/env python3
"""release_gate.py <header>

Release gate for the QPOOL contract header. Exit 0 only if the header is a production build; exit 1 (and
one "file:line: FAIL ..." line per problem) otherwise; exit 2 on a usage or I/O error. Run it as

    python3 -I release_gate.py <header>

What it FAILS on (each is checked on its own, so one run lists every problem):
  1. any occurrence (case-insensitive, comments included) of: TESTNET ONLY, TEST-ONLY, QPOOLK6_, QPOOLK7_,
     RDOIGJSE, QPoolK6, QPoolK7, and (beyond the list) any other tag of the form QPOOLK<digits>;
  2. QPOOL_TICKS_PER_HOUR not defined exactly once, or not exactly 3600;
  3. QPOOL_QPAYHUB_CONTRACT_INDEX not defined exactly once, or not exactly 29;
  4. the QPAY issuer identity letters in BuyAndBurnQpay not exactly the production ones, or any ID(...) in the
     file that spells the devkit test wallet;
  5. the contract structs not named QPOOL and QPOOL2 (and any other QPOOL-prefixed code identifier that is not
     QPOOL, QPOOL2 or QPOOL_*, which is what a tag rename leaves behind).
Structural checks (2 to 5) look at CODE only: comments and string literals are blanked first, so a commented-out
"= 3600" cannot satisfy them and a commented-out devkit value cannot fail them. Check 1 looks at the raw text.

It then prints every other deployment-bound constant (QX and QSWAP index, fees, windows, QPAY name and limits, the
embedded contract-name table) as a checklist to re-verify on proposal day. That list is information only; a WARN
line there never changes the exit code.

Only the Python standard library is used. Nothing is written anywhere.
"""
import re
import sys

MAX_LISTED = 4  # lines listed per forbidden-text rule; the count of all matching lines is always printed

# The production QPAY issuer identity, as the eight-letter rows of the ID(...) in BuyAndBurnQpay
# (_Q,_P,_A,_Y,_N,_O,_W,_S / _W,_Z,_M,_G,_H,_F,_E,_A / ...). The 4 checksum letters (VLXG) are not part of ID(...).
PROD_ISSUER_ROWS = ("QPAYNOWS", "WZMGHFEA", "EVJXGZAV", "SHABAZDD", "BDIHTEBO", "PCOGHRGB", "CYCUZOHC")
PROD_ISSUER = "".join(PROD_ISSUER_ROWS)
# The devkit test wallet (RDOIGJSE...), used on the owner's testnet devkit only.
TEST_ISSUER_ROWS = ("RDOIGJSE", "SACHNDUK", "OBMYSPDD", "WWRDKXDP", "JQIKHJDV", "ZGDEOZGV", "LOGOBWHC")
TEST_ISSUER = "".join(TEST_ISSUER_ROWS)

EXPECTED_TICKS_PER_HOUR = 3600
EXPECTED_QPAYHUB_INDEX = 29        # default; the real network number is passed with --qpayhub-index N at deployment
EXPECTED_AUTO_SETTLE_MAX = 32      # K8 only: the season build uses 3 and must not pass the production gate
# Derived windows of the audited production build, in ticks. Not a failure condition (the spec'd failures are listed in the
# docstring); a different value only produces a WARN in the checklist, because it is the sign of a shortened-window build.
AUDITED_WINDOWS = (("QPOOL_VOTE_WINDOW", 259200), ("QPOOL_EXECUTE_WINDOW", 172800), ("QPOOL_IPO_VOTE_WINDOW", 86400),
                   ("QPOOL_PROPOSAL_LIFETIME", 432000))

FORBIDDEN_TEXT = (
    ("TESTNET ONLY", re.compile(r"TESTNET[ \t-]+ONLY", re.I)),
    ("TEST-ONLY", re.compile(r"TEST-ONLY", re.I)),
    ("SEASON BUILD ONLY", re.compile(r"SEASON[ \t-]+BUILD[ \t-]+ONLY", re.I)),
    ("QPOOLK6_", re.compile(r"QPOOLK6_", re.I)),
    ("QPOOLK7_", re.compile(r"QPOOLK7_", re.I)),
    ("RDOIGJSE", re.compile(r"RDOIGJSE", re.I)),
    ("QPoolK6", re.compile(r"QPoolK6", re.I)),
    ("QPoolK7", re.compile(r"QPoolK7", re.I)),
)
GENERIC_TAG = re.compile(r"QPOOLK[0-9]+", re.I)  # any other tag (K8, K10, ...); K6 and K7 are reported by name above


class Report:
    def __init__(self, path):
        self.path = path
        self.problems = 0
        self.warnings = 0

    def ok(self, msg):
        print("  ok    " + msg)

    def fail(self, line, rule, msg):
        self.problems += 1
        print("%s:%s: FAIL [%s] %s" % (self.path, line if line else "-", rule, msg))

    def warn(self, line, msg):
        self.warnings += 1
        print("%s:%s: WARN %s" % (self.path, line if line else "-", msg))


def blank_comments(text):
    """Return text with // comments, /* */ comments, string and char literals blanked out (same length, newlines kept)."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            j = text.find("\n", i)
            if j < 0:
                j = n
            out.append(" " * (j - i))
            i = j
        elif c == "/" and nxt == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"' and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            if j < n and text[j] == '"':
                j += 1
            j = min(j, n)
            out.append(" " * (j - i))
            i = j
        elif c == "'":
            m = re.compile(r"'(?:\\.|[^'\\\n])'").match(text, i)
            if m:
                out.append(" " * (m.end() - i))
                i = m.end()
            else:
                out.append(c)
                i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


def eval_expr(expr, env):
    """Evaluate a simple integer constant expression (+ - * << >> parens, casts, ULL/LL suffixes). None if it cannot."""
    e = re.sub(r"\bQPOOLK[0-9]+_", "QPOOL_", expr)
    e = re.sub(r"\(\s*(?:uint|sint)(?:8|16|32|64)\s*\)", "", e)
    e = re.sub(r"\b0x([0-9A-Fa-f]+)(?:ULL|LL|UL|U|L)?\b", lambda m: str(int(m.group(1), 16)), e)
    e = re.sub(r"\b(\d+)(?:ULL|LL|UL|U|L)\b", r"\1", e, flags=re.I)
    for name in set(re.findall(r"[A-Za-z_]\w*", e)):
        if name not in env:
            return None
        e = re.sub(r"\b%s\b" % re.escape(name), str(env[name]), e)
    if not re.fullmatch(r"[0-9+\-*()<>\s]+", e):
        return None
    try:
        v = eval(e, {"__builtins__": {}}, {})
    except Exception:
        return None
    return v if isinstance(v, int) else None


def match_paren(code, open_pos):
    """Index of the ')' matching the '(' at open_pos in comment-free code, or -1."""
    depth = 0
    for k in range(open_pos, len(code)):
        if code[k] == "(":
            depth += 1
        elif code[k] == ")":
            depth -= 1
            if depth == 0:
                return k
    return -1


def match_brace(code, open_pos):
    depth = 0
    for k in range(open_pos, len(code)):
        if code[k] == "{":
            depth += 1
        elif code[k] == "}":
            depth -= 1
            if depth == 0:
                return k
    return -1


def parse_id_calls(code):
    """Every ID(...) in code: (pos, letters or None, problem or None)."""
    calls = []
    for m in re.finditer(r"\bID\s*\(", code):
        op = m.end() - 1
        cl = match_paren(code, op)
        if cl < 0:
            calls.append((m.start(), None, "unterminated ID( ... )"))
            continue
        toks = [t.strip() for t in code[op + 1:cl].split(",")]
        if toks and toks[-1] == "":
            toks.pop()
        bad = [t for t in toks if not re.fullmatch(r"_[A-Z]", t)]
        if bad:
            calls.append((m.start(), None, "ID( ... ) holds something other than _A.._Z letters: %r" % bad[0]))
        else:
            calls.append((m.start(), "".join(t[1] for t in toks), None))
    return calls


def rows_of(letters):
    return [letters[i:i + 8] for i in range(0, len(letters), 8)]


def decode_name(v):
    """A contract / asset name packed little-endian into a uint64, as ASCII."""
    b = []
    while v:
        b.append(v & 0xFF)
        v >>= 8
    return "".join(chr(x) if 32 <= x < 127 else "?" for x in b)


def check_forbidden_text(rep, raw):
    lines = raw.split("\n")
    for name, rx in FORBIDDEN_TEXT:
        hits = [(i + 1, ln) for i, ln in enumerate(lines) if rx.search(ln)]
        if not hits:
            rep.ok("no %s text" % name)
            continue
        for ln_no, ln in hits[:MAX_LISTED]:
            rep.fail(ln_no, "forbidden-text", "%s: %s" % (name, ln.strip()[:110]))
        if len(hits) > MAX_LISTED:
            rep.fail(hits[MAX_LISTED][0], "forbidden-text",
                     "%s: ... and %d more line(s) (%d lines in all)" % (name, len(hits) - MAX_LISTED, len(hits)))
    other = []
    for i, ln in enumerate(lines):
        for m in GENERIC_TAG.finditer(ln):
            if m.group(0).upper() not in ("QPOOLK6", "QPOOLK7"):
                other.append((i + 1, m.group(0)))
    if other:
        for ln_no, tok in other[:MAX_LISTED]:
            rep.fail(ln_no, "forbidden-text", "tagged devkit identifier %s" % tok)
    else:
        rep.ok("no other QPOOLK<digits> tag")


def check_single_constant(rep, raw, code, name, expected, label):
    rx = re.compile(r"\b%s\s*=(?!=)\s*([^;]*);" % re.escape(name))
    defs = [(line_of(code, m.start()), m.group(1).strip()) for m in rx.finditer(code)]
    decl = re.compile(r"\bconstexpr\s+\w+\s+%s\s*=" % re.escape(name))
    if len(defs) == 0:
        tagged = [(line_of(code, m.start()), m.group(1), m.group(2).strip()) for m in
                  re.finditer(r"\b(QPOOL[A-Z0-9]+_%s)\s*=(?!=)\s*([^;]*);" % re.escape(name[len("QPOOL_"):]), code)]
        if tagged:
            for ln_no, tname, tval in tagged:
                rep.fail(ln_no, label, "%s is not defined; the tagged %s = %s is (production requires %s = %d)" %
                         (name, tname, tval, name, expected))
        else:
            rep.fail(None, label, "%s is not defined" % name)
        return
    if len(defs) > 1:
        for ln_no, v in defs:
            rep.fail(ln_no, label, "%s is assigned more than once (value %s)" % (name, v))
        return
    ln_no, val = defs[0]
    if not decl.search(code):
        rep.fail(ln_no, label, "%s is assigned but not by a constexpr definition" % name)
        return
    if not re.fullmatch(r"%d(?:U|L|UL|ULL|LL)?" % expected, val, re.I):
        rep.fail(ln_no, label, "%s = %s, production requires exactly %d" % (name, val, expected))
        return
    rep.ok("%s = %d (line %d)" % (name, expected, ln_no))


def check_issuer(rep, raw, code):
    calls = parse_id_calls(code)
    # (a) the test wallet must not appear in any ID(...) of the file
    test_hit = False
    for pos, letters, prob in calls:
        if letters is not None and TEST_ISSUER in letters:
            rep.fail(line_of(code, pos), "qpay-issuer", "ID(...) spells the devkit test wallet RDOIGJSE...")
            test_hit = True
    if not test_hit:
        rep.ok("no ID(...) spells the devkit test wallet")
    # (b) the QPAY issuer inside BuyAndBurnQpay
    m = re.search(r"PUBLIC_PROCEDURE_WITH_LOCALS\s*\(\s*BuyAndBurnQpay\s*\)", code)
    if not m:
        rep.fail(None, "qpay-issuer", "PUBLIC_PROCEDURE_WITH_LOCALS(BuyAndBurnQpay) not found")
        return None
    ob = code.find("{", m.end())
    cb = match_brace(code, ob) if ob >= 0 else -1
    if cb < 0:
        rep.fail(line_of(code, m.start()), "qpay-issuer", "cannot find the body of BuyAndBurnQpay")
        return None
    inside = [c for c in calls if ob < c[0] < cb]
    if len(inside) != 1:
        rep.fail(line_of(code, m.start()), "qpay-issuer",
                 "BuyAndBurnQpay holds %d ID(...) identities, expected exactly 1 (the QPAY issuer)" % len(inside))
        return None
    pos, letters, prob = inside[0]
    ln_no = line_of(code, pos)
    if prob:
        rep.fail(ln_no, "qpay-issuer", prob)
        return None
    if letters == PROD_ISSUER:
        rep.ok("QPAY issuer letters in BuyAndBurnQpay are the production ones (line %d)" % ln_no)
        return ln_no
    if len(letters) != len(PROD_ISSUER):
        rep.fail(ln_no, "qpay-issuer", "QPAY issuer has %d letters, expected %d" % (len(letters), len(PROD_ISSUER)))
    got, want = rows_of(letters), list(PROD_ISSUER_ROWS)
    for r in range(max(len(got), len(want))):
        g = got[r] if r < len(got) else "(missing)"
        w = want[r] if r < len(want) else "(extra)"
        if g != w:
            rep.fail(ln_no + 1 + r, "qpay-issuer", "QPAY issuer row %d is %s, production is %s" % (r + 1, g, w))
    return None


def check_structs(rep, code):
    names = [(line_of(code, m.start()), m.group(1)) for m in re.finditer(r"\bstruct\s+(QPOOL\w*)\b", code)]
    want_state2 = [l for l, n in names if n == "QPOOL2"]
    want_state = [m for m in re.finditer(r"\bstruct\s+QPOOL\s*:\s*public\s+ContractBase\b", code)]
    ok = True
    if len(want_state2) != 1 or not re.search(r"\bstruct\s+QPOOL2\s*\{\s*\}\s*;", code):
        rep.fail(None, "struct-names", "expected exactly one 'struct QPOOL2 {};', found %s" %
                 (", ".join("%s (line %d)" % (n, l) for l, n in names if n != "QPOOL") or "none"))
        ok = False
    if len(want_state) != 1:
        rep.fail(None, "struct-names", "expected exactly one 'struct QPOOL : public ContractBase', found %s" %
                 (", ".join("struct %s (line %d)" % (n, l) for l, n in names if n not in ("QPOOL2",)) or "none"))
        ok = False
    if ok:
        rep.ok("contract structs are QPOOL (line %d) and QPOOL2 (line %d)" %
               (line_of(code, want_state[0].start()), want_state2[0]))
    # any QPOOL-prefixed code identifier other than QPOOL, QPOOL2, QPOOL_*: what a tag rename leaves behind
    odd = [(line_of(code, m.start()), m.group(0)) for m in re.finditer(r"\bQPOOL(?!_|2\b)[A-Za-z0-9]+\b", code)]
    if odd:
        seen = set()
        for ln_no, tok in odd:
            if tok in seen:
                continue
            seen.add(tok)
            rep.fail(ln_no, "struct-names", "unexpected QPOOL-prefixed identifier %s (first use)" % tok)
    else:
        rep.ok("no QPOOL-prefixed identifier other than QPOOL, QPOOL2, QPOOL_*")


def collect_constants(code):
    """QPOOL_* constexpr definitions in file order: name -> (line, expression text, value or None)."""
    env, consts = {}, {}
    rx = re.compile(r"\bconstexpr\s+[A-Za-z_]\w*\s+(QPOOL(?:K[0-9]+)?_[A-Z0-9_]+)\s*=\s*([^;]+);")
    for m in rx.finditer(code):
        name = re.sub(r"^QPOOLK[0-9]+_", "QPOOL_", m.group(1))
        expr = re.sub(r"\bQPOOLK[0-9]+_", "QPOOL_", " ".join(m.group(2).split()))
        val = eval_expr(expr, env)
        if val is not None:
            env[name] = val
        consts[name] = (line_of(code, m.start()), expr, val)
    return consts


def fmt_const(name, consts):
    if name not in consts:
        return "%-40s (not found)" % name
    ln_no, expr, val = consts[name]
    if val is None:
        return "%-40s = %s   (line %d)" % (name, expr, ln_no)
    shown = "%s" % format(val, ",")
    extra = "" if re.fullmatch(r"\d+(?:U|L|UL|ULL|LL)?", expr, re.I) else "   [%s]" % expr
    return "%-40s = %s%s   (line %d)" % (name, shown, extra, ln_no)


def print_checklist(rep, raw, code, consts):
    print()
    print("== Deployment-bound values: re-verify on proposal day (information only, never a failure) ==")
    if re.search(r"\bQPOOLK[0-9]+_", code):
        print("   (this header carries a tag: the constants below are listed with the tag stripped from their names)")
    groups = (
        ("Contract indices (verify against the target core's contract_def.h)",
         ("QPOOL_QX_CONTRACT_INDEX", "QPOOL_QSWAP_CONTRACT_INDEX", "QPOOL_QPAYHUB_CONTRACT_INDEX",
          "QPOOL_KNOWN_CONTRACTS", "QPOOL_MAX_CONTRACT_INDEX")),
        ("QPAY buy-and-burn (verify the issuer, the name value and the ceiling with the owner)",
         ("QPOOL_QPAY_ASSET_NAME", "QPOOL_QPAY_MAX_PRICE", "QPOOL_QPAY_MAX_LEVELS", "QPOOL_QX_TRANSFER_FEE",
          "QPOOL_QX_SELL_FIXED_COSTS", "QPOOL_QSWAP_SELL_FEE")),
        ("Fees and splits (verify against the owner's decisions)",
         ("QPOOL_CREATE_POOL_FEE", "QPOOL_JOIN_FEE", "QPOOL_PROPOSAL_FEE", "QPOOL_MIN_CONTRIBUTION",
          "QPOOL_MANAGEMENT_PERMILLE", "QPOOL_REVENUE_SHAREHOLDER_PERMILLE", "QPOOL_REVENUE_QPAYHUB_PERMILLE",
          "QPOOL_REVENUE_QPAYBUY_PERMILLE")),
        ("Windows (ticks; 1 tick is about 1 second on mainnet)",
         ("QPOOL_TICKS_PER_HOUR", "QPOOL_VOTE_WINDOW", "QPOOL_EXECUTE_WINDOW", "QPOOL_IPO_VOTE_WINDOW",
          "QPOOL_PROPOSAL_LIFETIME", "QPOOL_IPO_PROPOSAL_LIFETIME", "QPOOL_QUORUM_PERMILLE", "QPOOL_SQUAT_EPOCHS",
          "QPOOL_TICK_LIMIT")),
    )
    listed = set()
    for title, names in groups:
        print("-- " + title)
        for n in names:
            print("   " + fmt_const(n, consts))
            listed.add(n)
            if n not in consts:
                rep.warn(None, "%s was not found (expected in the checklist)" % n)
    for n, want in AUDITED_WINDOWS:
        if n in consts and consts[n][2] is not None and consts[n][2] != want:
            rep.warn(consts[n][0], "%s = %s ticks, the audited production build has %s" % (n, format(consts[n][2], ","), format(want, ",")))
    # a catch-all for anything else that looks deployment-bound
    sweep = re.compile(r"(?:INDEX|FEE|WINDOW|TICKS|LIFETIME|ASSET_NAME|PERMILLE|MAX_PRICE|COSTS|EPOCHS|QUORUM|CEILING)")
    extra = [n for n in consts if n not in listed and sweep.search(n) and not n.startswith("QPOOL_ERR_")
             and not n.startswith("QPOOL_MAX_") and not n.startswith("QPOOL_KIND_")]
    if extra:
        print("-- Other constants whose names look deployment-bound")
        for n in extra:
            print("   " + fmt_const(n, consts))

    # QPAY name value and identity
    print("-- QPAY token")
    if "QPOOL_QPAY_ASSET_NAME" in consts and consts["QPOOL_QPAY_ASSET_NAME"][2] is not None:
        v = consts["QPOOL_QPAY_ASSET_NAME"][2]
        print("   QPOOL_QPAY_ASSET_NAME %d decodes to the ASCII name %r (QPAYHUB uses the same value)" % (v, decode_name(v)))
        if decode_name(v) != "QPAY":
            rep.warn(consts["QPOOL_QPAY_ASSET_NAME"][0], "QPOOL_QPAY_ASSET_NAME decodes to %r, not 'QPAY'" % decode_name(v))
    print("   production issuer identity (56 letters, checksum VLXG not in ID(...)): %s" % PROD_ISSUER)
    where = [line_of(raw, m.start()) for m in re.finditer(re.escape(PROD_ISSUER), raw)]
    if where:
        print("   the header names it in a comment at line(s): %s" % ", ".join(str(w) for w in where))
    else:
        rep.warn(None, "the production QPAY issuer identity is not quoted anywhere in the header text")

    # embedded name table
    print("-- Embedded contract-name table (QPOOL_KNOWN_CONTRACTS rows; verify each row against the explorer)")
    by_index, by_name = {}, {}
    for m in re.finditer(r"if \(input\.index == (\d+)\) \{ output\.name = (\d+)ULL; return; \}[ \t]*(?://[ \t]*(\S+))?", raw):
        by_index[int(m.group(1))] = (int(m.group(2)), m.group(3), line_of(raw, m.start()))
    for m in re.finditer(r"if \(input\.name == (\d+)ULL\) \{ output\.index = (\d+); return; \}[ \t]*(?://[ \t]*(\S+))?", raw):
        by_name[int(m.group(1))] = (int(m.group(2)), m.group(3), line_of(raw, m.start()))
    if not by_index:
        rep.warn(None, "KnownContractName table not found; nothing to list")
    else:
        cells = []
        for idx in sorted(by_index):
            nm, label, ln_no = by_index[idx]
            dec = decode_name(nm)
            cells.append("%d=%s" % (idx, dec))
            if label is not None and label != dec:
                rep.warn(ln_no, "table row %d: value %d decodes to %r but the comment says %r" % (idx, nm, dec, label))
            back = by_name.get(nm)
            if back is None or back[0] != idx:
                rep.warn(ln_no, "table row %d (%s) has no matching KnownContractIndex row" % (idx, dec))
        for i in range(0, len(cells), 8):
            print("   " + "  ".join(cells[i:i + 8]))
        known = consts.get("QPOOL_KNOWN_CONTRACTS", (0, "", None))[2]
        if known is not None and known != len(by_index):
            rep.warn(None, "QPOOL_KNOWN_CONTRACTS = %d but the table has %d rows" % (known, len(by_index)))
        if sorted(by_index) != list(range(1, len(by_index) + 1)):
            rep.warn(None, "the table indices are not the contiguous run 1..%d" % len(by_index))
        for cname, expect in (("QPOOL_QX_CONTRACT_INDEX", "QX"), ("QPOOL_QSWAP_CONTRACT_INDEX", "QSWAP"),
                              ("QPOOL_QPAYHUB_CONTRACT_INDEX", "QPAYHUB")):
            if cname in consts and consts[cname][2] is not None:
                idx = consts[cname][2]
                row = by_index.get(idx)
                have = decode_name(row[0]) if row else "(no row)"
                # the QPAYHUB name is 7 letters: its decoded name is QPAYHUB
                if have != expect:
                    rep.warn(consts[cname][0], "%s = %d but the table names index %d %s, expected %s" %
                             (cname, idx, idx, have, expect))
    # preprocessor directives could hide a conditional value
    pp = [(i + 1, ln.strip()) for i, ln in enumerate(code.split("\n")) if re.match(r"\s*#", ln)]
    if pp:
        print("-- Preprocessor directives (a conditional could hide a second value set)")
        for ln_no, ln in pp[:MAX_LISTED]:
            rep.warn(ln_no, ln)


def main(argv):
    args = list(argv[1:])
    qpayhub_index = EXPECTED_QPAYHUB_INDEX
    if "--qpayhub-index" in args:
        i = args.index("--qpayhub-index")
        try:
            qpayhub_index = int(args[i + 1])
        except (IndexError, ValueError):
            print("usage: python3 -I release_gate.py [--qpayhub-index N] <header>", file=sys.stderr)
            return 2
        del args[i:i + 2]
    if len(args) != 1 or args[0] in ("-h", "--help"):
        print("usage: python3 -I release_gate.py [--qpayhub-index N] <header>   (N defaults to %d; pass the real network index at deployment)" % EXPECTED_QPAYHUB_INDEX, file=sys.stderr)
        return 2
    path = args[0]
    try:
        with open(path, "rb") as fh:
            data = fh.read()
        raw = data.decode("utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        print("release_gate: cannot read %s: %s" % (path, exc), file=sys.stderr)
        return 2
    code = blank_comments(raw)
    rep = Report(path)
    print("release_gate: %s (%d bytes, %d lines)" % (path, len(data), raw.count("\n") + (0 if raw.endswith("\n") else 1)))
    print("== Production checks ==")
    check_forbidden_text(rep, raw)
    check_single_constant(rep, raw, code, "QPOOL_TICKS_PER_HOUR", EXPECTED_TICKS_PER_HOUR, "ticks-per-hour")
    check_single_constant(rep, raw, code, "QPOOL_QPAYHUB_CONTRACT_INDEX", qpayhub_index, "qpayhub-index")
    if "QPOOL_AUTO_SETTLE_MAX" in code:
        check_single_constant(rep, raw, code, "QPOOL_AUTO_SETTLE_MAX", EXPECTED_AUTO_SETTLE_MAX, "auto-settle-bound")
    check_issuer(rep, raw, code)
    check_structs(rep, code)
    print_checklist(rep, raw, code, collect_constants(code))
    print()
    if rep.problems:
        print("RELEASE GATE: FAIL (%d problem(s), %d warning(s)) %s" % (rep.problems, rep.warnings, path))
        return 1
    print("RELEASE GATE: PASS (0 problems, %d warning(s)) %s" % (rep.warnings, path))
    print("PASS means: production values and names only. It does not prove the checklist above is right for the target network.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
