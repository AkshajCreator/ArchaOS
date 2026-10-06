#include "iso9660.h"
#include "ata.h"
#include "fs.h"
#include "mm.h"
#include "vga.h"
#include "serial.h"
#include "string.h"
#include <stdint.h>
#include <stddef.h>

#define ISO_SECTOR_SIZE 2048
#define ATA_SECTORS_PER_ISO (ISO_SECTOR_SIZE / 512)

static int read_iso_sector(ata_device_t *dev, uint32_t iso_lba, uint8_t *buf)
{
    uint32_t ata_lba = iso_lba * ATA_SECTORS_PER_ISO;
    return ata_read_sectors(dev, ata_lba, ATA_SECTORS_PER_ISO, buf);
}

static int scan_iso_directory(ata_device_t *dev, uint32_t dir_lba, uint32_t dir_size, const char *vfs_dir)
{
    fs_mkdir_p(vfs_dir);

    uint8_t *sector_buf = (uint8_t *)kmalloc(ISO_SECTOR_SIZE);
    if (!sector_buf) return 0;

    int total_files = 0;
    uint32_t bytes_read = 0;
    uint32_t current_lba = dir_lba;

    while (bytes_read < dir_size) {
        if (read_iso_sector(dev, current_lba, sector_buf) < 0) {
            break;
        }

        uint32_t pos = 0;
        while (pos < ISO_SECTOR_SIZE && bytes_read + pos < dir_size) {
            uint8_t record_len = sector_buf[pos];
            if (record_len == 0) {
                /* End of records in this 2048-byte sector, skip to next */
                break;
            }

            uint32_t extent_lba = (uint32_t)sector_buf[pos + 2] |
                                  ((uint32_t)sector_buf[pos + 3] << 8) |
                                  ((uint32_t)sector_buf[pos + 4] << 16) |
                                  ((uint32_t)sector_buf[pos + 5] << 24);

            uint32_t data_len   = (uint32_t)sector_buf[pos + 10] |
                                  ((uint32_t)sector_buf[pos + 11] << 8) |
                                  ((uint32_t)sector_buf[pos + 12] << 16) |
                                  ((uint32_t)sector_buf[pos + 13] << 24);

            uint8_t flags = sector_buf[pos + 25];
            uint8_t name_len = sector_buf[pos + 32];

            char filename[128];
            if (name_len == 1 && sector_buf[pos + 33] == 0) {
                /* Current directory '.' */
                pos += record_len;
                continue;
            } else if (name_len == 1 && sector_buf[pos + 33] == 1) {
                /* Parent directory '..' */
                pos += record_len;
                continue;
            } else {
                int copy_len = name_len < 127 ? name_len : 127;
                memcpy(filename, &sector_buf[pos + 33], copy_len);
                filename[copy_len] = '\0';
                /* Strip trailing ';1' or ';' ISO version specifier */
                char *semi = strchr(filename, ';');
                if (semi) *semi = '\0';
            }

            /* Build destination path */
            char target_path[FS_MAX_PATH];
            strncpy(target_path, vfs_dir, sizeof(target_path) - 1);
            size_t vlen = strlen(target_path);
            if (vlen > 0 && target_path[vlen - 1] != '/' && vlen < sizeof(target_path) - 1) {
                target_path[vlen] = '/';
                target_path[vlen + 1] = '\0';
            }
            size_t clen = strlen(target_path);
            strncpy(target_path + clen, filename, sizeof(target_path) - 1 - clen);
            target_path[sizeof(target_path) - 1] = '\0';

            if (flags & 0x02) {
                /* Subdirectory */
                total_files += scan_iso_directory(dev, extent_lba, data_len, target_path);
            } else {
                /* Regular file: read sectors and store to VFS if reasonable size */
                if (data_len > 0 && data_len <= 16 * 1024 * 1024) {
                    uint8_t *file_data = (uint8_t *)kmalloc(data_len);
                    if (file_data) {
                        uint32_t flba = extent_lba;
                        uint32_t fremaining = data_len;
                        uint32_t foffset = 0;
                        while (fremaining > 0) {
                            uint8_t chunk[ISO_SECTOR_SIZE];
                            if (read_iso_sector(dev, flba++, chunk) < 0) break;
                            uint32_t chunk_copy = fremaining < ISO_SECTOR_SIZE ? fremaining : ISO_SECTOR_SIZE;
                            memcpy(file_data + foffset, chunk, chunk_copy);
                            foffset += chunk_copy;
                            fremaining -= chunk_copy;
                        }
                        fs_write(target_path, (const char *)file_data, data_len);
                        kfree(file_data);
                        total_files++;
                        serial_printf(COM1_BASE, "[ISO9660] Mounted %s (%u bytes)\n", target_path, data_len);
                    }
                }
            }

            pos += record_len;
        }

        bytes_read += ISO_SECTOR_SIZE;
        current_lba++;
    }

    kfree(sector_buf);
    return total_files;
}

