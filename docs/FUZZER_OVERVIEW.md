# ApkVulFuzz — Fuzzer Overview & Operation Guide

A complete explanation of the ApkVulFuzz fuzzing pipeline: what it does, how each
part works, the tools it builds on, how to run it, and the key findings. Written
to be read top-to-bottom and to serve as the basis for a talk.

---

## 1. What this project does (the research goal)

Android APK files are ZIP archives. A normal fuzzer mutates a file by flipping
random bytes anywhere in it — but for an APK that mostly produces garbage that
fails the very first ZIP check, so almost no interesting behavior is ever
reached. **ApkVulFuzz is a *structure-aware* (a.k.a. format-aware) fuzzer for
APKs.** Instead of mutating blindly, it:

1. understands the APK/ZIP layout,
2. locates a *specific, interesting* region inside the archive — primarily the
   compressed `AndroidManifest.xml` entry — and
3. mutates **only that region**, so the archive stays structurally plausible and
   the mutation actually stresses the manifest/ZIP parsing code paths.

The goal is to find crashes, hangs, parsing failures, and tool bugs in the
software that reads APKs (ZIP tools, Android's manifest parser, DroidBot, the
emulator), using far fewer wasted executions than blind mutation.

---

## 2. Background: the tools the fuzzer builds on

| Tool | Role in this project |
|---|---|
| **AFL++ (`afl-fuzz`)** | The fuzzing *engine*. A coverage-guided greybox fuzzer that mutates inputs, runs a target program, records which code paths executed, and keeps inputs that reach new code. We use AFL++ 4.09c. |
| **AFL++ custom-mutator API** | AFL++ lets you replace its built-in byte mutations with your own C code compiled to a shared object (`.so`). We implement `afl_custom_init` / `afl_custom_fuzz` / `afl_custom_deinit`. This is where all the APK-awareness lives. |
| **py-AFL (`python-afl`)** | Lets a **Python** program act as an AFL target: the harness calls `afl.init()` and AFL++ then sees the Python code's coverage. Used by our smoke harnesses. |
| **Clang** | Compiles the custom mutator `.so` against the AFL++ headers. |
| **ZIP/APK format knowledge** | Hand-written ZIP parser (no external library) that walks the End-of-Central-Directory record, the central directory, and local file headers to find the manifest's exact byte range. |
| **DroidBot + Android emulator (SDK, `adb`, `aapt`)** | Dynamic execution: install and drive a (mutated) APK on an emulator to observe runtime behavior. Integrated via a `-aa` path-file option; full emulator runs are the next stage. |
| **External ZIP/APK tools** (`unzip`, `zipinfo`, `7z`, Python `zipfile`, `file`, `aapt`/`apkanalyzer`, optionally WinRAR) | *Differential testing*: run each mutated APK through many independent tools and look for disagreements, crashes, or segfaults. |

**One-line framing for a slide:** *AFL++ is the engine; our custom mutator is the
brain that makes it APK-aware; py-AFL, DroidBot/emulator, and the ZIP toolset are
the ways we execute and judge the mutated APKs.*

---

## 3. How it works — end-to-end architecture

### 3.1 The key design idea: "path-file" indirection

A subtle but important design choice: **AFL's seed inputs are not APKs.** Each
seed is a tiny text file (`.txt`) whose contents are the absolute path to a real
APK. The custom mutator reads that path, loads the real APK, mutates it, writes
the mutated APK to disk, and hands **the path of the mutated APK** back to AFL,
which passes it to the target harness. The harness opens that path as a ZIP.

Why do it this way? Because APKs are large (10–20 MB). Routing multi-megabyte
files through AFL's own input machinery every iteration is wasteful; passing a
short path string and doing the heavy APK work inside the mutator is cheaper. (It
also has an important consequence for coverage — see §9.)

### 3.2 Data flow (one fuzzing iteration)

