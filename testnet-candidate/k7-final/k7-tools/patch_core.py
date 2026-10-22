#!/usr/bin/env python3
"""patch_core.py <scratch-core-dir>

Applies the mechanical portability edits that let the Qubic core headers compile with Apple clang
(x86_64 target, LP64) to a SCRATCH COPY of the core. Never run this on a real checkout.

Each edit is (file, regex, replacement, expected number of matches). If the number of matches differs
the script stops, so a change in the core is noticed instead of silently producing a half-patched tree.
Line endings of every touched source file are first normalised from CRLF to LF.
"""
import os
import re
import sys

root = sys.argv[1]

EDITS = [
    # 1. CHAR16 is `unsigned short` in the core, but L"..." literals are wchar_t in clang (MSVC treats the two as one type).
    #    With -fshort-wchar wchar_t is 2 bytes wide, so making CHAR16 a wchar_t keeps the size and makes every
    #    appendText(dst, L"...") call resolve.
    ("src/platform/common_types.h", r"typedef unsigned short CHAR16;", "typedef wchar_t CHAR16;", 1),
    ("lib/platform_efi/uefi.h", r"(?m)^typedef unsigned short CHAR16;", "typedef wchar_t CHAR16;", 1),
    ("lib/platform_common/edk2_mdepkg/Include/X64/ProcessorBind.h", r"(?m)^typedef unsigned short      CHAR16;", "typedef wchar_t      CHAR16;", 2),
    ("src/public_settings.h", r"(?m)^static unsigned short (\w+)\[\] = L", r"static wchar_t \1[] = L", 13),
    ("src/platform/file_io.h", r"(?m)^static void addEpochToFileName\(unsigned short\* filename", "static void addEpochToFileName(CHAR16* filename", 1),
    # 2. concurrency.h assumes a 4-byte long (LLP64). The shims in qpool_compat.h are LP64-correct instead.
    ("src/platform/concurrency.h", r'static_assert\(sizeof\(long\) == 4, "Size of long for _InterlockedExchange is 4 bytes"\);', "", 1),
    ("src/platform/concurrency.h", r"\(volatile long\*\)&target", "(volatile int*)&target", 1),
    # 3. MSVC-only constructs.
    ("src/contracts/QReservePool.h", r"\b0i64\b", "0LL", 2),
    ("src/four_q.h", r"(?m)^    return \*\(\(__m256i\*\)A\) == \*\(\(__m256i\*\)signature\);",
     "    return _mm256_movemask_epi8(_mm256_cmpeq_epi8(*((__m256i*)A), *((__m256i*)signature))) == -1;", 1),
    ("src/contract_core/contract_action_tracker.h",
     r"(?s)    union\n    \{\n        struct QuTransfer\n        \{(.*?)\n        \} quTransfer;\n    \};",
     r"    struct QuTransfer\n    {\1\n    } quTransfer;", 1),
    # 4. Dependent-name / constant-expression strictness of clang.
    ("src/qpi/impl/qpi_proposals_impl.h",
     r"const ProposalWithAllVoteData<ProposalDataType, pv\.maxVotes>& p = pv\.proposals\[proposalIndex\];",
     "const auto& p = pv.proposals[proposalIndex];", 1),
    ("src/qpi/impl/qpi_oracle_impl.h", r"(?<!typename )OracleInterface::(OracleQuery|OracleReply)", r"typename OracleInterface::\1", 13),
    ("test/oracle_testing.h", r"\(const OracleInterface::OracleQuery\*\)", "(const typename OracleInterface::OracleQuery*)", 1),
    ("test/oracle_testing.h", r"sizeof\(OracleInterface::OracleQuery\)", "sizeof(typename OracleInterface::OracleQuery)", 1),
    ("test/oracle_testing.h", r"(?m)^static void enqueueResponse\(", "inline void enqueueResponse(", 1),
    ("test/stdlib_impl.cpp", r"ms\(unsigned char\(tm->tm_year % 100\)", "ms((unsigned char)(tm->tm_year % 100)", 1),
    ("test/test_util.h", r"#pragma once\n", "#pragma once\n#include <iomanip>\n", 1),
]


def normalise_line_endings():
    n = 0
    for sub in ("src", "lib", "test"):
        for dp, dn, fn in os.walk(os.path.join(root, sub)):
            for f in fn:
                if f.endswith((".h", ".hpp", ".cpp", ".c", ".txt", ".cmake")):
                    p = os.path.join(dp, f)
                    with open(p, "rb") as fh:
                        b = fh.read()
                    if b"\r\n" in b:
                        with open(p, "wb") as fh:
                            fh.write(b.replace(b"\r\n", b"\n"))
                        n += 1
    return n


def main():
    print("normalised CRLF -> LF in %d files" % normalise_line_endings())
    bad = 0
    for rel, rx, repl, expect in EDITS:
        p = os.path.join(root, rel)
        with open(p, "r", encoding="utf-8", errors="surrogateescape", newline="") as fh:
            s = fh.read()
        new, n = re.subn(rx, repl, s, flags=0)
        if n != expect:
            print("PATCH MISMATCH: %s: %r matched %d times, expected %d" % (rel, rx[:60], n, expect))
            bad += 1
            continue
        with open(p, "w", encoding="utf-8", errors="surrogateescape", newline="") as fh:
            fh.write(new)
    if bad:
        print("%d patch(es) did not apply: the core has changed since this script was written." % bad)
        sys.exit(1)
    print("applied %d edits" % len(EDITS))


main()
