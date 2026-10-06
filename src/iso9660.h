#ifndef ISO9660_H
#define ISO9660_H

#include <stdint.h>
#include <stddef.h>

/* Mount an ISO9660 CD-ROM or ISO disk image into the VFS at mount_point (e.g. "/mnt" or "/games") */
int iso9660_mount(int dev_idx, const char *mount_point);
void cmd_mount(const char *args);

#endif /* ISO9660_H */
