// The miIO LAN protocol (UDP 54321), plain C: the packets a Xiaomi Wi-Fi device takes on the home network.
//
//   header, 32 bytes, big-endian:
//     0  magic 0x2131      2  length (header + payload)      4  unknown (0; 0xFFFFFFFF in a hello)
//     8  device id         12 stamp (the device's seconds)   16 checksum: MD5(header[0..16] + token + payload)
//   payload: AES-128-CBC, key MD5(token), iv MD5(key + token), PKCS#7; the plain text is JSON and a NUL.
//
// A hello (32 bytes of header: magic, length 32, the rest 0xFF) is answered with a header carrying the device id and
// its stamp; requests then use that id and stamp + the seconds since.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIIO_PORT 54321
#define MIIO_HEADER 32
#define MIIO_TOKEN_LEN 16

typedef struct {
    uint16_t length;
    uint32_t unknown;
    uint32_t device_id;
    uint32_t stamp;
} miio_header_t;

void miio_hello(uint8_t out[MIIO_HEADER]);
/** False unless it starts with the magic and its length fits `len`. */
bool miio_parse_header(const uint8_t *pkt, size_t len, miio_header_t *out);
/** The payload a JSON text becomes (encrypted, padded): its size for a buffer. */
size_t miio_packet_size(size_t json_len);
/** A request: returns the packet's length, 0 when `cap` is too small. */
size_t miio_pack(const uint8_t token[MIIO_TOKEN_LEN], uint32_t device_id, uint32_t stamp, const char *json,
                 uint8_t *out, size_t cap);
/** An answer into `json` (NUL-terminated, trailing NULs dropped). Returns its length; -1 malformed, -2 the checksum
 *  does not match the token (a wrong token), -3 `cap` too small. */
int miio_unpack(const uint8_t token[MIIO_TOKEN_LEN], const uint8_t *pkt, size_t len, char *json, size_t cap);

#ifdef __cplusplus
}
#endif
