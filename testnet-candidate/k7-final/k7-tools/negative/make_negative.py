#!/usr/bin/env python3
"""make_negative.py <base-header> [<out-dir>]

Writes deliberately broken copies of the base header next to this script (or into <out-dir>). Each file differs from the base
by a few inserted lines, so a failing check can only be caused by those lines:

  base_type_error.h         ONE genuine type error in GetStats (assign a struct to a uint64)      -> check_contract.sh must FAIL at that line
  base_template_error.h     Array<uint64, 3> member (static_assert inside the core's Array)       -> must FAIL, reported at the header line that instantiated it
  base_smoke_crash.h        compiles, but GetStats writes through a null pointer                   -> compile + link PASS, smoke run must FAIL
  base_lint_violations.h    one forbidden construct per lint rule inside GetStats                  -> lint_contract.py must FAIL
"""
import os
import sys

base = sys.argv[1]
out = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.abspath(__file__))
src = open(base).read()

ANCHOR = "    PUBLIC_FUNCTION(GetStats)\n    {\n"
assert src.count(ANCHOR) == 1, "GetStats anchor not found exactly once"


def inject(name, lines, extra=None):
    s = src.replace(ANCHOR, ANCHOR + "".join("        " + l + "\n" for l in lines), 1)
    if extra:
        s = extra(s)
    with open(os.path.join(out, name), "w") as fh:
        fh.write(s)
    print("wrote", os.path.join(out, name))


inject("base_type_error.h", ["output.poolCount = output;   // NEGATIVE TEST: struct assigned to uint64"])

assert src.count("struct QPOOL2 {};\n") == 1
with open(os.path.join(out, "base_template_error.h"), "w") as fh:
    fh.write(src.replace("struct QPOOL2 {};\n", "struct QPOOL2 { Array<uint64, 3> negativeTestBadCapacity; };   // NEGATIVE TEST: capacity must be a power of two\n", 1))
print("wrote", os.path.join(out, "base_template_error.h"))

inject("base_smoke_crash.h", ["*((uint64*)0) = 1;   // NEGATIVE TEST: null write, compiles fine, crashes when GetStats runs"])

inject("base_lint_violations.h", [
    "uint64 negLocal = 5;                       // L07 local variable",
    "output.poolCount = negLocal % 3;           // L03 modulo",
    "output.holdingCount = negLocal / 3;        // L04 division",
    "float negF = 1.5;                          // L05 float",
    "uint64* negP = nullptr;                    // L06 pointer",
    "output.proposalCount = *negP;              // L06 dereference",
    "output.totalDeployed = negLocal[2];        // L02 indexing",
    "auto negS = \"text\";                        // L08 string literal",
    "output.totalBurned = __negInternal;        // L10 double underscore",
    "output.qpayBuyPot = std::max(1, 2);        // L12 std::",
], extra=lambda s: "#include <cstdint>\n" + s.replace("using namespace QPI;\n",
        "using namespace QPI;\nuint64 negGlobalCounter = 0;   // L13 global variable\n", 1))
