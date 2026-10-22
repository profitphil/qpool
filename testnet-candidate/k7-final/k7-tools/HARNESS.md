# QPOOL compile-check harness

Two tools. Run both on every candidate header; neither replaces the other.

```
tools/check_contract.sh [--compile-only] [--warnings] [--keep] <header>   # compiles, links, runs it as a Qubic contract
python3 -I tools/lint_contract.py [--quiet] [--prefix NAME] <header>      # static scan for forbidden constructs
tools/negative/selftest.sh                                                 # proves both tools can FAIL (about 30 s)
```

## check_contract.sh

| exit | last line | meaning |
|---|---|---|
| 0 | `PASS` | every stage that ran succeeded (the stages are listed above it) |
| 1 | `FAIL` | the header is at fault; errors are printed as `header-path:line:col`, core scaffolding errors are suppressed |
| 2 | `ERROR` | only core/scaffolding errors, or the harness broke: the header was NOT judged (not a pass) |
| 3 | - | usage |

Stages (on a patched scratch copy of the core, `core-scratch/`; the owner's core checkout is only read):

1. **compile** - the header is copied into a per-invocation overlay as `src/contracts/QPool.h` (struct `QPOOL` / `QPOOL2`, contract index 30, wired in the scratch `contract_def.h`). `tools/probe_qpool.cpp` includes the core's `test/contract_testing.h` and instantiates the contract the way the core's own tests do (`INIT_CONTRACT` registers every function and procedure, so every QPI template the contract uses is instantiated; `INITIALIZE` is called). Real code generation (`-c -O0`, not `-fsyntax-only`), `-std=c++20 -target x86_64-apple-macos13 -fshort-wchar -Wall -Wextra -Wpedantic -Wcast-align`. About 2 s.
2. **link** - x86_64 binary with gtest. About 4 s.
3. **smoke run** (skipped with `--compile-only`) - under Rosetta (`arch -x86_64`): `INITIALIZE`, then every registered user function once with an all-zero input of its registered size. It proves the contract starts and that reading empty state does not crash or abort. It checks no results. About 5 s, about 1.4 GB RAM, so smoke runs are serialised machine-wide by a lock; compiles run in parallel.

Safe to run several times at once: each run uses its own `work/check_contract/chk.XXXXXX` directory (deleted on exit, `--keep` to keep it; a smoke run leaves about 270 MB there while it lives). The scratch core and the gtest library are read-only after their one-time creation. Verified with three concurrent runs.

Warnings from the header are summarised (count per flag) and never change the exit code; `--warnings` lists them. `-Wunused-parameter` and `-Wcast-function-type-mismatch` are switched off because the QPI macros trigger them in every contract. The base header has 44 warning locations (`-Wsign-compare` x30, `-Wdeprecated-anon-enum-enum-conversion` x14).

### What PASS does and does not prove
- It proves: the header compiles and links with Apple clang 21 against this core's QPI, QX and QSWAP, and starts.
- It does NOT prove: MSVC compatibility (the real core build); the official `qubic-contract-verify` rules (a header using `[]` or `%` still compiles under clang, which is what `lint_contract.py` is for); behaviour of any procedure (no procedure is run); anything about mainnet, since the local core is older than mainnet in unknown ways (QX, QSWAP and the IPO code are the parts QPOOL uses).
- Only QX (index 1) and QSWAP (index 13) are called by the header; QPAYHUB (29) is only a transfer target, so it does not need to exist in the scratch core.

## The scratch core (how the portability problem was solved)
The core's tests are written for MSVC and do not compile on LP64 clang. `tools/setup_scratch.sh` (run automatically on first use, `--force` to rebuild) copies `src`, `test`, `lib`, `cmake` from `/Volumes/data/Github/core` (override: `QPOOL_CORE_SRC`) without `.git` and build output, converts CRLF to LF, then `tools/patch_core.py` applies 17 mechanical edits, each with an expected match count so a changed core stops the script instead of half-patching: `CHAR16` becomes `wchar_t` (3 files, 13 `L"..."` arrays), a `long`-size static_assert and one cast in `concurrency.h`, `0i64` literals, a vector `==` in `four_q.h`, an anonymous-union struct, a `const auto&` in `qpi_proposals_impl.h`, 13 missing `typename` in `qpi_oracle_impl.h`, and five test-support edits (`oracle_testing.h` x3, `test_util.h`, `stdlib_impl.cpp`). `tools/qpool_compat.h` is force-included and supplies the MSVC intrinsics (`_umul128`, `__shiftright128`, the `_Interlocked*` family on `__sync` builtins, `__cpuid`, `_wfopen_s`, `wcstombs_s`, `_cdecl`). `tools/qpool_link_stubs.cpp` replaces the Microsoft-ABI `SetJump`/`LongJump`. The scratch core is 7.9 MB, gtest 2.3 MB. If the setup script is rebuilt and the diff against `core-scratch/` is empty except `.qpool_scratch_ok`, nothing was changed by hand (checked once).
Errors raised inside core code are attributed to the header when any note of the diagnostic chain (for example "in instantiation of ... requested here") points into it, and then reported at the header line.

## lint_contract.py
Token-level scan (comments dropped, string contents never scanned for other rules). Rules L01-L15 are listed by `--rules`: `#`, `[ ]`, `%`, `/`, float, pointers (declaration, cast, dereference, `->`, `&x`, nullptr/new), local variables and for-indices outside `*_locals`, string/char literals, `...`, `__`, `union`/`QpiContext`/`const_cast`, `std::` and global `::`, global variables / typedef / using / names without the contract prefix, static variables, try/catch/throw (the last two groups marked `[extra]`). Source of the rules: `core/doc/contracts.md` section "Restrictions of C++ Language Features". Exit 1 on any hit.

Calibration: run over the 41 headers in `core/src/contracts/`. 32 pass. The 9 others were read one by one and are rule matches, not heuristic noise: a prefix that is the short name instead of the struct name (`QTF_`, `QRP_`, `IE_`; `--prefix` accepts them), string literals in `STATIC_ASSERT` (Pulse, IEscrow), `#if 0` (QUtil), and the non-contract helper files `math_lib.h` and `TestExampleA/B/C.h`. It also passes the base, the candidate and the repo's current `QPool.h`. It is a heuristic, not a C++ parser, and is not the official verifier (not installed here); the official one may flag more.

## Verification record (this session, Apple clang 21.0.0, macOS 26 / Darwin 25.6, Rosetta)
Run by `tools/negative/selftest.sh` (all expectations held):
- base `base/base_K7.h`: lint PASS; check PASS (compile, link, smoke: INITIALIZE + 9 functions).
- `negative/base_type_error.h` (one struct assigned to a uint64 in GetStats): check FAIL, one error at the injected line.
- `negative/base_template_error.h` (`Array<uint64, 3>` member): check FAIL, reported at header line 500 with the core's static_assert text.
- `negative/base_smoke_crash.h` (null write in GetStats): compile and link PASS, smoke run FAIL (SIGSEGV); `--compile-only` PASS, which shows what compile-only does not see.
- `negative/base_lint_violations.h`: lint FAIL, 18 hits across 11 rules.
- `review/candidate_QPool.h` (for comparison): check PASS, lint PASS.

## Known limits
- Header-attributable linker errors (an unimplemented QPI function) are recognised only if the undefined symbol names QPOOL; that path was never triggered here and is untested.
- A macro defined in the header that breaks core code compiled after it gives `ERROR` (exit 2), not `FAIL`; the script then points at preprocessor lines. Tested once.
- The smoke run exercises read-only functions with zero input only. Money logic is covered by the repo's gtest suite (`contract_qpool.cpp`), which this harness does not run.
- -O0 only; flow-based warnings that appear at -O2 are not seen.
- The tools directory is shared with other agents' scripts (`release_gate.py`, `test_tools.sh`, ...); this harness only owns the files named above.
