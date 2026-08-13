# ApkVulFuzz — My Update + Teammate Guide

Scope: only the work done since the last meeting, plus how a new teammate runs
and tests it. (Background on the overall project is the professor's; not repeated
here.)

---

## What I did since the last meeting (12 steps)

**Verified the existing AFL workflow**
1. Confirmed it **builds and runs end-to-end** — compiles against AFL++, exports the
   `afl_custom_*` hooks, and a real `afl-fuzz` run produces `fuzzer_stats` with no abort.
2. Confirmed AFL runs with **our custom mutator only** (`AFL_CUSTOM_MUTATOR_ONLY=1`
   disables AFL's built-in havoc/deterministic mutations).
3. Investigated **multi-APK behavior** and found (with real runs) that AFL **fixates on
   one APK** unless the target's coverage differs per APK — fix is one AFL instance per APK.

**Corpus + run/analysis tooling (new scripts)**
4. `validate-corpus.py` — validates each APK (opens, has a locatable manifest) + regenerates path-files; ready to scale to ~20 APKs.
5. `run-corpus-smoke.sh` — one command to smoke-test the whole corpus (and it fails loudly if AFL aborts).
6. `triage-afl-output.py` — summarizes an AFL run: crashes, hangs, coverage, which seeds were used.
7. `test-apk-tools.py` — runs mutated APKs through `unzip`/`zipinfo`/`7z`/Python `zipfile`/`file`/`aapt` and classifies each.
8. …with **tool crash / segfault / hang detection** and automatic preservation of any specimen that crashes a tool.

**New multi-strategy mutator (V2)**
9. **Configurable mutation region** — offset, segment size, and flip count.
10. **Split mutation into separate strategy files** (a new mutator, kept separate from the baseline).
11. **Runtime strategy selection** — `APKFUZZ_MUTATOR_MODE` picks one of **10** strategies.
12. **Mutator-comparison method** + `compare-mutators.py` to score which strategy is best.

> Still to do (roadmap step 6): the long ~12-hour campaign — deferred, I'll run that next.

---

## V1 vs V2 (the two mutators)

Both are AFL++ custom mutators that find `AndroidManifest.xml` dynamically and flip
bits inside it. The difference:

| | **V1** (`cm-ApkVulFuzz.so`) | **V2** (`cm-ApkVulFuzz2.so`) |
|---|---|---|
| Behavior | one fixed mode: flip 1–50 random bits across the whole manifest | same by default, **+ 9 more strategies** chosen at run time |
| Pick the target region | no | yes (manifest sub-parts, ZIP header, central directory, CRC/size fields, random) |
| Intensity / logging / kept mutants | no | yes (`APKFUZZ_MAX_FLIPS`, `APKFUZZ_LOG`, `APKFUZZ_SAVE_DIR`) |

**Use V1** as the simple baseline; **use V2** for experiments and comparisons.
With no env vars, V2 == V1. Pick the mutator with `AFL_CUSTOM_MUTATOR_LIBRARY`
and the V2 strategy with `APKFUZZ_MUTATOR_MODE`.

---

## How to operate it

From the project root, on a machine with AFL++ installed:
```bash
# 1. build both mutators
cd src
AFLPP_DIR=~/AFLplusplus bash compile_afl_mutators.sh       # V1 → build/cm-ApkVulFuzz.so
AFLPP_DIR=~/AFLplusplus bash compile_afl_mutator_v2.sh     # V2 → build/cm-ApkVulFuzz2.so
cd ..

# 2. validate + regenerate the corpus path-files
python3 scripts/validate-corpus.py --write-inputs

# 3. fuzz the whole corpus:  <seconds> <mutator.so> <strategy>
bash scripts/run-corpus-smoke.sh 30 build/cm-ApkVulFuzz2.so manifest-bitflip

# 4. (optional) keep mutants and analyze
export APKFUZZ_SAVE_DIR=mutants-run1
bash scripts/run-corpus-smoke.sh 300 build/cm-ApkVulFuzz2.so crc-fields
python3 scripts/triage-afl-output.py afl-corpus-smoke-*/
python3 scripts/test-apk-tools.py mutants-run1 --preserve crashers
```
V2 strategies: `manifest-bitflip`, `manifest-segment`, `manifest-start`,
`manifest-middle`, `manifest-end`, `zip-local-header`, `zip-central-directory`,
`crc-fields`, `size-fields`, `random-range`.

---

## How to test it (hand this to your teammate)

~2 minutes to confirm it works on your machine:
```bash
# A. mutator builds + exports the AFL hooks
cd src && AFLPP_DIR=~/AFLplusplus bash compile_afl_mutator_v2.sh && cd ..
nm -D build/cm-ApkVulFuzz2.so | grep afl_custom
#    EXPECT: afl_custom_init, afl_custom_fuzz, afl_custom_deinit

# B. corpus is valid
python3 scripts/validate-corpus.py
#    EXPECT: every APK "USABLE" with a manifest_range

# C. a 1-second smoke runs AFL with our mutator
bash scripts/run-corpus-smoke.sh 1 build/cm-ApkVulFuzz2.so manifest-bitflip
#    EXPECT: prints fuzzer_stats (execs_done > 0); AFL says the custom mutator loaded;
#            does NOT print "Smoke test FAILED"
```
It's broken if: `nm` shows no `afl_custom_*` symbols (bad build); `validate-corpus.py`
marks an APK `BAD` (bad ZIP / no manifest); or the smoke prints **`Smoke test FAILED`**
/ no `fuzzer_stats` (AFL aborted — usually a missing/bad mutator, bad seeds, or a
broken harness).

No-AFL sanity check of a single mutation:
```bash
APKFUZZ_MUTATOR_MODE=manifest-bitflip build/mut2-standalone Evaluation-SSBSE-2026/apk/F-Droid.apk
#    EXPECT: prints the path of a newly created mutant APK
```
