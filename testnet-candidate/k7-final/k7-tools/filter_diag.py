#!/usr/bin/env python3
"""filter_diag.py -- classify clang diagnostics for check_contract.sh.

usage: filter_diag.py <diag-file> <overlay-header-path> <original-header-path> [--warnings]

The diag file is clang's stderr, produced with -fno-caret-diagnostics (one line per diagnostic).
Every error/warning is grouped with the notes that follow it (template-instantiation chains,
"expanded from macro" and so on) and sorted into:

  A  attributable to the contract header: the error itself, or any note of its chain, points into the header
     (an error raised deep inside a core template that the header instantiated is reported at the header line)
  B  not located in the header, but the text names the contract (QPOOL / QPool): for example a static_assert
     in contract_def.h about the state size
  C  everything else: portability-shim or core noise. Never shown as a contract error.

Exit code: 0 no error at all, 1 at least one A/B error, 2 only C errors (harness problem), 3 usage.
Header warnings (class A warnings) are summarised separately and never change the exit code.
"""
import os
import re
import sys
from collections import Counter

LINE = re.compile(r"^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+): (?P<sev>fatal error|error|warning|note|remark): (?P<msg>.*)$")


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 3
    diag_path, overlay, original = sys.argv[1], sys.argv[2], sys.argv[3]
    show_warnings = "--warnings" in sys.argv[4:]
    try:
        with open(diag_path, "r", errors="replace") as fh:
            lines = fh.read().splitlines()
    except OSError as e:
        print("cannot read diagnostics: %s" % e)
        return 3

    blocks = []  # each: list of dict(file,line,col,sev,msg); first is the primary
    for raw in lines:
        m = LINE.match(raw)
        if not m:
            continue
        d = m.groupdict()
        d["line"] = int(d["line"])
        d["col"] = int(d["col"])
        if d["sev"] in ("error", "fatal error", "warning"):
            blocks.append([d])
        elif blocks:
            blocks[-1].append(d)

    overlay_n = os.path.normpath(overlay)

    def in_header(d):
        return os.path.normpath(d["file"]) == overlay_n

    def shown(d):
        f = original if in_header(d) else d["file"]
        # core files are shown relative to the scratch core for readability
        if "/core-scratch/" in f:
            f = "core:" + f.split("/core-scratch/", 1)[1]
        return "%s:%d:%d: %s: %s" % (f, d["line"], d["col"], d["sev"], d["msg"])

    errA, errB, errC, warnA = [], [], [], []
    for b in blocks:
        prim = b[0]
        is_err = prim["sev"] != "warning"
        hdr = [d for d in b if in_header(d)]
        if hdr:
            # location reported: the primary if it is in the header, else the first header note of the chain
            loc = hdr[0]
            if loc is prim:
                text = shown(prim)
            else:
                pf = shown(prim).split(": ", 1)[0]
                text = "%s:%d:%d: %s (triggered by this line; reported in %s): %s" % (
                    original, loc["line"], loc["col"], prim["sev"], pf, prim["msg"])
            (errA if is_err else warnA).append((loc["line"], loc["col"], text, prim["msg"]))
        elif is_err:
            blob = " ".join(d["msg"] for d in b)
            if re.search(r"QPOOL|QPool", blob):
                errB.append(shown(prim))
            else:
                errC.append(shown(prim))

    def dedupe(items, keyf):
        seen, out = set(), []
        for it in items:
            k = keyf(it)
            if k not in seen:
                seen.add(k)
                out.append(it)
        return out

    errA = dedupe(sorted(errA), lambda t: t[2])
    errB = dedupe(errB, lambda t: t)
    errC = dedupe(errC, lambda t: t)
    warnA = dedupe(sorted(warnA), lambda t: t[2])

    out = []
    if errA:
        out.append("%d error(s) attributable to %s:" % (len(errA), original))
        for t in errA[:40]:
            out.append("  " + t[2])
        if len(errA) > 40:
            out.append("  ... %d more" % (len(errA) - 40))
    if errB:
        out.append("%d contract-related error(s) located in core scaffolding (text names QPOOL):" % len(errB))
        for t in errB[:15]:
            out.append("  " + t)
    if errC and not errA and not errB:
        out.append("%d error(s) NOT attributable to the header (harness / core / shim problem), first 15:" % len(errC))
        for t in errC[:15]:
            out.append("  " + t)
    elif errC:
        out.append("(%d further error(s) in core code were suppressed as not attributable to the header)" % len(errC))

    if warnA:
        flags = Counter()
        for _, _, _, msg in warnA:
            m = re.search(r"\[(-W[\w-]+)\]\s*$", msg)
            flags[m.group(1) if m else "(none)"] += 1
        out.append("%d distinct warning location(s) in the header (informational, do not fail the check): %s" % (
            len(warnA), ", ".join("%s x%d" % kv for kv in flags.most_common())))
        if show_warnings:
            for t in warnA:
                out.append("  " + t[2])

    print("\n".join(out))
    if errA or errB:
        return 1
    if errC:
        return 2
    return 0


sys.exit(main())
