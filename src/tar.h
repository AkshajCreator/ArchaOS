#ifndef TAR_H
#define TAR_H

#include <stdint.h>
#include <stddef.h>

/* Extract a POSIX ustar TAR archive into dest_prefix (e.g. "/games/tentacle") */
int tar_extract(const uint8_t *tar_data, size_t tar_len, const char *dest_prefix);

#endif /* TAR_H */
