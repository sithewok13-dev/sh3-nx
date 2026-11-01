/* src/runtime/unzip.c against zips Python's zipfile wrote (tests/run-unzip-test.sh). */
#include "../src/runtime/unzip.h"
#include <stdio.h>
#include <string.h>
static void progress(uint64_t done, void *ctx) { *(uint64_t *)ctx = done; }
int main(int argc, char **argv) {
    /* argv: zip dest expect(0 ok / 1 fail) */
    char err[512] = "";
    uint64_t last = 0, total = unzip_total_size(argv[1]);
    int r = unzip_to(argv[1], argv[2], progress, &last, 0, err, sizeof err);
    printf("%s: %s%s (total %llu, progress reached %llu)\n", argv[1], r ? "refused: " : "unpacked", err,
           (unsigned long long)total, (unsigned long long)last);
    return (r != 0) != (argv[3][0] == '1');
}
