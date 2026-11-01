/* unzip.c: unpacking the game-file zips on iOS. */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef void (*UnzipProgress)(uint64_t bytes_done, void *ctx);
/* Bytes the zip's files take once unpacked (0 if it cannot be read). */
uint64_t unzip_total_size(const char *zip_path);
/* Unpacks every entry into dest (which must exist). progress gets the running byte count, starting from
 * done_before. 0 on success; -1 with a reason in err. */
int unzip_to(const char *zip_path, const char *dest, UnzipProgress progress, void *ctx, uint64_t done_before,
             char *err, size_t err_n);
