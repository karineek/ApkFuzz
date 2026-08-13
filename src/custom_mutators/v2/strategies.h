/*
 * strategies.h - Selectable mutation strategies for the multi-strategy mutator.
 *
 * Strategy is chosen at runtime via APKFUZZ_MUTATOR_MODE. Region and intensity
 * are tunable via APKFUZZ_SEGMENT_OFFSET / APKFUZZ_SEGMENT_SIZE / APKFUZZ_MAX_FLIPS.
 * Opt-in per-fuzz logging via APKFUZZ_LOG=1 (+ optional APKFUZZ_LOG_FILE).
 */
#ifndef APKFUZZ_STRATEGIES_H
#define APKFUZZ_STRATEGIES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "zip2.h"

typedef enum {
    MODE_MANIFEST_BITFLIP = 0, /* flip bits across whole compressed manifest (== baseline) */
    MODE_MANIFEST_SEGMENT,     /* flip within [off, off+size) inside the manifest region   */
    MODE_MANIFEST_START,       /* flip within first third of the manifest region           */
    MODE_MANIFEST_MIDDLE,      /* flip within middle third of the manifest region          */
    MODE_MANIFEST_END,         /* flip within last third of the manifest region            */
    MODE_ZIP_LOCAL_HEADER,     /* flip within the manifest local file header               */
    MODE_ZIP_CENTRAL_DIR,      /* flip within the whole central directory                  */
    MODE_CRC_FIELDS,           /* perturb manifest CRC-32 fields (local + central)         */
    MODE_SIZE_FIELDS,          /* perturb manifest comp/uncomp size fields (local+central) */
    MODE_RANDOM_RANGE,         /* flip anywhere in the file (or [off,off+size) if set)     */
    MODE_INVALID
} mut_mode_t;

typedef struct {
    mut_mode_t   mode;
    const char  *mode_name;
    long         seg_offset;   /* -1 = unset */
    long         seg_size;     /* -1 = unset */
    long         max_flips;    /* -1 = random 1..50 (baseline behavior) */
    int          do_log;       /* APKFUZZ_LOG */
    const char  *log_file;     /* APKFUZZ_LOG_FILE or default */
    const char  *out_dir;      /* APKFUZZ_OUT_DIR: write mutants here instead of next to the seed (NULL = next to seed) */
    const char  *save_dir;     /* APKFUZZ_SAVE_DIR: keep a copy of each mutant here (NULL = off) */
    unsigned int seeded;       /* internal: RNG seeded flag */
} apkfuzz_cfg_t;

mut_mode_t   apkfuzz_mode_from_string(const char *s);
const char  *apkfuzz_mode_name(mut_mode_t m);

/* Load config from environment (defaults preserve baseline behavior). */
void apkfuzz_cfg_load(apkfuzz_cfg_t *cfg);

/*
 * Mutate `buf` in place according to cfg + zip layout.
 * Returns the number of byte/bit edits applied (>=0), or -1 on hard error.
 * On success, [*out_rstart, *out_rend) reports the region that was targeted.
 */
int apkfuzz_mutate(uint8_t *buf, size_t size, const zip_layout_t *zl,
                   apkfuzz_cfg_t *cfg,
                   uint64_t *out_rstart, uint64_t *out_rend);

/* Append one structured line describing this fuzz iteration (if do_log). */
void apkfuzz_log_line(const apkfuzz_cfg_t *cfg,
                      const char *src_path, const char *out_path,
                      const zip_layout_t *zl,
                      uint64_t rstart, uint64_t rend, int flips,
                      int manifest_found);

#endif /* APKFUZZ_STRATEGIES_H */