```
  Evaluation-SSBSE-2026/input/F-Droid.txt        (seed = a path to an APK)
        │  contents: "/…/Evaluation-SSBSE-2026/apk/F-Droid.apk"
        ▼
  afl-fuzz  ──calls──►  afl_custom_fuzz()  in  cm-ApkVulFuzz2.so
                              │
                              │ 1. read the APK path from the seed text
                              │ 2. load the real APK bytes
                              │ 3. parse the ZIP: find AndroidManifest.xml's
                              │    exact byte range (EOCD → central dir → local header)
                              │ 4. flip bits inside the chosen region (per strategy)
                              │ 5. write the mutated APK to a scratch dir
                              │ 6. return the mutant's PATH to AFL
                              ▼
  afl-fuzz  writes that path into the file it passes as @@
        │
        ▼
  target harness:  python3 afl/apk_pathfile_harness.py @@
        │  reads the path, opens the mutated APK as a ZIP,
        │  touches AndroidManifest.xml / classes.dex / resources.arsc
        ▼
  AFL records coverage, decides whether the input is "interesting",
  and saves crashes / hangs.
```

### 3.3 Dynamic manifest location (no hardcoded offsets)

The heart of the "APK-aware" claim is that the mutator finds the manifest
*dynamically* from the ZIP structure, exactly like a real ZIP reader would:

1. Scan backwards from the end of the file for the **End-Of-Central-Directory
   (EOCD)** signature `0x06054b50`.
2. From the EOCD, read the offset and size of the **central directory**.
3. Walk the central-directory entries (signature `0x02014b50`) until the one
   named `AndroidManifest.xml`.
4. From that entry, jump to its **local file header** (`0x04034b50`), skip the
   header + file name + extra field, and compute the exact `[start, end)` byte
   range of the entry's **compressed** data.

The mutation then happens strictly inside that computed range. Because the
manifest is stored *compressed* (DEFLATE), flipping bytes there corrupts the
compressed stream and its CRC-32 — which is precisely the kind of malformed-but-
structurally-valid input that exercises error handling in manifest/ZIP parsers.

---

## 4. The two mutators

The project ships **two** custom mutators, built as two separate shared objects.
You pick which one AFL loads at run time.

**V1 — baseline (`build/cm-ApkVulFuzz.so`)**
The original mutator. One behavior: locate the manifest and flip a random number
(1–50) of random bits across the whole compressed manifest region.

**V2 — multi-strategy (`build/cm-ApkVulFuzz2.so`)**
A superset, kept in a separate `src/custom_mutators/v2/` folder so the baseline is
never touched. V2 adds:
- **10 selectable strategies** (`APKFUZZ_MUTATOR_MODE`), see §5;
- **configurable region and intensity** (`APKFUZZ_SEGMENT_OFFSET/SIZE`, `APKFUZZ_MAX_FLIPS`);
- **per-iteration logging** (`APKFUZZ_LOG`) — which APK, which region, how many flips;
- **clean output handling** (`APKFUZZ_OUT_DIR` scratch dir, `APKFUZZ_SAVE_DIR` for kept specimens);
- **hardened file writes** (short-write / close-failure detection).

With no environment set, V2 reproduces the V1 default behavior, so it can double
as an *instrumented* build for verification.

---

## 5. Mutation strategies (V2)

Selected with `APKFUZZ_MUTATOR_MODE=<name>`.

| Strategy | Region mutated | What it stresses |
|---|---|---|
| `manifest-bitflip` *(default)* | whole compressed `AndroidManifest.xml` | the Android binary-XML (AXML) manifest parser |
| `manifest-segment` | `[start+OFFSET, start+OFFSET+SIZE)` inside the manifest | a precise, localized part of the manifest |
| `manifest-start` / `-middle` / `-end` | first / middle / last third of the manifest | which part of the manifest trips the parser |
| `zip-local-header` | the manifest's **local file header** | the ZIP local-header reader |
| `zip-central-directory` | the whole **central directory** | the ZIP directory reader (often makes the file unopenable) |
| `crc-fields` | the manifest's CRC-32 fields (local + central copies) | CRC validation logic in ZIP tools |
| `size-fields` | the manifest's compressed/uncompressed size fields | size/bounds handling in ZIP tools |
| `random-range` | anywhere in the file (or `[OFFSET, OFFSET+SIZE)`) | a blind baseline to compare "smart" modes against |

