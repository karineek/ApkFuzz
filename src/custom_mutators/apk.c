#include "apk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <stdbool.h>

static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool find_zip_entry_data_range(const uint8_t *buf, size_t size,
                                      const char *entry_name,
                                      uint64_t *start_offset,
                                      uint64_t *end_offset) {
    const uint32_t eocd_sig = 0x06054b50;
    const uint32_t cd_sig = 0x02014b50;
    const uint32_t local_sig = 0x04034b50;
    const size_t eocd_min_size = 22;
    const size_t max_comment_size = 0xffff;
    size_t search_start;
    size_t eocd_offset = SIZE_MAX;

    if (!buf || size < eocd_min_size || !entry_name || !start_offset || !end_offset) {
        return false;
    }

    search_start = (size > eocd_min_size + max_comment_size)
        ? size - eocd_min_size - max_comment_size
        : 0;

    for (size_t pos = size - eocd_min_size + 1; pos-- > search_start;) {
        if (read_le32(buf + pos) == eocd_sig) {
            eocd_offset = pos;
            break;
        }
        if (pos == 0) {
            break;
        }
    }

    if (eocd_offset == SIZE_MAX || eocd_offset + eocd_min_size > size) {
        return false;
    }

    uint16_t entry_count = read_le16(buf + eocd_offset + 10);
    uint32_t cd_size = read_le32(buf + eocd_offset + 12);
    uint32_t cd_offset = read_le32(buf + eocd_offset + 16);

    if ((uint64_t)cd_offset + cd_size > size) {
        return false;
    }

    size_t pos = cd_offset;
    size_t cd_end = cd_offset + cd_size;

    for (uint16_t entry = 0; entry < entry_count && pos + 46 <= cd_end; entry++) {
        if (read_le32(buf + pos) != cd_sig) {
            return false;
        }

        uint32_t compressed_size = read_le32(buf + pos + 20);
        uint16_t name_len = read_le16(buf + pos + 28);
        uint16_t extra_len = read_le16(buf + pos + 30);
        uint16_t comment_len = read_le16(buf + pos + 32);
        uint32_t local_header_offset = read_le32(buf + pos + 42);
        size_t name_offset = pos + 46;
        size_t next = name_offset + name_len + extra_len + comment_len;

        if (next > cd_end) {
            return false;
        }

        if (strlen(entry_name) == name_len &&
            memcmp(buf + name_offset, entry_name, name_len) == 0) {
            if ((uint64_t)local_header_offset + 30 > size ||
                read_le32(buf + local_header_offset) != local_sig) {
                return false;
            }

            uint16_t local_name_len = read_le16(buf + local_header_offset + 26);
            uint16_t local_extra_len = read_le16(buf + local_header_offset + 28);
            uint64_t data_start = (uint64_t)local_header_offset + 30 +
                                  local_name_len + local_extra_len;
            uint64_t data_end = data_start + compressed_size;

            if (compressed_size == 0 || data_start >= data_end || data_end > size) {
                return false;
            }

            *start_offset = data_start;
            *end_offset = data_end;
            return true;
        }

        pos = next;
    }

    return false;
}

// We populate the APK data based on what we have:
int load_apk_into_mutator(my_mutator_t *data, const char *path) {
	// NEW FILE NAME, we give a name to our output APK fuzzed file
	if (data->fileout_name) { // Safety check to avoid memort leaks
	    free(data->fileout_name);
	    data->fileout_name = NULL;
	}
	data->fileout_name = build_output_filename(path);
	if (!data->fileout_name) {
	    fprintf(stderr, "Error: Failed to create a file name\n");
	    return -1;
	}

	
	// BINARY DATA: We then try to populate the data itself 
    FILE *in = fopen(path, "rb");
    if (!in) {
        fprintf(stderr, "Error: Failed to open file: %s\n", path);
        return -1;
    }
    if (fseek(in, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error: fseek failed\n");
        fclose(in);
        return -1;
    }
    long size = ftell(in);
    if (size < 0) {
        fprintf(stderr, "Error: ftell failed\n");
        fclose(in);
        return -1;
    }
    rewind(in);
    uint8_t *buf = malloc(size);
    if (!buf) {
        fprintf(stderr, "Error: malloc failed\n");
        fclose(in);
        return -1;
    }
    if (fread(buf, 1, size, in) != (size_t)size) {
        fprintf(stderr, "Error: Failed to read file\n");
        free(buf);
        fclose(in);
        return -1;
    }
    fclose(in);

	// We now populate also the binary content to mutate
    data->out_buf = buf;
    data->buf_size = (size_t)size;

    // OFFSETS: dynamically fuzz the compressed AndroidManifest.xml bytes.
    if (!find_zip_entry_data_range(data->out_buf, data->buf_size,
                                   "AndroidManifest.xml", &data->i, &data->j)) {
        fprintf(stderr, "Error: Failed to find AndroidManifest.xml offsets in: %s\n", path);
        free(data->out_buf);
        data->out_buf = NULL;
        data->buf_size = 0;
        return -1;
    }

    return 0;
}

// This is a pretty standard function - I wrote it with ChatGPT 24-March-2026
// It creates a new temp name from the input_path name, no big logic, tons of pointers!
char *build_output_filename(const char *input_path) {
    // Find last '/' (directory separator)
    const char *slash = strrchr(input_path, '/');

    const char *dir = "";
    const char *base = input_path;

    if (slash) {
        size_t dir_len = slash - input_path + 1;

        char *dir_buf = malloc(dir_len + 1);
        if (!dir_buf) return NULL;

        strncpy(dir_buf, input_path, dir_len);
        dir_buf[dir_len] = '\0';

        dir = dir_buf;
        base = slash + 1;
    }

    // Copy basename so we can modify it
    char name[512];
    strncpy(name, base, sizeof(name));
    name[sizeof(name) - 1] = '\0';

    // Remove ".apk"
    char *dot = strrchr(name, '.');
    if (dot && strcmp(dot, ".apk") == 0) {
        *dot = '\0';
    }

    // Remove the old seed number
    // Strip trailing _<digits> or _<digits>_<digits>
	char *p = name + strlen(name) - 1;
	
    while (p > name && (*p >= '0' && *p <= '9')) {
        p--;
    }
	
    if (*p == '_') {
        *p = '\0';
	
        // handle second numeric part (_sec_nsec)
	    p--;
	    while (p > name && (*p >= '0' && *p <= '9')) {
	        p--;
	    }
	
	    if (*p == '_') {
	        *p = '\0';
	    }
	}

    // Timestamp
    struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

    // Allocate final string
    size_t out_size = strlen(dir) + strlen(name) + 64;
    char *out = malloc(out_size);
    if (!out) {
        if (slash) free((void *)dir);
        return NULL;
    }

    snprintf(out, out_size, "%s%s_%ld_%ld.apk", dir, name, (long)ts.tv_sec, ts.tv_nsec);
	
    if (slash) free((void *)dir);

    return out;
}
