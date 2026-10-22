#!/usr/bin/env python3
"""lint_contract.py -- fast static scan of a Qubic contract header for constructs contracts must not use.

usage: python3 -I lint_contract.py [--quiet] [--rules] [--prefix NAME] <header> [<header> ...]
(--prefix: contract state struct name for the L13 naming rule; default: the struct deriving from ContractBase)

Output: one line per hit, `file:line:col: [RULE] message`, then a per-rule summary.
Exit status: 0 no hit, 1 at least one hit, 2 unreadable input.

The rules are those of the core's contract documentation (core/doc/contracts.md, "Restrictions of C++ Language
Features"; the official checker is the external qubic-contract-verify tool, which is not installed here), plus a few
[extra] rules for things that cannot work in the core build at all. This is a TOKEN-LEVEL heuristic, not a C++
parser: it needs no compiler, ignores comments, and never looks inside string literals for other rules, but it
can miss exotic constructs and (rarely) flag legal ones. It complements check_contract.sh, it does not replace
the compiler or the official verifier. See HARNESS.md for what was and was not calibrated.

Rules
  L01-PREPROC    any '#' (include, define, pragma, #if ...): the whole directive line is reported once
  L02-INDEX      '[' or ']' (arrays, subscripting, lambdas, attributes): use Array / HashMap / Collection members
  L03-MODULO     '%' or '%=': use mod()
  L04-DIVISION   '/' or '/=': use div()
  L05-FLOAT      float / double keywords and floating-point literals
  L06-POINTER    pointer declarations, casts to pointers, dereference '*', '->', address-of '&', nullptr/NULL, new/delete
  L07-LOCAL      a local variable (or for-loop index) declared inside a function/procedure body
  L08-LITERAL    string literal or character literal (the characters " and ' are forbidden), digit separators
  L09-VARIADIC   '...' (variadic arguments, parameter packs)
  L10-DUNDERSCORE  an identifier containing '__' (reserved for core internals)
  L11-KEYWORD    union, QpiContext, const_cast, reinterpret_cast
  L12-SCOPE      '::' on std / lowercase namespaces or with a leading global '::'
  L13-GLOBAL     global variable, global typedef/using (except `using namespace QPI;`), global constant/struct/class
                 whose name does not begin with the contract state struct name (QPOOL)
  L14-STATIC     a static VARIABLE (global / member / local that is not const or constexpr), or any 'static' inside a
                 function body. `static constexpr` constants and `inline static` helper functions are accepted by the core.
  L15-EXTRA      [extra] try / catch / throw / dynamic_cast / typeid / asm: not available in the core build
"""
import re
import sys

DEFAULT_PREFIX = "QPOOL"

BUILTIN_TYPES = {
    "void", "bool", "char", "short", "int", "long", "float", "double", "signed", "unsigned", "wchar_t",
    "char8_t", "char16_t", "char32_t", "auto", "size_t",
}
QUALIFIERS = {"const", "constexpr", "volatile", "static", "register", "extern", "mutable", "inline", "thread_local"}
QPI_TYPES = {
    "uint8", "uint16", "uint32", "uint64", "uint128", "sint8", "sint16", "sint32", "sint64", "id", "bit",
    "m256i", "Asset", "Array", "BitArray", "HashMap", "HashSet", "Collection", "LinkedList", "DateAndTime",
    "NoData", "ContractBase", "ProposalDataYesNo", "AssetIssuanceSelect", "AssetOwnershipSelect",
    "AssetPossessionSelect", "ProposalSummarizedVotingDataV1",
}
CONTROL = {"return", "else", "case", "default", "goto", "do", "throw", "delete", "new", "typename", "sizeof",
           "co_return", "co_yield", "co_await", "if", "for", "while", "switch", "break", "continue", "using",
           "typedef", "struct", "class", "enum", "union", "namespace", "template", "static_assert", "public",
           "private", "protected", "friend", "operator", "alignas", "alignof", "decltype", "this"}