int iso9660_mount(int dev_idx, const char *mount_point)
{
    if (dev_idx < 0 || dev_idx >= ata_get_device_count()) {
        return -1;
    }
    ata_device_t *dev = ata_get_device(dev_idx);
    if (!dev || !dev->present) {
        return -1;
    }

    /* Read Sector 16: Primary Volume Descriptor */
    uint8_t pvd[ISO_SECTOR_SIZE];
    if (read_iso_sector(dev, 16, pvd) < 0) {
        return -1;
    }

    /* Check "CD001" identifier at offset 1 */
    if (pvd[0] != 0x01 || memcmp(&pvd[1], "CD001", 5) != 0) {
        return -2; /* Not an ISO9660 image */
    }

    /* Root directory record at offset 156 (0x9C) */
    uint32_t root_lba = (uint32_t)pvd[156 + 2] |
                        ((uint32_t)pvd[156 + 3] << 8) |
                        ((uint32_t)pvd[156 + 4] << 16) |
                        ((uint32_t)pvd[156 + 5] << 24);

    uint32_t root_size = (uint32_t)pvd[156 + 10] |
                         ((uint32_t)pvd[156 + 11] << 8) |
                         ((uint32_t)pvd[156 + 12] << 16) |
                         ((uint32_t)pvd[156 + 13] << 24);

    serial_printf(COM1_BASE, "[ISO9660] Mounting drive %d into %s (Root LBA=%u, size=%u)\n",
                  dev_idx, mount_point, root_lba, root_size);

    return scan_iso_directory(dev, root_lba, root_size, mount_point);
}

void cmd_mount(const char *args)
{
    if (!args || !args[0]) {
        vga_print_color("Usage: mount [dev_index] [mount_point]\n", 0x0E);
        vga_print("  Mounts an ISO9660 CD-ROM or secondary drive into VFS.\n");
        vga_print("  Example: mount 1 /games\n");
        vga_print("  Detected ATA devices:\n");
        for (int i = 0; i < ata_get_device_count(); i++) {
            ata_device_t *d = ata_get_device(i);
            if (d && d->present) {
                char ibuf[16];
                itoa(i, ibuf, 10);
                vga_print("    Drive "); vga_print(ibuf); vga_print(": ");
                vga_print(d->model[0] ? d->model : "Generic Drive");
                vga_print(d->type == ATA_TYPE_ATAPI ? " [ATAPI CD-ROM]\n" : " [ATA Hard Disk]\n");
            }
        }
        return;
    }

    while (*args == ' ') args++;
    int dev_idx = *args - '0';
    while (*args && *args != ' ') args++;
    while (*args == ' ') args++;

    const char *mount_pt = *args ? args : "/mnt";

    vga_print("Mounting Drive ");
    char dbuf[16]; itoa(dev_idx, dbuf, 10); vga_print(dbuf);
    vga_print(" to "); vga_print(mount_pt); vga_print("...\n");

    int files = iso9660_mount(dev_idx, mount_pt);
    if (files >= 0) {
        vga_print_color("[Mount] Success: Mounted ", 0x0A);
        char cbuf[16]; itoa(files, cbuf, 10); vga_print(cbuf);
        vga_print(" files into "); vga_print(mount_pt); vga_print("\n");
    } else if (files == -2) {
        vga_print_color("[Mount] Error: Drive is not an ISO9660 filesystem\n", 0x0C);
    } else {
        vga_print_color("[Mount] Error: Failed to read device\n", 0x0C);
    }
}
