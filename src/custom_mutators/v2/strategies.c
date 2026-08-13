/*
 * strategies.c - Implementation of the selectable mutation strategies.
 */
#include "strategies.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Mode name <-> enum                                                  */
/* ------------------------------------------------------------------ */
static const char *k_mode_names[] = {
    "manifest-bitflip", "manifest-segment", "manifest-start",
    "manifest-middle",  "manifest-end",     "zip-local-header",
    "zip-central-directory", "crc-fields",  "size-fields",
    "random-range"
};

const char *apkfuzz_mode_name(mut_mode_t m) {
    if (m < 0 || m >= MODE_INVALID) return "invalid";
    return k_mode_names[m];
}

mut_mode_t apkfuzz_mode_from_string(const char *s) {
    if (!s || !*s) return MODE_MANIFEST_BITFLIP;
    for (int i = 0; i < (int)MODE_INVALID; i++) {
        if (strcmp(s, k_mode_names[i]) == 0) return (mut_mode_t)i;
    }
    /* a couple of friendly aliases */
    if (strcmp(s, "central-directory") == 0) return MODE_ZIP_CENTRAL_DIR;
    if (strcmp(s, "local-header") == 0)      return MODE_ZIP_LOCAL_HEADER;
    if (strcmp(s, "random") == 0)            return MODE_RANDOM_RANGE;
    return MODE_INVALID;
}

/* ------------------------------------------------------------------ */
/* Config loading                                                      */
/* ------------------------------------------------------------------ */
static long env_long(const char *name, long dflt) {
    const char *v = getenv(name);
    if (!v || !*v) return dflt;
    char *end = NULL;
    long r = strtol(v, &end, 0);
    if (end == v) return dflt;
    return r;
}

void apkfuzz_cfg_load(apkfuzz_cfg_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));

    const char *mode_s = getenv("APKFUZZ_MUTATOR_MODE");
    cfg->mode = apkfuzz_mode_from_string(mode_s);
    if (cfg->mode == MODE_INVALID) {
        fprintf(stderr,
                "[apkfuzz] WARNING: unknown APKFUZZ_MUTATOR_MODE='%s', "
                "falling back to manifest-bitflip\n", mode_s ? mode_s : "");
        cfg->mode = MODE_MANIFEST_BITFLIP;
    }
    cfg->mode_name  = apkfuzz_mode_name(cfg->mode);
    cfg->seg_offset = env_long("APKFUZZ_SEGMENT_OFFSET", -1);
    cfg->seg_size   = env_long("APKFUZZ_SEGMENT_SIZE",   -1);
    cfg->max_flips  = env_long("APKFUZZ_MAX_FLIPS",      -1);

    const char *lg = getenv("APKFUZZ_LOG");
    cfg->do_log = (lg && *lg && strcmp(lg, "0") != 0) ? 1 : 0;
    const char *lf = getenv("APKFUZZ_LOG_FILE");
    cfg->log_file = (lf && *lf) ? lf : "apkfuzz_mutator.log";
    const char *od = getenv("APKFUZZ_OUT_DIR");
    cfg->out_dir = (od && *od) ? od : NULL;
    const char *sd = getenv("APKFUZZ_SAVE_DIR");
    cfg->save_dir = (sd && *sd) ? sd : NULL;
    cfg->seeded = 0;
}

/* ------------------------------------------------------------------ */
/* RNG (kept local; baseline seeds from time once)                     */
/* ------------------------------------------------------------------ */
static void ensure_seed(apkfuzz_cfg_t *cfg) {
    if (cfg->seeded) return;
    srand((unsigned int)time(NULL) ^ (unsigned int)(size_t)cfg);
    cfg->seeded = 1;
}

static int pick_flip_count(apkfuzz_cfg_t *cfg) {
    if (cfg->max_flips >= 0) return (int)cfg->max_flips;
    return (rand() % 50) + 1;   /* baseline: 1..50 */
}

/* Flip `n` random bits within [start,end). Returns flips actually done. */
static int flip_bits_in_range(uint8_t *buf, uint64_t start, uint64_t end, int n) {
    if (end <= start) return 0;
    uint64_t range = end - start;
    int done = 0;
    for (int k = 0; k < n; k++) {
        uint64_t off = start + (uint64_t)(rand() % range);
        int bit = rand() % 8;
        buf[off] ^= (uint8_t)(1u << bit);
        done++;
    }
    return done;
}

/* ------------------------------------------------------------------ */
/* Region computation per mode                                         */
/* ------------------------------------------------------------------ */
/* Returns 1 and fills [rs,re) for range-style modes; returns 0 for the
 * field-style modes (crc/size) which are handled specially. */
