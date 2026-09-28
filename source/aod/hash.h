#ifndef AOD_HASH_H
#define AOD_HASH_H

#include <stddef.h>
#include <stdint.h>

typedef struct { uint32_t s[8]; uint64_t len; uint8_t buf[64]; size_t fill; } aod_sha256_ctx;
typedef struct { uint32_t s[4]; uint64_t len; uint8_t buf[64]; size_t fill; } aod_md5_ctx;

void aod_sha256_init(aod_sha256_ctx *c);
void aod_sha256_update(aod_sha256_ctx *c, const void *data, size_t n);
void aod_sha256_final(aod_sha256_ctx *c, uint8_t out[32]);

void aod_md5_init(aod_md5_ctx *c);
void aod_md5_update(aod_md5_ctx *c, const void *data, size_t n);
void aod_md5_final(aod_md5_ctx *c, uint8_t out[16]);

#endif
