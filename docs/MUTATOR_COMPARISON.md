# Mutator Strategy Comparison (task 13)

How to compare the mutation strategies and decide which is best for which phase
of the research. This is a **methodology + tooling** doc; the actual runs happen
on your machine (they need `afl-fuzz`).

## Strategies under test (`APKFUZZ_MUTATOR_MODE`)

| Mode | What it stresses | Typical output class |
|---|---|---|
| `manifest-bitflip` | AXML manifest parser (whole compressed entry) | valid ZIP, manifest bad CRC |
| `manifest-segment` / `-start` / `-middle` / `-end` | localized manifest regions | manifest bad CRC (targeted) |
| `zip-local-header` | ZIP local-header reader | broken local header |
| `zip-central-directory` | ZIP central-directory reader | often unopenable ZIP |
| `crc-fields` | CRC validation paths in ZIP tools | bad CRC |
| `size-fields` | size/bounds handling in ZIP tools | bad CRC / bounds |
| `random-range` | broad/blind baseline for contrast | mixed |

## Metrics

From `fuzzer_stats` (fuzzer-side):
- `execs_per_sec` — throughput (higher = cheaper strategy).
- `corpus_found` — new coverage inputs (limited value here; see the coverage
  caveat in VERIFICATION_RUNBOOK.md).
- `saved_crashes`, `saved_hangs` — target-side findings.

From `test-apk-tools.py` (downstream tool-side — the real signal for this project):
- rate of `BAD_CRC`, `MANIFEST_PARSE_FAIL`, `BROKEN_ZIP` (how effectively the
  mode breaks each layer), and
- `SEGFAULT` / `TOOL_CRASH` / `TOOL_HANG` — **the bugs you actually want**.

## Protocol (fixed-budget, apples-to-apples)

For each mode, same corpus, same wall-clock budget:

```bash
for MODE in manifest-bitflip manifest-segment manifest-start manifest-middle \
            manifest-end zip-local-header zip-central-directory crc-fields \
            size-fields random-range; do
  # APKFUZZ_SAVE_DIR is honored by the v2 mutator: it copies every mutant here,
  # and those copies survive the smoke script's seed-dir cleanup.
  export APKFUZZ_SAVE_DIR="mutants-$MODE"
  bash scripts/run-corpus-smoke.sh 300 build/cm-ApkVulFuzz2.so "$MODE"
  # capture this run's (timestamped) output dir for the stats comparison:
  mv "$(ls -td afl-corpus-smoke-*/ | head -1)" "cmp-$MODE"
  unset APKFUZZ_SAVE_DIR
done
```

Then classify the mutants each mode produced and aggregate:

```bash
for MODE in manifest-bitflip zip-central-directory crc-fields random-range ...; do
  python3 scripts/test-apk-tools.py mutants-$MODE \
     --preserve crashers-$MODE --report tools-$MODE.csv
done

python3 scripts/compare-mutators.py \
   $(for M in manifest-bitflip zip-central-directory crc-fields random-range; do echo $M=cmp-$M; done) \
   $(for M in manifest-bitflip zip-central-directory crc-fields random-range; do echo --tools $M=tools-$M.csv; done) \
   --csv comparison.csv
```

`comparison.csv` gives one row per mode with throughput + finding rates side by
side.

## Reading the results — phase guidance (hypotheses to confirm with data)

- **Broad triage / first pass:** `manifest-bitflip` and `random-range` — cheapest,
  widest spread of broken outputs; good for shaking out obvious tool failures.
- **Parser-focused (AXML):** `manifest-start/middle/end`, `manifest-segment` —
  localize which manifest region trips the Android manifest parser / aapt.
- **ZIP-container-focused:** `zip-central-directory`, `zip-local-header` — these
  stress the ZIP readers themselves (unzip / 7z / zipfile / WinRAR), the most
  likely place to find a real `SEGFAULT`/`TOOL_CRASH`.
- **Field-level:** `crc-fields`, `size-fields` — probe integer/bounds handling in
  size/CRC validation code paths specifically.

Pick the mode with the highest `SEGFAULT`+`TOOL_CRASH`+`TOOL_HANG` rate per
exec-second for the "find bugs" phase, and a manifest-focused mode for the
"characterize the Android parser" phase.

## Caveat
None of the current modes preserve APK *installability* (they all break CRC), so
DroidBot/emulator install-time bugs are out of reach until a CRC-repair / re-zip
step is added. That repaired variant would be a valuable extra "mode" to compare
against here.
