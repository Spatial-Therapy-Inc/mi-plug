// Small crypto for the miIO LAN protocol, plain C (host tests run it): MD5 and AES-128-CBC, and hex for tokens.
// Nothing here keeps state or logs; keys and tokens never leave the caller's buffers.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void mp_md5(const uint8_t *data, size_t len, uint8_t out[16]);
/** MD5 in parts (the miIO checksum runs over a header, the token and the payload). */
typedef struct {
    uint32_t h[4];
    uint8_t buf[64];
    size_t used;
    uint64_t total;
} mp_md5_ctx;
void mp_md5_init(mp_md5_ctx *c);
void mp_md5_update(mp_md5_ctx *c, const uint8_t *data, size_t len);
void mp_md5_final(mp_md5_ctx *c, uint8_t out[16]);

/** AES-128-CBC over whole blocks (len a multiple of 16); in and out may be the same buffer. */
void mp_aes128_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16], const uint8_t *in, size_t len, uint8_t *out);
void mp_aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16], const uint8_t *in, size_t len, uint8_t *out);

/** 2·n hex characters into n bytes; false on anything else. */
bool mp_hex_decode(const char *hex, uint8_t *out, size_t n);
void mp_hex_encode(const uint8_t *data, size_t n, char *out);  // 2·n characters and a NUL

#ifdef __cplusplus
}
#endif
