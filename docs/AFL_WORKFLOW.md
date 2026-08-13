# AFL Workflow

This project uses AFL++ to fuzz APK-processing workflows. The current setup has two smoke-test modes: a direct Python APK harness mode and a custom-mutator path-file mode.

## Components

`afl-fuzz` is the AFL++ driver. It mutates inputs, runs a target program, records coverage, and writes run metadata such as `fuzzer_stats`.

`py-AFL` lets Python harnesses cooperate with AFL++ forkserver/instrumentation. In this repo, the Python harnesses try to import `afl` and call `afl.init()`, but still run as normal Python scripts if py-AFL is unavailable.

`build/cm-ApkVulFuzz.so` is the AFL++ custom mutator. It receives path-file seeds, loads the APK named inside each path file, dynamically locates the compressed `AndroidManifest.xml` data in the APK ZIP structure, and mutates only that manifest byte range.

## Smoke Modes

### Mode 1: Python APK Harness Mode

```text
input = real APK files
target = afl/apk_zip_harness.py
```

This mode points AFL++ directly at APK files in `Evaluation-SSBSE-2026/apk`. The harness opens each AFL input as a ZIP/APK, checks for `AndroidManifest.xml`, and lightly touches common APK entries such as `classes.dex` and `resources.arsc`.

Run it with:

```bash
bash scripts/run-py-afl-smoke.sh
bash scripts/run-py-afl-smoke.sh 30
```

The output directory is `afl-smoke-output`.

### Mode 2: Custom Mutator Path-File Mode

```text
input = .txt files containing APK paths
AFL loads build/cm-ApkVulFuzz.so
mutator loads the APK
mutator dynamically finds AndroidManifest.xml
mutator flips bits only in the manifest region
harness/DroidBot receives the mutated APK path
```

This mode uses path-file seeds from `Evaluation-SSBSE-2026/input`. Each `.txt` file contains an absolute path to a real APK. AFL++ passes the path-file as `@@`; the custom mutator reads the APK path inside it, writes a mutated APK, and returns a path to that mutated APK. The smoke harness `afl/apk_pathfile_harness.py` then opens the path it receives and checks the APK as a ZIP.

Run it with:

```bash
bash scripts/run-custom-mutator-smoke.sh
bash scripts/run-custom-mutator-smoke.sh 30
```

The output directory is `afl-custom-smoke-output`.

## Compile The Mutator

By default the compile script expects AFL++ source at `$HOME/AFLplusplus`. Override that with `AFLPP_DIR` when needed.

```bash
cd src
bash compile_afl_mutators.sh

# or
AFLPP_DIR=/path/to/AFLplusplus bash compile_afl_mutators.sh
```

The shared object is copied to:

```text
build/cm-ApkVulFuzz.so
```

Check the required AFL++ custom-mutator exports with:

```bash
nm -D ../build/cm-ApkVulFuzz.so | grep afl_custom
```

Expected symbols include `afl_custom_init`, `afl_custom_fuzz`, and `afl_custom_deinit`.

## Set Up The Corpus

Run:

```bash
bash scripts/setup-evaluation-corpus.sh
```

The script creates:

```text
Evaluation-SSBSE-2026/apk
Evaluation-SSBSE-2026/input
```

It extracts the local professor corpus tarball if present, can download it with `--download`, copies APK files into the APK folder, regenerates path-file `.txt` inputs with absolute APK paths, and validates that those paths exist.

## DroidBot Path Files

For full DroidBot fuzzing, AFL passes `@@` as a path-file, not as a raw APK. DroidBot therefore needs `-aa <path-file>` support:

```bash
python3 /home/sahar/Projects/droidbot/start.py -aa @@ -d emulator-5554 ...
```

`-aa` reads the APK path from the file passed by AFL and then uses that APK path exactly like `-a <apk>`.

## Known Limitations

Mutating compressed APK bytes can break ZIP CRCs or other ZIP metadata. The current mutator is useful for demonstrating targeted manifest-region mutation, but a production/evaluation run may need ZIP metadata repair, APK rebuilding, or signing-aware mutation to keep more generated APKs installable.

## Fixed Issue

The bitflip mutator must not reuse the manifest start offset as a loop counter. The fixed logic stores `start = data->i` and `end = data->j`, validates the range, and uses a separate loop counter while choosing offsets only inside `[start, end)`.
