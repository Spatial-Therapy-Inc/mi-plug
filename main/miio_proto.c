// See miio_proto.h.
#include "miio_proto.h"

#include <string.h>

#include "mp_crypto.h"

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8), p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static void keys(const uint8_t token[MIIO_TOKEN_LEN], uint8_t key[16], uint8_t iv[16]) {
    mp_md5(token, MIIO_TOKEN_LEN, key);
    uint8_t both[32];
    memcpy(both, key, 16);
    memcpy(both + 16, token, 16);
    mp_md5(both, sizeof(both), iv);
}

/** MD5 over header[0..16] + token + payload (the checksum field itself is left out). */
static void checksum(const uint8_t *pkt, size_t len, const uint8_t token[MIIO_TOKEN_LEN], uint8_t out[16]) {
    mp_md5_ctx c;
    mp_md5_init(&c);
    mp_md5_update(&c, pkt, 16);
    mp_md5_update(&c, token, MIIO_TOKEN_LEN);
    mp_md5_update(&c, pkt + MIIO_HEADER, len - MIIO_HEADER);
    mp_md5_final(&c, out);
}

void miio_hello(uint8_t out[MIIO_HEADER]) {
    memset(out, 0xff, MIIO_HEADER);
    put16(out, 0x2131);
    put16(out + 2, MIIO_HEADER);
}

bool miio_parse_header(const uint8_t *pkt, size_t len, miio_header_t *out) {
    if (len < MIIO_HEADER || pkt[0] != 0x21 || pkt[1] != 0x31) return false;
    out->length = (uint16_t)(pkt[2] << 8 | pkt[3]);
    if (out->length < MIIO_HEADER || out->length > len) return false;
    out->unknown = get32(pkt + 4);
    out->device_id = get32(pkt + 8);
    out->stamp = get32(pkt + 12);
    return true;
}

size_t miio_packet_size(size_t json_len) { return MIIO_HEADER + (json_len + 1) / 16 * 16 + 16; }

size_t miio_pack(const uint8_t token[MIIO_TOKEN_LEN], uint32_t device_id, uint32_t stamp, const char *json,
                 uint8_t *out, size_t cap) {
    const size_t plain = strlen(json) + 1;  // the NUL goes too
    const size_t padded = plain / 16 * 16 + 16;
    const size_t total = MIIO_HEADER + padded;
    if (total > cap || padded > 4096) return 0;
    uint8_t *payload = out + MIIO_HEADER;
    memcpy(payload, json, plain);
    memset(payload + plain, (int)(padded - plain), padded - plain);  // PKCS#7
    uint8_t key[16], iv[16];
    keys(token, key, iv);
    mp_aes128_cbc_encrypt(key, iv, payload, padded, payload);
    put16(out, 0x2131);
    put16(out + 2, (uint16_t)total);
    put32(out + 4, 0);
    put32(out + 8, device_id);
    put32(out + 12, stamp);
    uint8_t sum[16];
    checksum(out, total, token, sum);
    memcpy(out + 16, sum, 16);
    return total;
}

int miio_unpack(const uint8_t token[MIIO_TOKEN_LEN], const uint8_t *pkt, size_t len, char *json, size_t cap) {
    miio_header_t h;
    if (!miio_parse_header(pkt, len, &h)) return -1;
    const size_t payload = h.length - MIIO_HEADER;
    if (payload == 0 || payload % 16 || payload > 4096) return -1;
    uint8_t sum[16];
    checksum(pkt, h.length, token, sum);
    if (memcmp(sum, pkt + 16, 16)) return -2;
    if (payload + 1 > cap) return -3;
    uint8_t key[16], iv[16];
    keys(token, key, iv);
    mp_aes128_cbc_decrypt(key, iv, pkt + MIIO_HEADER, payload, (uint8_t *)json);
    size_t n = payload;
    const uint8_t pad = (uint8_t)json[n - 1];
    if (pad >= 1 && pad <= 16) n -= pad;
    while (n > 0 && json[n - 1] == 0) n--;  // the NUL the device sends (some send more)
    json[n] = 0;
    return (int)n;
}
