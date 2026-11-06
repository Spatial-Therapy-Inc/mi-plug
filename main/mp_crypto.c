// See mp_crypto.h. Straightforward implementations of the published algorithms (RFC 1321, FIPS 197); test/test_plug.c
// checks each against known vectors.
#include "mp_crypto.h"

#include <string.h>

// ---------------------------------------------------------------- MD5 (RFC 1321)

static uint32_t rol32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static const uint32_t MD5_K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};
static const uint8_t MD5_R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                  5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

static void md5_block(uint32_t h[4], const uint8_t *p) {
    uint32_t w[16];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16 | (uint32_t)p[i * 4 + 3] << 24;
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16) f = (b & c) | (~b & d), g = i;
        else if (i < 32) f = (d & b) | (~d & c), g = (5 * i + 1) % 16;
        else if (i < 48) f = b ^ c ^ d, g = (3 * i + 5) % 16;
        else f = c ^ (b | ~d), g = (7 * i) % 16;
        const uint32_t t = d;
        d = c;
        c = b;
        b = b + rol32(a + f + MD5_K[i] + w[g], MD5_R[i]);
        a = t;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d;
}

/** The padding all three hashes share: 0x80, zeros, the bit length (little-endian for MD5, big for the SHAs). */
typedef void (*block_fn)(uint32_t *h, const uint8_t *p);
static void md_run(uint32_t *h, block_fn block, const uint8_t *data, size_t len, bool big_endian_len) {
    size_t i = 0;
    for (; i + 64 <= len; i += 64) block(h, data + i);
    uint8_t tail[128] = {0};
    const size_t rest = len - i;
    memcpy(tail, data + i, rest);
    tail[rest] = 0x80;
    const size_t total = rest + 9 <= 64 ? 64 : 128;
    const uint64_t bits = (uint64_t)len * 8;
    for (int k = 0; k < 8; k++)
        tail[big_endian_len ? total - 1 - k : total - 8 + k] = (uint8_t)(bits >> (8 * k));
    block(h, tail);
    if (total == 128) block(h, tail + 64);
}

void mp_md5(const uint8_t *data, size_t len, uint8_t out[16]) {
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    md_run(h, md5_block, data, len, false);
    for (int i = 0; i < 16; i++) out[i] = (uint8_t)(h[i / 4] >> (8 * (i % 4)));
}

void mp_md5_init(mp_md5_ctx *c) {
    c->h[0] = 0x67452301, c->h[1] = 0xefcdab89, c->h[2] = 0x98badcfe, c->h[3] = 0x10325476;
    c->used = 0;
    c->total = 0;
}

void mp_md5_update(mp_md5_ctx *c, const uint8_t *data, size_t len) {
    c->total += len;
    while (len > 0) {
        const size_t n = 64 - c->used < len ? 64 - c->used : len;
        memcpy(c->buf + c->used, data, n);
        c->used += n, data += n, len -= n;
        if (c->used == 64) {
            md5_block(c->h, c->buf);
            c->used = 0;
        }
    }
}

void mp_md5_final(mp_md5_ctx *c, uint8_t out[16]) {
    const uint64_t bits = c->total * 8;
    const uint8_t one = 0x80, zero = 0;
    mp_md5_update(c, &one, 1);
    while (c->used != 56) mp_md5_update(c, &zero, 1);
    uint8_t len_le[8];
    for (int k = 0; k < 8; k++) len_le[k] = (uint8_t)(bits >> (8 * k));
    mp_md5_update(c, len_le, 8);
    for (int i = 0; i < 16; i++) out[i] = (uint8_t)(c->h[i / 4] >> (8 * (i % 4)));
}

// ---------------------------------------------------------------- AES-128 (FIPS 197)

static const uint8_t SBOX[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9,
    0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f,
    0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07,
    0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3,
    0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58,
    0xcf, 0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3,
    0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f,
    0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73, 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
    0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac,
    0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a,
    0xae, 0x08, 0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70,
    0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11,
    0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf, 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42,
    0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};
