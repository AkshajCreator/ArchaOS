#include "tar.h"
#include "fs.h"
#include "string.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>

int tar_extract(const uint8_t *tar_data, size_t tar_len, const char *dest_prefix)
{
    if (!tar_data || tar_len < 512) return -1;

    size_t offset = 0;
    int files_extracted = 0;

    /* Ensure root destination directory exists */
    if (dest_prefix && dest_prefix[0]) {
        fs_mkdir_p(dest_prefix);
    }

    while (offset + 512 <= tar_len) {
        const uint8_t *block = tar_data + offset;

        /* Check for end of archive (block of zeros) */
        int is_zero = 1;
        for (int i = 0; i < 512; i++) {
            if (block[i] != 0) { is_zero = 0; break; }
        }
        if (is_zero) {
            break;
        }

        /* Read filename */
        char raw_name[101];
        memcpy(raw_name, block, 100);
        raw_name[100] = '\0';

        /* Strip leading "./" if present */
        const char *rel_name = raw_name;
        if (rel_name[0] == '.' && rel_name[1] == '/') {
            rel_name += 2;
        }
        while (rel_name[0] == '/') rel_name++;

        if (rel_name[0] == '\0') {
            offset += 512;
            continue;
        }

        /* Read size in octal */
        size_t file_size = 0;
        for (int i = 0; i < 11; i++) {
            char c = (char)block[124 + i];
            if (c >= '0' && c <= '7') {
                file_size = (file_size << 3) + (c - '0');
            } else if (c == ' ' || c == '\0') {
                if (file_size > 0) break;
            }
        }

        char typeflag = (char)block[156];
        offset += 512;

        if (offset + file_size > tar_len) {
            serial_printf(COM1_BASE, "[TAR] Corrupted block: file %s size %u exceeds archive\n",
                          rel_name, (uint32_t)file_size);
            break;
        }

        /* Build full destination path */
        char full_path[FS_MAX_PATH];
        full_path[0] = '\0';
        if (dest_prefix && dest_prefix[0]) {
            strncpy(full_path, dest_prefix, sizeof(full_path) - 1);
            size_t plen = strlen(full_path);
            if (plen > 0 && full_path[plen - 1] != '/' && plen < sizeof(full_path) - 1) {
                full_path[plen] = '/';
                full_path[plen + 1] = '\0';
            }
        }
        size_t cur_len = strlen(full_path);
        strncpy(full_path + cur_len, rel_name, sizeof(full_path) - 1 - cur_len);
        full_path[sizeof(full_path) - 1] = '\0';

        if (typeflag == '5' || rel_name[strlen(rel_name) - 1] == '/') {
            /* Directory */
            fs_mkdir_p(full_path);
        } else {
            /* Regular file */
            char parent_dir[FS_MAX_PATH];
            strncpy(parent_dir, full_path, sizeof(parent_dir) - 1);
            parent_dir[sizeof(parent_dir) - 1] = '\0';
            int last_slash = -1;
            for (int i = (int)strlen(parent_dir) - 1; i >= 0; i--) {
                if (parent_dir[i] == '/') { last_slash = i; break; }
            }
            if (last_slash > 0) {
                parent_dir[last_slash] = '\0';
                fs_mkdir_p(parent_dir);
            }

            fs_write(full_path, (const char *)(tar_data + offset), file_size);
            files_extracted++;
            serial_printf(COM1_BASE, "[TAR] Extracted: %s (%u bytes)\n", full_path, (uint32_t)file_size);
        }

        /* Move offset past file payload (padded to 512-byte boundary) */
        offset += (file_size + 511) & ~511;
    }

    return files_extracted;
}
