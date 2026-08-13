# Verification Runbook (tasks 1, 2, 3)

Exact commands to run **on your machine** (they need `afl-fuzz`, your `.venv`,
and `~/AFLplusplus`, which the cloud assistant environment does not have). Where
useful, the expected output is shown.

## 0. Build both mutators

```bash
cd ~/Projects/ApkVulFuzz/src
AFLPP_DIR=~/AFLplusplus bash compile_afl_mutators.sh        # baseline  -> build/cm-ApkVulFuzz.so
AFLPP_DIR=~/AFLplusplus bash compile_afl_mutator_v2.sh      # v2/modes  -> build/cm-ApkVulFuzz2.so
cd ..
nm -D build/cm-ApkVulFuzz.so  | grep afl_custom
nm -D build/cm-ApkVulFuzz2.so | grep afl_custom
```
Expect `afl_custom_init`, `afl_custom_fuzz`, `afl_custom_deinit` for **both**.
(Already confirmed in the cloud against a real AFL++ checkout — this just
re-confirms on your toolchain.)

---

## Task 1 — is the current AFL workflow stable?

Build is verified above. For runtime stability, run a short bounded campaign and
confirm it reaches AFL's steady loop and writes stats (rather than aborting in
setup/dry-run):

```bash
bash scripts/setup-evaluation-corpus.sh          # ensure corpus + path-files
bash scripts/run-custom-mutator-smoke.sh 30       # 30s baseline smoke
cat afl-custom-smoke-output/default/fuzzer_stats | grep -E 'execs_done|stability|saved_crashes'
```
Stable looks like: `fuzzer_stats` exists, `execs_done` > 0, `stability` near
100%, no `PROGRAM ABORT` on screen.

---

## Task 2 — is AFL using ONLY our custom mutator (no havoc/deterministic)?

The smoke scripts already export `AFL_CUSTOM_MUTATOR_ONLY=1` and
`AFL_CUSTOM_MUTATOR_LIBRARY=...`. Three independent confirmations:

**a) Startup banner** — run without `AFL_NO_UI` once and read the header, or check setup:
```bash
grep -E 'AFL_CUSTOM_MUTATOR' afl-custom-smoke-output/default/fuzzer_setup
```
Expect `AFL_CUSTOM_MUTATOR_LIBRARY=.../cm-ApkVulFuzz*.so` to be present.

**b) Queue op tags** — with custom-mutator-only, new queue entries are produced
by the custom stage, not havoc/arith/deterministic. Match up to the next comma
(the tag contains capitals, `-`, and `.`, e.g. `op:cm-ApkVulFuzz2.so`, so a
`[a-z_]` character class would truncate it to `op:cm`):
```bash
ls afl-custom-smoke-output/default/queue | grep -oE 'op:[^,]+' | sort | uniq -c
```
Expect the tag to be your mutator's library basename, e.g. `op:cm-ApkVulFuzz2.so`
(confirmed in a real AFL++ 5.x run), and `op:havoc` to be **absent**. Contrast:
the plain py-AFL run in `afl/out` (no mutator-only) contains an
`id:000004,...,op:havoc,...` entry — exactly what should **not** appear when the
custom mutator is the only source of mutation. (Seed entries have no `op:` tag,
so they don't show up here.)

**c) It literally can't mutate without the .so** — sanity check that removing the
library changes behavior (AFL falls back to its own mutators / errors), proving
the .so is what's driving mutation.

> Note: `AFL_CUSTOM_MUTATOR_ONLY=1` disables AFL's own havoc/deterministic
> stages; the trimming stage may still run, which is expected and is not a
> "normal mutation strategy".

---

## Task 3 — does AFL cycle multiple APKs (not fixate on one)?

Use the v2 mutator with logging (default mode reproduces baseline behavior):