static uint8_t INV_SBOX[256];
static bool s_inv_ready;

static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }
static uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t r = 0;
    while (b) {
        if (b & 1) r ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return r;
}

static void aes_expand(const uint8_t key[16], uint8_t rk[176]) {
    if (!s_inv_ready) {  // first use; a second task doing it at the same time writes the same bytes
        for (int i = 0; i < 256; i++) INV_SBOX[SBOX[i]] = (uint8_t)i;
        s_inv_ready = true;
    }
    memcpy(rk, key, 16);
    uint8_t rcon = 1;
    for (int i = 16; i < 176; i += 4) {
        uint8_t t[4] = {rk[i - 4], rk[i - 3], rk[i - 2], rk[i - 1]};
        if (i % 16 == 0) {
            const uint8_t u = t[0];
            t[0] = SBOX[t[1]] ^ rcon, t[1] = SBOX[t[2]], t[2] = SBOX[t[3]], t[3] = SBOX[u];
            rcon = xtime(rcon);
        }
        for (int k = 0; k < 4; k++) rk[i + k] = rk[i - 16 + k] ^ t[k];
    }
}

static void aes_encrypt_block(const uint8_t rk[176], uint8_t s[16]) {
    for (int i = 0; i < 16; i++) s[i] ^= rk[i];
    for (int round = 1; round <= 10; round++) {
        uint8_t t[16];
        for (int i = 0; i < 16; i++) t[i] = SBOX[s[i]];
        // ShiftRows: column-major state, row r shifts left by r
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++) s[c * 4 + r] = t[((c + r) % 4) * 4 + r];
        if (round < 10) {
            for (int c = 0; c < 4; c++) {
                uint8_t *col = s + c * 4;
                const uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                col[0] = xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3;
                col[1] = a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3;
                col[2] = a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3);
                col[3] = (xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3);
            }
        }
        for (int i = 0; i < 16; i++) s[i] ^= rk[round * 16 + i];
    }
}

static void aes_decrypt_block(const uint8_t rk[176], uint8_t s[16]) {
    for (int i = 0; i < 16; i++) s[i] ^= rk[160 + i];
    for (int round = 9; round >= 0; round--) {
        uint8_t t[16];
        // InvShiftRows: row r shifts right by r
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++) t[((c + r) % 4) * 4 + r] = s[c * 4 + r];
        for (int i = 0; i < 16; i++) s[i] = INV_SBOX[t[i]] ^ rk[round * 16 + i];
        if (round > 0) {
            for (int c = 0; c < 4; c++) {
                uint8_t *col = s + c * 4;
                const uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                col[0] = gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9);
                col[1] = gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13);
                col[2] = gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11);
                col[3] = gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14);
            }
        }
    }
}

void mp_aes128_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16], const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t rk[176], chain[16];
    aes_expand(key, rk);
    memcpy(chain, iv, 16);
    for (size_t off = 0; off + 16 <= len; off += 16) {
        for (int i = 0; i < 16; i++) chain[i] ^= in[off + i];
        aes_encrypt_block(rk, chain);
        memcpy(out + off, chain, 16);
    }
}

void mp_aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16], const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t rk[176], chain[16], block[16];
    aes_expand(key, rk);
    memcpy(chain, iv, 16);
    for (size_t off = 0; off + 16 <= len; off += 16) {
        memcpy(block, in + off, 16);
        uint8_t s[16];
        memcpy(s, block, 16);
        aes_decrypt_block(rk, s);
        for (int i = 0; i < 16; i++) out[off + i] = s[i] ^ chain[i];
        memcpy(chain, block, 16);
    }
}

// ---------------------------------------------------------------- hex

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool mp_hex_decode(const char *hex, uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        const int hi = hex_value(hex[2 * i]), lo = hi < 0 ? -1 : hex_value(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return true;
}

void mp_hex_encode(const uint8_t *data, size_t n, char *out) {
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) out[2 * i] = H[data[i] >> 4], out[2 * i + 1] = H[data[i] & 15];
    out[2 * n] = 0;
}