Each strategy tends to produce a **different failure class** (see §10), which is
what makes comparing them meaningful.

---

## 6. Key files

```
src/custom_mutators/
  cm_ApkVulFuzz.c, bitflip.c, apk.c      V1 baseline mutator (frozen)
  compile via src/compile_afl_mutators.sh  → build/cm-ApkVulFuzz.so
  v2/
    zip2.c/.h        extended ZIP analyzer (manifest range + header/CRC/size/central-dir offsets)
    strategies.c/.h  the 10 strategies, region selection, logging, env config
    cm_ApkVulFuzz2.c AFL glue (afl_custom_*) + standalone tester
    README.md        modes + env-var reference
  compile via src/compile_afl_mutator_v2.sh → build/cm-ApkVulFuzz2.so

afl/
  apk_pathfile_harness.py   target for the custom-mutator workflow (reads a path-file → opens the APK)
  apk_zip_harness.py        target for the direct py-AFL workflow (AFL feeds raw APKs)

scripts/
  setup-evaluation-corpus.sh   fetch/lay out the corpus + generate path-files
  validate-corpus.py           deep-validate each APK + regenerate path-files (--write-inputs)
  run-py-afl-smoke.sh          quick py-AFL smoke over real APKs
  run-custom-mutator-smoke.sh  quick smoke with the V1 custom mutator
  run-corpus-smoke.sh          smoke over the whole corpus with a chosen mutator+mode (+multi-APK evidence)
  triage-afl-output.py         summarize an AFL output dir (crashes/hangs/coverage/seed diversity)
  test-apk-tools.py            run mutants through external ZIP/APK tools + classify + preserve crashers
  compare-mutators.py          aggregate several runs into one comparison table

docs/
  FUZZER_OVERVIEW.md       this document
  AFL_WORKFLOW.md          the AFL/py-AFL/custom-mutator workflow
  VERIFICATION_RUNBOOK.md  exact commands to verify the workflow (build/mutator-only/multi-APK)
  MUTATOR_COMPARISON.md    methodology for comparing strategies

Evaluation-SSBSE-2026/
  apk/     the seed APKs
  input/   the .txt path-files (one per APK; machine-specific absolute paths)
```

---

## 7. How to run it (step by step)

All commands run from the project root on the machine that has AFL++ installed.

### 7.0 Prerequisites (once)
- AFL++ installed (`afl-fuzz` on `PATH`) and its source at `~/AFLplusplus`.
- `clang`, Python 3 with a virtualenv at `.venv` (py-AFL installed there).
- For dynamic testing: Android SDK + emulator + DroidBot (see the top-level `README.md`).

### 7.1 Build the mutators
```bash
cd src
AFLPP_DIR=~/AFLplusplus bash compile_afl_mutators.sh      # V1  → build/cm-ApkVulFuzz.so
AFLPP_DIR=~/AFLplusplus bash compile_afl_mutator_v2.sh    # V2  → build/cm-ApkVulFuzz2.so
cd ..
nm -D build/cm-ApkVulFuzz2.so | grep afl_custom           # expect init/fuzz/deinit
```

### 7.2 Prepare & validate the corpus
```bash
bash scripts/setup-evaluation-corpus.sh          # lay out apk/ and input/
python3 scripts/validate-corpus.py --write-inputs # check each APK + (re)generate path-files
```
`validate-corpus.py` confirms every APK opens as a ZIP, contains a locatable
`AndroidManifest.xml`, and flags duplicate contents.

### 7.3 Smoke test (seconds — proves the plumbing works)
```bash
# whole corpus, V2 mutator, default strategy, 30-second run:
bash scripts/run-corpus-smoke.sh 30 build/cm-ApkVulFuzz2.so manifest-bitflip
```
This validates the corpus, runs AFL with **only** our mutator, prints
`fuzzer_stats`, and lists which APK seeds were actually mutated. It **fails
loudly** if AFL aborts.