STMT_PRECEDING_KEYWORDS = {"else", "do"}

MULTI_PUNCT = ["<<=", ">>=", "->*", "...", "->", "::", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
               "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", ".*", "<=>"]


class Tok:
    __slots__ = ("kind", "text", "line", "col")

    def __init__(self, kind, text, line, col):
        self.kind, self.text, self.line, self.col = kind, text, line, col

    def __repr__(self):
        return "%s:%s@%d:%d" % (self.kind, self.text, self.line, self.col)


def lex(src):
    """Return (tokens, hits_from_lexer). Comments are dropped; string/char literal CONTENTS are never scanned."""
    toks, hits = [], []
    i, n = 0, len(src)
    line, col = 1, 1

    def adv(k):
        nonlocal i, line, col
        for _ in range(k):
            if src[i] == "\n":
                line += 1
                col = 1
            else:
                col += 1
            i += 1

    while i < n:
        c = src[i]
        if c in " \t\r\n\f\v":
            adv(1)
            continue
        if c == "\\" and i + 1 < n and src[i + 1] in "\r\n":  # stray line continuation
            adv(1)
            continue
        if src.startswith("//", i):
            while i < n and src[i] != "\n":
                if src[i] == "\\" and i + 1 < n and src[i + 1] == "\n":
                    adv(1)
                adv(1)
            continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            adv((n if j < 0 else j + 2) - i)
            continue
        if c == "#":
            hits.append((line, col, "L01-PREPROC", "preprocessor directive '#' (no #include / #define / #pragma / #if in a contract)"))
            while i < n and src[i] != "\n":  # skip the directive, honouring backslash continuations
                if src[i] == "\\" and i + 1 < n and src[i + 1] in "\r\n":
                    adv(1)
                    if i < n and src[i] == "\r":
                        adv(1)
                if i < n:
                    adv(1)
            continue
        # string / char literals (with encoding prefixes and raw strings)
        m = re.compile(r'(?:u8|u|U|L)?R"([^()\\ \t\n]{0,16})\(').match(src, i)
        if m:
            end = src.find(")" + m.group(1) + '"', m.end())
            end = n if end < 0 else end + len(m.group(1)) + 2
            hits.append((line, col, "L08-LITERAL", "raw string literal (the character \" is forbidden in contracts)"))
            adv(end - i)
            continue
        m = re.compile(r'(?:u8|u|U|L)?(["\'])').match(src, i)
        if m:
            q = m.group(1)
            sl, sc = line, col
            j = m.end()
            while j < n and src[j] != q and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            hits.append((sl, sc, "L08-LITERAL",
                         ("string literal" if q == '"' else "character literal") +
                         " (the characters \" and ' are forbidden in contracts; static_assert needs no message)"))
            adv(min(n, j + 1) - i)
            continue
        # numbers
        if c.isdigit() or (c == "." and i + 1 < n and src[i + 1].isdigit()):
            sl, sc = line, col
            j = i
            while j < n:
                d = src[j]
                if d.isalnum() or d in "._":
                    j += 1
                elif d == "'" and j + 1 < n and src[j + 1].isalnum():
                    hits.append((line, col + (j - i), "L08-LITERAL", "digit separator ' (the character ' is forbidden in contracts)"))
                    j += 1
                elif d in "+-" and ((src[j - 1] in "eE" and not re.match(r"0[xX]", src[i:j])) or src[j - 1] in "pP"):
                    j += 1
                else:
                    break
            text = src[i:j]
            toks.append(Tok("num", text, sl, sc))
            adv(j - i)
            continue
        if c.isalpha() or c == "_":
            j = i
            while j < n and (src[j].isalnum() or src[j] == "_"):
                j += 1
            toks.append(Tok("id", src[i:j], line, col))
            adv(j - i)
            continue
        for p in MULTI_PUNCT:
            if src.startswith(p, i):
                toks.append(Tok("p", p, line, col))
                adv(len(p))
                break
        else:
            toks.append(Tok("p", c, line, col))
            adv(1)
    return toks, hits