```bash
bash scripts/run-corpus-smoke.sh 30 build/cm-ApkVulFuzz2.so manifest-bitflip
```
The script prints, and you can re-derive, the multi-APK evidence:
```bash
LOG=$(ls -t afl-corpus-smoke-*/apkfuzz_mutator.log | head -1)
grep -oE 'src=[^ ]+' "$LOG" | sort | uniq -c      # how often each seed APK was mutated
```
This tells you *which* APKs AFL actually mutated. Do **not** assume every seed
appears: with the current harness you will typically see **one APK dominate** (or
be the only one) — that is expected, and the "Empirical result" section below
explains why and how to force breadth. Seeing more than one here is a bonus, not
the baseline. Also:
```bash
python3 scripts/triage-afl-output.py afl-corpus-smoke-*/
# -> "distinct seeds used: N -> F-Droid.apk, flashlight.apk, ..."
```

### Important nuance to tell the professor
In this design AFL's seeds are **path-files**, and the custom mutator returns the
path of the mutant it wrote — so **AFL's coverage feedback sees only the path
string, not the APK bytes**. Consequences:
- AFL cycles through the seed queue (one entry per APK) roughly round-robin, so
  it *does* exercise every APK — the `src=` log proves it.
- But it does **not** evolve APK content via coverage (each iteration reloads the
  pristine seed and re-randomizes the manifest region). The "smartness" lives in
  the custom mutator, not in AFL's queue evolution.
- Therefore "multi-APK" here means *breadth across seeds*, not *coverage-guided
  focus*. If you want coverage-guided evolution over APK bytes, that's a separate
  design change (feed real APK bytes through AFL, or instrument the ZIP/parse
  target) worth discussing.

### Empirical result — reproduced with a REAL afl-fuzz build

We built AFL++ from source and ran the actual custom-mutator workflow
(`AFL_CUSTOM_MUTATOR_ONLY=1`, `cm-ApkVulFuzz2.so`, `APKFUZZ_LOG=1`) against a
small instrumented target that stands in for the harness. Three runs:

| Run | Seeds | Target coverage depends on APK content? | Result (distinct `src=` in the mutator log) |
|---|---|---|---|
| A | 4 real 12–20 MB APKs, 60 s | no | **only `F-Droid`** (≈6 execs/s; never left seed #0) |
| B | 4 tiny APKs, 45 s, **87 cycles** | no | **only `app_A`** — B/C/D culled as redundant |
| C | same 4 tiny APKs, 45 s | **yes** | **all four** `app_A/B/C/D`; +12 new inputs, corpus 4→16 |

**Conclusion.** AFL loads every APK into the queue, but its scheduler fuzzes a
seed only if that seed exposes coverage the others don't. When the target/harness
does the same work regardless of APK content (runs A/B), AFL marks all but one
seed **redundant/non-favored** and fuzzes a single APK indefinitely — even across
dozens of completed cycles. When coverage is content-dependent (run C), AFL
fuzzes all APKs and discovers new inputs.

The current `apk_pathfile_harness.py` is close to the A/B case (it opens the APK
and touches the same entries regardless of content), so **expect AFL to fixate on
one APK** unless you change one of the following.

### How to actually fuzz all ~20 APKs (pick one)

1. **One afl-fuzz instance per APK** (simplest, recommended for the campaign):
   loop over seeds, run each in its own `-o` dir (or use AFL parallel `-M/-S`).
   Guarantees every APK gets fuzzed regardless of coverage.
2. **Make the harness coverage APK-distinguishing:** have the target actually
   parse/branch on manifest/ZIP content (or fuzz a real instrumented APK parser),
   so different APKs look different to AFL — then a single instance cycles them
   (proven in run C).
3. **Reduce favored-skipping** for a diagnostic run so non-favored seeds still get
   cycled (less efficient, but shows breadth).

### Quick check on YOUR machine (which regime are you in?)
```bash
bash scripts/run-corpus-smoke.sh 120 build/cm-ApkVulFuzz2.so manifest-bitflip
LOG=$(ls -t afl-corpus-smoke-*/apkfuzz_mutator.log | head -1)
grep -oE 'src=[^ ]+' "$LOG" | sed -E 's#.*/##' | sort | uniq -c
```
One APK dominating ⇒ you're in the A/B regime ⇒ use option 1 for the real campaign.

