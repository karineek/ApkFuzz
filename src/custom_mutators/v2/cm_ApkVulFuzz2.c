/*
 * cm_ApkVulFuzz2.c - Multi-strategy AFL++ custom mutator (parallel build).
 *
 * This is a SEPARATE mutator from the frozen baseline cm_ApkVulFuzz.c. It keeps
 * the same path-file workflow (AFL feeds a text file whose contents are a path
 * to a real APK; the mutator loads that APK, mutates it, writes a timestamped
 * mutant next to the seed, and returns the mutant's path to AFL), but the
 * mutation itself is dispatched to a selectable strategy (see strategies.h).
 *
 * Default mode (no env set) == baseline manifest-bitflip, so this build can also
 * be used to VERIFY tasks 2-3 with APKFUZZ_LOG=1 while leaving the original
 * mutator untouched.
 *
 * Build as AFL custom mutator:  -D AFL_CM (links afl-fuzz.h)
 * Build as standalone tester:   -D APKFUZZ_STANDALONE (adds main())
 */
#include <stdint.h>

#ifdef AFL_CM
  #include "afl-fuzz.h"
#else
  typedef void    afl_state_t;
  typedef uint8_t u8;
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "zip2.h"
#include "strategies.h"

#define TARGET_ENTRY "AndroidManifest.xml"

typedef struct {
#ifdef AFL_CM
    afl_state_t   *afl;
#endif
    uint8_t       *out_buf;       /* whole APK buffer being mutated */
    size_t         buf_size;
    char          *fileout_name;  /* path of the mutant we write */
    zip_layout_t   zl;
    apkfuzz_cfg_t  cfg;
} my_mutator2_t;

/* ---- filename builder: <dir><base>_<sec>_<nsec>.apk (matches baseline) ----
 * If out_dir is non-NULL the mutant is placed there (keeping the corpus/seed
 * directory clean); otherwise it is placed next to the input APK. */