### 7.4 A longer campaign
Same command with a longer duration, or (recommended) one AFL instance per APK so
every APK is exercised (see §9). Keep specimens for later analysis:
```bash
export APKFUZZ_SAVE_DIR=mutants-run1
bash scripts/run-corpus-smoke.sh 3600 build/cm-ApkVulFuzz2.so manifest-bitflip
```

### 7.5 Analyze the results
```bash
# summarize the AFL output (crashes, hangs, coverage, which seeds were used):
python3 scripts/triage-afl-output.py afl-corpus-smoke-*/

# run every saved mutant through external tools and classify + preserve crashers:
python3 scripts/test-apk-tools.py mutants-run1 --preserve crashers-run1 --report tools-run1.csv
```

### 7.6 Compare strategies
Run several modes (each into its own dir), then:
```bash
python3 scripts/compare-mutators.py \
  bitflip=cmp-manifest-bitflip central=cmp-zip-central-directory crc=cmp-crc-fields \
  --tools bitflip=tools-bitflip.csv --tools central=tools-central.csv --csv comparison.csv
```

---

## 8. Configuration reference (environment variables)

**AFL++ side:**
| Variable | Meaning |
|---|---|
| `AFL_CUSTOM_MUTATOR_LIBRARY` | path to the mutator `.so` to load |
| `AFL_CUSTOM_MUTATOR_ONLY=1` | use **only** our mutator — disables AFL's own havoc/deterministic stages |

**Our mutator (V2):**
| Variable | Meaning | Default |
|---|---|---|
| `APKFUZZ_MUTATOR_MODE` | which strategy (see §5) | `manifest-bitflip` |
| `APKFUZZ_SEGMENT_OFFSET` | region offset (relative to manifest start; absolute for `random-range`) | 0 |
| `APKFUZZ_SEGMENT_SIZE` | region size in bytes | manifest length / rest of file |
| `APKFUZZ_MAX_FLIPS` | fixed number of bit-flips | random 1–50 |
| `APKFUZZ_LOG` / `APKFUZZ_LOG_FILE` | append a structured line per iteration / where | off / `apkfuzz_mutator.log` |
| `APKFUZZ_OUT_DIR` | write transient mutants here (keeps the corpus clean) | next to the seed |
| `APKFUZZ_SAVE_DIR` | also keep a copy of every mutant here for analysis | off |

---

## 9. Key research finding: does AFL fuzz *all* the APKs?

This is the most important intellectual point of the project, and it was
**confirmed empirically** by running a real `afl-fuzz` build.

**Finding:** AFL loads every APK seed into its queue, but it only keeps fuzzing a
seed if that seed reaches *new coverage* the others don't. Because of the
path-file design, **AFL's coverage feedback only sees the short path string, not
the APK's bytes.** So if the target harness does the same work regardless of which
APK it opens, AFL considers all but one seed *redundant* and fuzzes a single APK —
even for a long time.

The three controlled runs:

| Run | Seeds | Does the target's coverage depend on APK content? | Result |
|---|---|---|---|
| A | 4 real 12–20 MB APKs | no | fuzzed only 1 APK (slow execs, never advanced) |
| B | 4 tiny APKs, **87 completed cycles** | no | still only 1 APK — the other 3 culled as redundant |
| C | same 4 tiny APKs | **yes** | fuzzed **all 4** and discovered 12 new inputs |

**Consequence / how to actually fuzz all ~20 APKs:** the current harness is like
runs A/B, so for the real campaign use **one `afl-fuzz` instance per APK** (or
make the target's coverage depend on APK content, e.g. by fuzzing a real
instrumented parser). This is a genuine, defensible research insight about
coverage-guided fuzzing of container formats, not just an implementation detail.

---

## 10. What "a result" looks like (classification)

`test-apk-tools.py` runs each mutant through many tools and assigns a class
(worst wins):

