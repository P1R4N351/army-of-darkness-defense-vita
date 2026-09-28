/* Host test: prints sha1/sha256/md5 of a file, fed in uneven chunks. */
#include <stdio.h>
#include <stdlib.h>
#include "aod/hash.h"
#include "sha1/sha1.h"
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n + 1); if (fread(b, 1, n, f) != (size_t)n) return 3;
    aod_sha256_ctx s; aod_md5_ctx m; SHA1_CTX h;
    aod_sha256_init(&s); aod_md5_init(&m); sha1_init(&h);
    long off = 0, step = 1;
    while (off < n) { long k = step; if (off + k > n) k = n - off;
        aod_sha256_update(&s, b + off, k); aod_md5_update(&m, b + off, k); sha1_update(&h, b + off, k);
        off += k; step = (step * 7 + 3) % 200 + 1; }
    unsigned char o[32];
    sha1_final(&h, o); for (int i = 0; i < 20; i++) printf("%02x", o[i]); printf(" ");
    aod_sha256_final(&s, o); for (int i = 0; i < 32; i++) printf("%02x", o[i]); printf(" ");
    aod_md5_final(&m, o); for (int i = 0; i < 16; i++) printf("%02x", o[i]); printf("\n");
    return 0;
}