static char *build_output_filename(const char *input_path, const char *out_dir) {
    const char *slash = strrchr(input_path, '/');
    const char *dir = "";
    const char *base = slash ? slash + 1 : input_path;
    char *dir_buf = NULL;

    if (out_dir && *out_dir) {
        size_t dl = strlen(out_dir);
        int add_slash = (out_dir[dl - 1] != '/');
        dir_buf = malloc(dl + (add_slash ? 1 : 0) + 1);
        if (!dir_buf) return NULL;
        memcpy(dir_buf, out_dir, dl);
        if (add_slash) dir_buf[dl] = '/';
        dir_buf[dl + (add_slash ? 1 : 0)] = '\0';
        dir = dir_buf;
    } else if (slash) {
        size_t dir_len = (size_t)(slash - input_path) + 1;
        dir_buf = malloc(dir_len + 1);
        if (!dir_buf) return NULL;
        memcpy(dir_buf, input_path, dir_len);
        dir_buf[dir_len] = '\0';
        dir = dir_buf;
    }

    char name[512];
    strncpy(name, base, sizeof(name));
    name[sizeof(name) - 1] = '\0';

    char *dot = strrchr(name, '.');
    if (dot && strcmp(dot, ".apk") == 0) *dot = '\0';

    /* strip trailing _<digits> (and a second _<digits>) so we re-derive base */
    char *p = name + strlen(name) - 1;
    while (p > name && *p >= '0' && *p <= '9') p--;
    if (*p == '_') {
        *p = '\0';
        p--;
        while (p > name && *p >= '0' && *p <= '9') p--;
        if (*p == '_') *p = '\0';
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    size_t out_size = strlen(dir) + strlen(name) + 64;
    char *out = malloc(out_size);
    if (!out) { free(dir_buf); return NULL; }
    snprintf(out, out_size, "%s%s_%ld_%ld.apk", dir, name, (long)ts.tv_sec, ts.tv_nsec);
    free(dir_buf);
    return out;
}

static void free_state_buffers(my_mutator2_t *d) {
    if (d->out_buf) { free(d->out_buf); d->out_buf = NULL; }
    if (d->fileout_name) { free(d->fileout_name); d->fileout_name = NULL; }
    d->buf_size = 0;
}

static int write_file_exact(const char *path, const uint8_t *buf, size_t size,
                            const char *label, int required) {
    FILE *out = fopen(path, "wb");
    if (!out) {
        fprintf(stderr, "[apkfuzz] %s: failed to open %s\n", label, path);
        return required ? -1 : 0;
    }

    size_t wrote = fwrite(buf, 1, size, out);
    if (wrote != size) {
        fprintf(stderr, "[apkfuzz] %s: short write for %s (%zu/%zu bytes)\n",
                label, path, wrote, size);
        fclose(out);
        return required ? -1 : 0;
    }

    if (fclose(out) != 0) {
        fprintf(stderr, "[apkfuzz] %s: failed to close %s after write\n", label, path);
        return required ? -1 : 0;
    }

    return 1;
}

/* Load APK bytes + analyze zip layout. Returns 0 on success. */
static int load_and_analyze(my_mutator2_t *d, const char *path) {
    free_state_buffers(d);

    d->fileout_name = build_output_filename(path, d->cfg.out_dir);
    if (!d->fileout_name) return -1;

    FILE *in = fopen(path, "rb");
    if (!in) return -1;
    if (fseek(in, 0, SEEK_END) != 0) { fclose(in); return -1; }
    long size = ftell(in);
    if (size < 0) { fclose(in); return -1; }
    rewind(in);
    uint8_t *buf = malloc((size_t)size);
    if (!buf) { fclose(in); return -1; }
    if (fread(buf, 1, (size_t)size, in) != (size_t)size) { free(buf); fclose(in); return -1; }
    fclose(in);

    d->out_buf  = buf;
    d->buf_size = (size_t)size;

    if (!zip2_analyze(d->out_buf, d->buf_size, TARGET_ENTRY, &d->zl) || !d->zl.found) {
        /* Keep buffer (some modes only need whole-file/random), but flag not found. */
        return 0;
    }
    return 0;
}

/* ----------------------------- AFL interface ----------------------------- */
my_mutator2_t *afl_custom_init(afl_state_t *afl, unsigned int seed) {
    srand(seed);
    my_mutator2_t *d = calloc(1, sizeof(my_mutator2_t));
    if (!d) return NULL;
#ifdef AFL_CM
    d->afl = afl;
#else
    (void)afl;
#endif
    apkfuzz_cfg_load(&d->cfg);
    /* Create the output/save directories up-front (best-effort; ignore EEXIST). */
    if (d->cfg.out_dir)  mkdir(d->cfg.out_dir, 0777);
    if (d->cfg.save_dir) mkdir(d->cfg.save_dir, 0777);
    return d;
}

void afl_custom_deinit(my_mutator2_t *d) {
    if (!d) return;
    free_state_buffers(d);
#ifdef AFL_CM
    d->afl = 0;
#endif
    free(d);
}

size_t afl_custom_fuzz(my_mutator2_t *d, uint8_t *buf, size_t buf_size,
                       u8 **out_buf, uint8_t *add_buf, size_t add_buf_size,
                       size_t max_size) {
    (void)add_buf; (void)add_buf_size;

    if (!d || !buf || buf_size < 5 || max_size < buf_size) {
        *out_buf = NULL; return 0;
    }

    /* buf holds the text of a path-file: extract the APK path. */
    char path[512];
    size_t len = (buf_size < sizeof(path) - 1) ? buf_size : sizeof(path) - 1;
    memcpy(path, buf, len);
    path[len] = '\0';
    path[strcspn(path, "\r\n")] = '\0';
    size_t plen = strlen(path);
    if (plen < 4 || strcmp(path + plen - 4, ".apk") != 0) { *out_buf = NULL; return 0; }

    if (load_and_analyze(d, path) != 0 || !d->out_buf || !d->fileout_name) {
        *out_buf = NULL; return 0;
    }

    size_t out_len = strlen(d->fileout_name);
    if (out_len + 1 > max_size) { *out_buf = NULL; return 0; }

    uint64_t rs = 0, re = 0;
    int flips = -1;
    int manifest_found = d->zl.found ? 1 : 0;

    /* crc/size/manifest/zip modes need a located manifest; random-range does not. */
    if (d->zl.found || d->cfg.mode == MODE_RANDOM_RANGE) {
        flips = apkfuzz_mutate(d->out_buf, d->buf_size, &d->zl, &d->cfg, &rs, &re);
    }

    apkfuzz_log_line(&d->cfg, path, d->fileout_name, &d->zl, rs, re, flips, manifest_found);

    if (flips < 0) { *out_buf = NULL; return 0; }

    if (write_file_exact(d->fileout_name, d->out_buf, d->buf_size,
                         "mutant-output", 1) < 0) {
        *out_buf = NULL; return 0;
    }

    /* Optionally keep a persistent copy of the mutant for post-run analysis
     * (APKFUZZ_SAVE_DIR). This survives the smoke script's cleanup of the seed
     * dir, so test-apk-tools.py has specimens to inspect. */
    if (d->cfg.save_dir) {
        const char *bn = strrchr(d->fileout_name, '/');
        bn = bn ? bn + 1 : d->fileout_name;
        char save_path[1024];
        int m = snprintf(save_path, sizeof(save_path), "%s/%s", d->cfg.save_dir, bn);
        if (m > 0 && (size_t)m < sizeof(save_path)) {
            (void)write_file_exact(save_path, d->out_buf, d->buf_size,
                                   "save-copy", 0);
        } else {
            fprintf(stderr, "[apkfuzz] save-copy: path too long for %s/%s\n",
                    d->cfg.save_dir, bn);
        }
    }

    char *nb = malloc(out_len + 1);
    if (!nb) { *out_buf = NULL; return 0; }
    memcpy(nb, d->fileout_name, out_len + 1);
    *out_buf = (u8 *)nb;
    return out_len;
}

/* ------------------------------- standalone ------------------------------ */
#ifdef APKFUZZ_STANDALONE
int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "F-Droid.apk";
    uint8_t *buf = (uint8_t *)path;
    size_t file_size = strlen(path);
    u8 *out_buf = NULL;

    my_mutator2_t *d = afl_custom_init(NULL, (unsigned int)time(NULL));
    if (!d) { fprintf(stderr, "init failed\n"); return 1; }

    fprintf(stderr, "[apkfuzz] mode=%s seg_off=%ld seg_size=%ld max_flips=%ld log=%d\n",
            d->cfg.mode_name, d->cfg.seg_offset, d->cfg.seg_size, d->cfg.max_flips, d->cfg.do_log);

    size_t n = afl_custom_fuzz(d, buf, file_size, &out_buf, NULL, 0, 512);
    if (!out_buf || n == 0) { fprintf(stderr, "fuzz failed\n"); afl_custom_deinit(d); return 2; }
    printf("%s\n", (char *)out_buf);
    free(out_buf);
    afl_custom_deinit(d);
    return 0;
}
#endif