| Class | Meaning |
|---|---|
| `VALID_ZIP` | opens and verifies cleanly (mutation didn't break structure) |
| `BAD_CRC` | opens, but an entry fails its CRC-32 check |
| `MANIFEST_PARSE_FAIL` | `AndroidManifest.xml` present but unreadable / bad CRC |
| `BROKEN_ZIP` | can't be opened as a ZIP at all |
| `NO_MANIFEST` | opens, but the manifest entry is gone |
| `INTERESTING_BEHAVIOR` | tools *disagree* (one says OK, another says broken) |
| `TOOL_HANG` | a tool exceeded the timeout |
| `TOOL_CRASH` / `SEGFAULT` | a tool died from a signal — **the bugs we most want**; the specimen is preserved |

For a typical manifest bit-flip, the mutant stays a `VALID_ZIP` but the manifest
fails CRC (`MANIFEST_PARSE_FAIL`) — showing the mutation landed exactly on the
manifest and nowhere else.

---

## 11. Limitations & open questions (be upfront about these)

- **Coverage vs. content (§9):** with the path-file harness, a single AFL instance
  won't fairly cover all APKs. Mitigation: one instance per APK.
- **Installability:** flipping *compressed* manifest bytes breaks the CRC, so most
  mutants won't install on a device. Reaching *install-time* / runtime bugs
  (DroidBot, emulator) would need a CRC-repair / re-zip step so mutants stay valid.
- **Corpus quality:** the current 6-APK corpus is really ~2 distinct apps
  (`flashlight`/`happymod`/`weather` are byte-identical); it should be expanded to
  ~20 distinct open-source APKs.
- **Format coverage:** the ZIP parser doesn't handle Zip64 (fine for normal APKs).
- **Signing:** mutated APKs are unsigned/invalid-signature; signing-aware mutation
  is future work if targeting the installer's signature verification.

---

## 12. Talking points for the professor (summary)

- We built a **structure-aware APK fuzzer** on top of **AFL++**, using AFL++'s
  **custom-mutator API** to mutate *only* the dynamically-located
  `AndroidManifest.xml` region rather than the whole file.
- The manifest is found **dynamically** from the ZIP structure (EOCD → central
  directory → local header) — **no hardcoded offsets**, so it works across
  different APKs.
- We provide **two mutators**: a frozen baseline and a **10-strategy** version
  (manifest sub-regions, ZIP local header, central directory, CRC/size fields,
  random baseline), selectable at run time, plus tooling to **classify** mutants
  against external ZIP/APK tools, **triage** AFL output, and **compare** strategies.
- We **verified the pipeline end-to-end** with a real AFL++ build: it compiles,
  AFL loads the mutator, and mutation is provably localized to the manifest (valid
  ZIP, manifest CRC failure).
- Our main research finding: **coverage-guided fuzzing of a container format via
  path-files does not automatically spread across all seeds** — AFL fixates on one
  APK unless coverage distinguishes them. We demonstrated this with three
  controlled runs and identified the fixes.
- **Open research decisions:** how to handle duplicate/near-identical APKs, whether
  ZIP/APK *repair* is needed after mutation to reach install/runtime bugs, and how
  a long campaign should evaluate coverage fairly across the full corpus.

---

## 13. Glossary

- **Greybox fuzzing:** fuzzing guided by lightweight coverage feedback (which code
  ran), between "blackbox" (no feedback) and "whitebox" (full program analysis).
- **Coverage / edges:** which basic-block transitions the target executed; AFL
  keeps inputs that hit new edges.
- **Queue:** AFL's evolving set of inputs worth mutating further.
- **Havoc / deterministic stages:** AFL's *built-in* mutation strategies — the ones
  we **disable** with `AFL_CUSTOM_MUTATOR_ONLY=1`.
- **Custom mutator:** a user `.so` implementing `afl_custom_*` that replaces AFL's
  built-in mutations.
- **Forkserver:** AFL's mechanism to fork the target quickly for each execution
  (py-AFL provides this for Python targets).
- **EOCD / central directory / local file header:** the three ZIP structures the
  parser walks to find an entry's exact byte range.
- **CRC-32:** the per-entry checksum in a ZIP; corrupting compressed bytes makes it
  mismatch, which is what most manifest mutations trigger.
```