static int compute_region(size_t size, const zip_layout_t *zl,
                          apkfuzz_cfg_t *cfg, uint64_t *rs, uint64_t *re) {
    uint64_t ms = zl->data_start, me = zl->data_end;
    uint64_t mlen = me - ms;

    switch (cfg->mode) {
        case MODE_MANIFEST_BITFLIP:
            *rs = ms; *re = me; return 1;

        case MODE_MANIFEST_SEGMENT: {
            uint64_t off = (cfg->seg_offset >= 0) ? (uint64_t)cfg->seg_offset : 0;
            uint64_t sz  = (cfg->seg_size   >= 0) ? (uint64_t)cfg->seg_size   : mlen;
            uint64_t s = ms + off;
            if (s > me) s = me;
            uint64_t e = s + sz;
            if (e > me) e = me;
            *rs = s; *re = e; return 1;
        }
        case MODE_MANIFEST_START:
            *rs = ms; *re = ms + (mlen / 3 ? mlen / 3 : mlen); return 1;
        case MODE_MANIFEST_MIDDLE:
            *rs = ms + mlen / 3; *re = ms + (2 * mlen) / 3;
            if (*re <= *rs) *re = me;
            return 1;
        case MODE_MANIFEST_END:
            *rs = ms + (2 * mlen) / 3; *re = me;
            if (*re <= *rs) *rs = ms;
            return 1;

        case MODE_ZIP_LOCAL_HEADER:
            /* header sig .. start of compressed data (header+name+extra) */
            *rs = zl->local_header_off; *re = zl->local_data_off; return 1;

        case MODE_ZIP_CENTRAL_DIR:
            *rs = zl->cd_start; *re = zl->cd_end; return 1;

        case MODE_RANDOM_RANGE: {
            uint64_t off = (cfg->seg_offset >= 0) ? (uint64_t)cfg->seg_offset : 0;
            uint64_t sz  = (cfg->seg_size   >= 0) ? (uint64_t)cfg->seg_size   : (size - off);
            uint64_t s = off, e = off + sz;
            if (s > size) s = size;
            if (e > size) e = size;
            *rs = s; *re = e; return 1;
        }

        case MODE_CRC_FIELDS:
        case MODE_SIZE_FIELDS:
        default:
            return 0;
    }
}

/* Perturb the 4 bytes at `field_off` with a random non-zero delta pattern. */
static int perturb_field(uint8_t *buf, uint64_t field_off, uint64_t size, int width) {
    if (field_off + width > size) return 0;
    int done = 0;
    for (int b = 0; b < width; b++) {
        int bit = rand() % 8;
        buf[field_off + b] ^= (uint8_t)(1u << bit);
        done++;
    }
    return done;
}

/* ------------------------------------------------------------------ */
/* Public mutate                                                       */
/* ------------------------------------------------------------------ */
int apkfuzz_mutate(uint8_t *buf, size_t size, const zip_layout_t *zl,
                   apkfuzz_cfg_t *cfg,
                   uint64_t *out_rstart, uint64_t *out_rend) {
    if (!buf || !zl || !cfg) return -1;
    /* random-range works on the whole file and does not need a located manifest;
     * every other mode does. */
    if (!zl->found && cfg->mode != MODE_RANDOM_RANGE) return -1;
    ensure_seed(cfg);

    uint64_t rs = 0, re = 0;
    int flips = 0;

    if (cfg->mode == MODE_CRC_FIELDS) {
        int n = perturb_field(buf, zl->local_crc_off, size, 4);
        n += perturb_field(buf, zl->cd_crc_off, size, 4);
        flips = n;
        rs = zl->local_crc_off; re = zl->local_crc_off + 4;
    } else if (cfg->mode == MODE_SIZE_FIELDS) {
        int n = perturb_field(buf, zl->local_csize_off, size, 4);
        n += perturb_field(buf, zl->local_usize_off, size, 4);
        n += perturb_field(buf, zl->cd_csize_off, size, 4);
        n += perturb_field(buf, zl->cd_usize_off, size, 4);
        flips = n;
        rs = zl->local_csize_off; re = zl->local_usize_off + 4;
    } else {
        if (!compute_region(size, zl, cfg, &rs, &re)) return -1;
        if (re > size) re = size;
        if (re <= rs) return -1;              /* nothing sane to fuzz */
        int n = pick_flip_count(cfg);
        flips = flip_bits_in_range(buf, rs, re, n);
    }

    if (out_rstart) *out_rstart = rs;
    if (out_rend)   *out_rend   = re;
    return flips;
}

/* ------------------------------------------------------------------ */
/* Logging                                                             */
/* ------------------------------------------------------------------ */
void apkfuzz_log_line(const apkfuzz_cfg_t *cfg,
                      const char *src_path, const char *out_path,
                      const zip_layout_t *zl,
                      uint64_t rstart, uint64_t rend, int flips,
                      int manifest_found) {
    if (!cfg || !cfg->do_log) return;
    FILE *f = fopen(cfg->log_file, "a");
    if (!f) return;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    fprintf(f,
        "ts=%ld.%09ld mode=%s src=%s manifest_found=%d "
        "manifest=[%llu,%llu) region=[%llu,%llu) region_len=%llu flips=%d out=%s\n",
        (long)ts.tv_sec, ts.tv_nsec,
        cfg->mode_name ? cfg->mode_name : "?",
        src_path ? src_path : "?",
        manifest_found,
        (unsigned long long)(zl ? zl->data_start : 0),
        (unsigned long long)(zl ? zl->data_end : 0),
        (unsigned long long)rstart, (unsigned long long)rend,
        (unsigned long long)(rend > rstart ? rend - rstart : 0),
        flips,
        out_path ? out_path : "?");
    fclose(f);
}
