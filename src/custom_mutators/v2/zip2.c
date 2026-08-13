/*
 * zip2.c - Extended ZIP layout analyzer. See zip2.h.
 *
 * The central-directory / EOCD walking logic mirrors the proven baseline in
 * apk.c (find_zip_entry_data_range) so behavior stays consistent, but it
 * records more offsets. Kept independent so apk.c is never modified.
 */
#include "zip2.h"
#include <string.h>

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

bool zip2_analyze(const uint8_t *buf, size_t size,
                  const char *entry_name, zip_layout_t *out) {
    const uint32_t eocd_sig  = 0x06054b50;
    const uint32_t cd_sig    = 0x02014b50;
    const uint32_t local_sig = 0x04034b50;
    const size_t   eocd_min  = 22;
    const size_t   max_comment = 0xffff;

    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->found = false;

    if (!buf || size < eocd_min || !entry_name) return false;

    /* Locate EOCD by scanning backwards. */
    size_t search_start = (size > eocd_min + max_comment)
        ? size - eocd_min - max_comment : 0;
    size_t eocd_off = (size_t)-1;
    for (size_t pos = size - eocd_min + 1; pos-- > search_start;) {
        if (rd32(buf + pos) == eocd_sig) { eocd_off = pos; break; }
        if (pos == 0) break;
    }
    if (eocd_off == (size_t)-1 || eocd_off + eocd_min > size) return false;

    uint16_t entry_count = rd16(buf + eocd_off + 10);
    uint32_t cd_size     = rd32(buf + eocd_off + 12);
    uint32_t cd_offset   = rd32(buf + eocd_off + 16);
    if ((uint64_t)cd_offset + cd_size > size) return false;

    out->eocd_off  = eocd_off;
    out->cd_start  = cd_offset;
    out->cd_end    = (uint64_t)cd_offset + cd_size;

    size_t pos    = cd_offset;
    size_t cd_end = cd_offset + cd_size;

    for (uint16_t e = 0; e < entry_count && pos + 46 <= cd_end; e++) {
        if (rd32(buf + pos) != cd_sig) return false;

        uint32_t compressed_size    = rd32(buf + pos + 20);
        uint16_t name_len           = rd16(buf + pos + 28);
        uint16_t extra_len          = rd16(buf + pos + 30);
        uint16_t comment_len        = rd16(buf + pos + 32);
        uint32_t local_header_off   = rd32(buf + pos + 42);
        size_t   name_off           = pos + 46;
        size_t   next               = name_off + name_len + extra_len + comment_len;

        if (next > cd_end) return false;

        if (strlen(entry_name) == name_len &&
            memcmp(buf + name_off, entry_name, name_len) == 0) {

            if ((uint64_t)local_header_off + 30 > size ||
                rd32(buf + local_header_off) != local_sig) {
                return false;
            }

            uint16_t l_name_len  = rd16(buf + local_header_off + 26);
            uint16_t l_extra_len = rd16(buf + local_header_off + 28);
            uint64_t data_start  = (uint64_t)local_header_off + 30 + l_name_len + l_extra_len;
            uint64_t data_end    = data_start + compressed_size;

            if (compressed_size == 0 || data_start >= data_end || data_end > size) {
                return false;
            }

            out->found            = true;
            out->compressed_size  = compressed_size;
            out->data_start       = data_start;
            out->data_end         = data_end;

            out->local_header_off = local_header_off;
            out->local_data_off   = data_start;
            out->local_crc_off    = local_header_off + 14;
            out->local_csize_off  = local_header_off + 18;
            out->local_usize_off  = local_header_off + 22;

            out->cd_entry_off     = pos;
            out->cd_crc_off       = pos + 16;
            out->cd_csize_off     = pos + 20;
            out->cd_usize_off     = pos + 24;
            return true;
        }
        pos = next;
    }
    return false;
}
