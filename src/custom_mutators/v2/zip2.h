/*
 * zip2.h - Extended ZIP layout analyzer for the multi-strategy APK mutator.
 *
 * This is intentionally SEPARATE from the frozen baseline apk.c so that the
 * original mutator stays byte-for-byte unchanged. Where apk.c only exposes the
 * compressed AndroidManifest.xml data range, zip2 additionally exposes the
 * local file header, the CRC-32 / size fields (local + central directory), and
 * the central directory extents, which the zip-header / crc / size strategies
 * need.
 *
 * All offsets are absolute byte offsets into the whole APK/ZIP buffer.
 */
#ifndef APKFUZZ_ZIP2_H
#define APKFUZZ_ZIP2_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    bool     found;             /* entry_name was located in the central dir   */

    /* Compressed data payload of the target entry (e.g. AndroidManifest.xml). */
    uint64_t data_start;        /* first byte of compressed data               */
    uint64_t data_end;          /* one past last byte (start + compressed_size)*/

    /* Local file header of the target entry.                                  */
    uint64_t local_header_off;  /* 'PK\x03\x04' signature offset               */
    uint64_t local_data_off;    /* local_header_off + 30 + name_len + extra_len*/
    uint64_t local_crc_off;     /* local_header_off + 14 (4 bytes)             */
    uint64_t local_csize_off;   /* local_header_off + 18 (4 bytes)             */
    uint64_t local_usize_off;   /* local_header_off + 22 (4 bytes)             */

    /* Central directory record of the target entry.                           */
    uint64_t cd_entry_off;      /* 'PK\x01\x02' signature offset               */
    uint64_t cd_crc_off;        /* cd_entry_off + 16 (4 bytes)                 */
    uint64_t cd_csize_off;      /* cd_entry_off + 20 (4 bytes)                 */
    uint64_t cd_usize_off;      /* cd_entry_off + 24 (4 bytes)                 */

    /* Whole central directory + EOCD.                                         */
    uint64_t cd_start;          /* start of central directory                  */
    uint64_t cd_end;            /* end of central directory (start + cd_size)  */
    uint64_t eocd_off;          /* End Of Central Directory signature offset   */

    uint32_t compressed_size;   /* compressed size of the target entry         */
} zip_layout_t;

/*
 * Analyze `buf` (size bytes) as a ZIP and fill `out` with the layout for
 * `entry_name`. Returns true on success (entry found + all offsets valid),
 * false otherwise. On false, out->found is set to false.
 */
bool zip2_analyze(const uint8_t *buf, size_t size,
                  const char *entry_name, zip_layout_t *out);

#endif /* APKFUZZ_ZIP2_H */