def is_float_literal(text):
    t = text.replace("'", "")
    if re.match(r"0[xX]", t):
        return bool(re.search(r"[pP][+-]?\d", t))
    if re.match(r"0[bB]", t):
        return False
    return bool(re.search(r"[.]|[eE][+-]?\d", t)) and not re.search(r"[^\d.eE+\-fFlL]", t)


def T0(toks, k):
    return toks[k].text if 0 <= k < len(toks) else ""


def lint(path, prefix_arg=None):
    try:
        with open(path, "rb") as fh:
            src = fh.read().decode("utf-8", errors="replace")
    except OSError as e:
        print("%s: cannot read: %s" % (path, e))
        return None
    toks, hits = lex(src)
    n = len(toks)

    # ---- pre-pass: names declared as struct/enum/class/union/using-alias in this file
    declared_types = set()
    for k in range(n - 1):
        if toks[k].kind == "id" and toks[k].text in ("struct", "class", "enum", "union") and toks[k + 1].kind == "id":
            declared_types.add(toks[k + 1].text)
    typeset = declared_types | QPI_TYPES
    CONTRACT_PREFIX = prefix_arg
    if CONTRACT_PREFIX is None:  # the contract state struct is the one deriving from ContractBase
        CONTRACT_PREFIX = DEFAULT_PREFIX
        for k in range(n - 4):
            if T0(toks, k) == "struct" and T0(toks, k + 2) == ":" and T0(toks, k + 3) == "public" and T0(toks, k + 4) == "ContractBase":
                CONTRACT_PREFIX = toks[k + 1].text
                break

    def T(k):
        return toks[k].text if 0 <= k < n else ""

    def add(k, rule, msg):
        hits.append((toks[k].line, toks[k].col, rule, msg))

    def prev_is_operand_end(k):
        if k == 0:
            return False
        p = toks[k - 1]
        if p.kind == "num":
            return True
        if p.kind == "id":
            return p.text not in ("return", "case", "else", "do", "new", "delete", "throw", "co_return")
        if p.text == ")":
            return not paren_is_cast(k - 1)  # after a cast the next '*' / '&' is unary
        return p.text == "]"

    def prev_ident_is_type(k):
        """toks[k-1] is an identifier that denotes a type here (so '*' / '&' after it declares a pointer/reference)."""
        p = toks[k - 1]
        if p.kind != "id":
            return False
        if p.text in BUILTIN_TYPES or p.text in ("const", "struct", "class", "enum", "typename"):
            return True
        if k >= 2 and toks[k - 2].text in (".", "->", "::") and toks[k - 2].text != "::":
            return False  # member access: a.b * c
        if p.text in typeset:
            return True
        return False

    def paren_is_cast(k):
        """toks[k] is ')': does the parenthesis enclose only a type (a C-style cast like (uint64) or (Foo*))?"""
        depth = 0
        m = k
        while m >= 0:
            if T(m) == ")":
                depth += 1
            elif T(m) == "(":
                depth -= 1
                if depth == 0:
                    break
            m -= 1
        if m < 0 or k - m < 2:
            return False
        if m > 0 and ((toks[m - 1].kind == "id" and toks[m - 1].text not in ("return", "case", "else", "do", "throw"))
                      or toks[m - 1].text in (")", "]", ">")):
            return False  # sizeof(T), f(T), g<T>(U): a call or operator, not a C-style cast
        for q in range(m + 1, k):
            tk = toks[q]
            if tk.kind == "id":
                if not (tk.text in BUILTIN_TYPES or tk.text in typeset or tk.text in QUALIFIERS):
                    return False
            elif tk.kind == "num":
                continue
            elif tk.text not in ("::", "*", "&", "<", ">", ",", ">>"):
                return False
        return True

    # ---- scope tracking and statement starts
    stack = []  # entries: kind in {"ns","struct","enum","func","block","init"}
    stmt_start = True
    paren_stack = []  # (kind) per open paren: "for", "other"

    def cur_scope():
        return stack[-1] if stack else "ns"

    def in_func():
        return any(s in ("func", "block") for s in stack)

    def parse_decl(k):
        """If toks[k:] looks like `[quals] TYPE [*&]* NAME (; = ( { , [ )` return (name_index, quals, has_const)."""
        j = k
        quals = set()
        saw_type = False
        while j < n and toks[j].kind == "id" and (toks[j].text in QUALIFIERS):
            quals.add(toks[j].text)
            j += 1
        # builtin multiword types
        while j < n and toks[j].kind == "id" and toks[j].text in BUILTIN_TYPES:
            saw_type = True
            j += 1
        if not saw_type:
            if j < n and toks[j].kind == "id" and toks[j].text not in CONTROL and toks[j].text not in QUALIFIERS:
                saw_type = True
                j += 1
                while j + 1 < n and T(j) == "::" and toks[j + 1].kind == "id":
                    j += 2
                if T(j) == "<":  # template args, balanced
                    depth = 0
                    while j < n:
                        if T(j) == "<":
                            depth += 1
                        elif T(j) == ">":
                            depth -= 1
                        elif T(j) == ">>":
                            depth -= 2
                        elif T(j) in (";", "{", "}"):
                            return None
                        j += 1
                        if depth <= 0:
                            break
                    if depth > 0:
                        return None
            else:
                return None
        # trailing const etc and pointer / reference markers
        while j < n and (T(j) in ("*", "&", "&&") or (toks[j].kind == "id" and T(j) in ("const", "volatile"))):
            j += 1
        if j < n and toks[j].kind == "id" and toks[j].text not in CONTROL and toks[j].text not in BUILTIN_TYPES \
                and toks[j].text not in QUALIFIERS:
            nxt = T(j + 1)
            if nxt in (";", "=", "(", "{", ",", "[", ")", ":") or nxt == "":
                return (j, quals)
        return None

    i = 0
    while i < n:
        t = toks[i]
        x = t.text

        # ---------------- token-level rules
        if t.kind == "p":
            if x in ("[", "]"):
                add(i, "L02-INDEX", "'%s' is forbidden (no arrays / indexing / lambdas): use Array, HashMap, Collection" % x)
            elif x in ("%", "%="):
                add(i, "L03-MODULO", "'%s': the modulo operator is forbidden, use mod()" % x)
            elif x in ("/", "/="):
                add(i, "L04-DIVISION", "'%s': the division operator is forbidden, use div()" % x)
            elif x == "...":
                add(i, "L09-VARIADIC", "'...' (variadic arguments / parameter packs) is forbidden")
            elif x in ("->", "->*", ".*"):
                add(i, "L06-POINTER", "'%s' pointer member access is forbidden" % x)
            elif x == "*":
                mult = prev_is_operand_end(i) and not prev_ident_is_type(i)
                if prev_is_operand_end(i) and not mult:
                    add(i, "L06-POINTER", "'*' after type '%s' declares or casts to a pointer (pointers are forbidden)" % T(i - 1))
                elif not mult:
                    if T(i - 1) == ">":
                        add(i, "L06-POINTER", "'*' after a template argument list declares a pointer")
                    else:
                        add(i, "L06-POINTER", "'*' is only allowed as multiplication (this is a dereference / pointer)")
                elif T(i + 1) in (")", ",", ">", ";") and T(i - 1) not in ("(",):
                    add(i, "L06-POINTER", "'*' followed by '%s' is a pointer type, not a multiplication" % T(i + 1))
            elif x == "&":
                if not prev_is_operand_end(i) and T(i - 1) not in (">",) and (toks[i + 1].kind == "id" or T(i + 1) == "("
                                                                              if i + 1 < n else False):
                    add(i, "L06-POINTER", "unary '&' (address-of) creates a pointer and is forbidden")
            elif x == "::":
                pk = toks[i - 1] if i > 0 else None
                if pk is None or not (pk.kind == "id" or pk.text == ">"):
                    add(i, "L12-SCOPE", "global scope operator '::' is forbidden (only QPI / contract-defined structs, enums and namespaces)")
                elif pk.kind == "id" and (pk.text == "std" or pk.text[0].islower() or pk.text[0] == "_") \
                        and pk.text not in BUILTIN_TYPES and pk.text not in typeset:
                    add(i, "L12-SCOPE", "'%s::' is not a QPI or contract-defined scope; '::' is only allowed for structs, enums and namespaces defined in contracts and qpi.h" % pk.text)
        elif t.kind == "num":
            if is_float_literal(x):
                add(i, "L05-FLOAT", "floating-point literal '%s' (float / double are forbidden)" % x)
        elif t.kind == "id":
            if x in ("float", "double"):
                add(i, "L05-FLOAT", "'%s' is forbidden (floating point is not well-defined)" % x)
            if "__" in x:
                add(i, "L10-DUNDERSCORE", "identifier '%s' contains '__' (reserved for core internals)" % x)
            if x in ("union", "QpiContext", "const_cast", "reinterpret_cast"):
                add(i, "L11-KEYWORD", "'%s' is forbidden in contracts" % x)
            if x in ("nullptr", "NULL"):
                add(i, "L06-POINTER", "'%s': null pointer constant, pointers are forbidden" % x)
            if x in ("new", "delete"):
                add(i, "L06-POINTER", "'%s': dynamic allocation / pointers are forbidden" % x)
            if x == "static" and in_func():
                add(i, "L14-STATIC", "'static' inside a function body (a static local is a hidden global)")
            if x in ("try", "catch", "throw", "dynamic_cast", "typeid", "asm"):
                add(i, "L15-EXTRA", "[extra] '%s' is not available in the core build (no exceptions / RTTI / inline asm)" % x)

        # ---------------- scope tracking
        if t.kind == "p":
            if x == "(":
                paren_stack.append("for" if T(i - 1) == "for" else "other")
                if T(i - 1) == "for" and in_func():
                    k2 = i + 1
                    d = parse_decl(k2)
                    if d:
                        add(d[0], "L07-LOCAL", "for-loop index '%s' is a local variable on the call stack (put it in the *_locals struct)" % toks[d[0]].text)
            elif x == ")":
                if paren_stack:
                    paren_stack.pop()
            elif x == "{":
                # classify
                kind = None
                k = i - 1
                while k >= 0 and T(k) not in (";", "{", "}"):
                    if toks[k].kind == "id" and toks[k].text in ("struct", "class", "union") and not (k > 0 and T(k - 1) == "enum"):
                        kind = "struct"
                        break
                    if toks[k].kind == "id" and toks[k].text == "enum":
                        kind = "enum"
                        break
                    if toks[k].kind == "id" and toks[k].text == "namespace":
                        kind = "ns"
                        break
                    k -= 1
                if kind is None:
                    p = T(i - 1)
                    pp = T(i - 2)
                    if p == ")" or (p in ("const", "noexcept", "override", "final") and ")" in (pp, T(i - 3))) or p == "else" or p == "do":
                        kind = "block" if in_func() else "func"
                    elif in_func():
                        kind = "init" if p in ("=", ",", "(", "return") or toks[i - 1].kind == "id" else "block"
                    else:
                        kind = "init"
                stack.append(kind)
                stmt_start = kind in ("func", "block", "struct", "ns")
                i += 1
                continue
            elif x == "}":
                if stack:
                    stack.pop()
                stmt_start = True
                i += 1
                continue
            elif x == ";":
                if not paren_stack:
                    stmt_start = True
                i += 1
                continue

        # ---------------- statement-start analysis (declarations)
        if stmt_start and t.kind == "id":
            sc = cur_scope()
            if sc in ("func", "block") and x not in CONTROL:
                d = parse_decl(i)
                if d:
                    add(d[0], "L07-LOCAL", "local variable '%s' declared in a function body (put it in the *_locals struct)" % toks[d[0]].text)
            elif sc == "ns" and not stack:
                if x == "typedef":
                    add(i, "L13-GLOBAL", "global 'typedef' is forbidden (typedef / using are only allowed in local scope)")
                elif x == "using":
                    if not (T(i + 1) == "namespace" and T(i + 2) == "QPI" and T(i + 3) == ";"):
                        add(i, "L13-GLOBAL", "global 'using' is forbidden; only `using namespace QPI;` is allowed at global scope")
                elif x in ("struct", "class"):
                    if toks[i + 1].kind == "id" if i + 1 < n else False:
                        nm = T(i + 1)
                        if not nm.startswith(CONTRACT_PREFIX):
                            add(i + 1, "L13-GLOBAL", "global %s '%s' must begin with the contract state struct name '%s'" % (x, nm, CONTRACT_PREFIX))
                elif x not in CONTROL:
                    d = parse_decl(i)
                    if d:
                        nm = toks[d[0]].text
                        quals = d[1]
                        if T(d[0] + 1) == "(":
                            pass  # function declaration / definition at global scope: not a variable
                        elif "constexpr" in quals or "const" in quals:
                            if not nm.startswith(CONTRACT_PREFIX):
                                add(d[0], "L13-GLOBAL", "global constant '%s' must begin with the contract state struct name '%s'" % (nm, CONTRACT_PREFIX))
                        else:
                            add(d[0], "L13-GLOBAL", "global variable '%s' is forbidden (all state lives in StateData)" % nm)
                elif x in QUALIFIERS:
                    pass
            elif sc == "struct" and (x == "static" or x in QUALIFIERS):
                d = parse_decl(i)
                if d and "static" in d[1] and "constexpr" not in d[1] and "const" not in d[1] and T(d[0] + 1) != "(":
                    add(d[0], "L14-STATIC", "static data member '%s' is a hidden global (only `static constexpr` constants are accepted)" % toks[d[0]].text)
            stmt_start = x in STMT_PRECEDING_KEYWORDS
        elif stmt_start and t.kind in ("num", "p") and x not in ("{", "}", ";"):
            stmt_start = False
        i += 1

    # de-duplicate (a construct can be reached by two paths) and sort
    seen, out = set(), []
    for h in sorted(hits):
        key = (h[0], h[1], h[2])
        if key not in seen:
            seen.add(key)
            out.append(h)
    return out, src.split("\n")


def main(argv):
    quiet = "--quiet" in argv
    prefix = None
    if "--prefix" in argv:
        k = argv.index("--prefix")
        prefix = argv[k + 1] if k + 1 < len(argv) else None
        argv = argv[:k] + argv[k + 2:]
    if "--rules" in argv:
        print(__doc__)
        return 0
    files = [a for a in argv if not a.startswith("--")]
    if not files:
        print(__doc__)
        return 2
    rc = 0
    for path in files:
        res = lint(path, prefix)
        if res is None:
            rc = max(rc, 2)
            continue
        hits, lines = res
        counts = {}
        for ln, col, rule, msg in hits:
            counts[rule] = counts.get(rule, 0) + 1
            if not quiet:
                print("%s:%d:%d: [%s] %s" % (path, ln, col, rule, msg))
                if 1 <= ln <= len(lines):
                    print("    | " + lines[ln - 1].strip()[:140])
        if hits:
            rc = max(rc, 1)
            print("%s: LINT FAIL: %d hit(s) (%s)" % (path, len(hits), ", ".join("%s x%d" % kv for kv in sorted(counts.items()))))
        else:
            print("%s: LINT PASS: no forbidden construct found" % path)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
