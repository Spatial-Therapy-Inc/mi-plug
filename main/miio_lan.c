// See miio_lan.h.
#include "miio_lan.h"

#include <string.h>
#include <sys/time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "miio_proto.h"
#include "mp_crypto.h"

static const char *TAG = "miio";

#define ANSWER_MS 1500  // a device answers within a few hundred ms; a busy one later
#define PKT_MAX 1536

static uint32_t s_next_id;

static int open_socket(int timeout_ms, bool broadcast) {
    const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s < 0) return -1;
    struct timeval tv = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (broadcast) {
        const int on = 1;
        setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    }
    return s;
}

static bool to_addr(const char *ip, struct sockaddr_in *addr) {
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(MIIO_PORT);
    return inet_aton(ip, &addr->sin_addr) != 0;
}

/** A packet from `from` (other senders are skipped) until the socket's timeout. Returns its length, -1 on none. */
static int receive(int s, const struct sockaddr_in *from, uint8_t *buf, size_t cap) {
    for (int tries = 0; tries < 8; tries++) {
        struct sockaddr_in src;
        socklen_t src_len = sizeof(src);
        const int n = recvfrom(s, buf, cap, 0, (struct sockaddr *)&src, &src_len);
        if (n < 0) return -1;
        if (src.sin_addr.s_addr == from->sin_addr.s_addr) return n;
    }
    return -1;
}

static esp_err_t hello(int s, const struct sockaddr_in *addr, miio_header_t *h) {
    uint8_t pkt[MIIO_HEADER], buf[64];
    miio_hello(pkt);
    if (sendto(s, pkt, sizeof(pkt), 0, (const struct sockaddr *)addr, sizeof(*addr)) < 0) return ESP_FAIL;
    const int n = receive(s, addr, buf, sizeof(buf));
    if (n < 0) return ESP_ERR_TIMEOUT;
    return miio_parse_header(buf, (size_t)n, h) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

/** One request after a hello. ESP_OK with the answer in `json`. */
static esp_err_t request(int s, const struct sockaddr_in *addr, const uint8_t token[16], const miio_header_t *h,
                         const char *json, uint32_t id, char *answer, size_t cap, uint8_t *pkt) {
    // The device's stamp and a second: python-miio's way (a stamp behind the device's is dropped as a replay).
    const size_t n = miio_pack(token, h->device_id, h->stamp + 1, json, pkt, PKT_MAX);
    if (!n) return ESP_ERR_INVALID_SIZE;
    if (sendto(s, pkt, n, 0, (const struct sockaddr *)addr, sizeof(*addr)) < 0) return ESP_FAIL;
    for (int tries = 0; tries < 4; tries++) {  // an old answer (another id) may come first
        const int got = receive(s, addr, pkt, PKT_MAX);
        if (got < 0) return ESP_ERR_TIMEOUT;
        const int len = miio_unpack(token, pkt, (size_t)got, answer, cap);
        if (len == -2) return ESP_ERR_INVALID_CRC;
        if (len < 0) continue;
        uint32_t answer_id = 0;
        plug_reply(PLUG_MIOT, answer, (size_t)len, NULL, &answer_id);
        if (answer_id == id || answer_id == 0) return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t miio_lan_power(plug_t *p, bool set, bool on, bool *state) {
    uint8_t token[16];
    struct sockaddr_in addr;
    if (!mp_hex_decode(p->token, token, sizeof(token)) || !to_addr(p->ip, &addr)) return ESP_ERR_INVALID_ARG;
    if (!s_next_id) s_next_id = 1 + esp_random() % 10000;
    uint8_t *pkt = heap_caps_malloc(PKT_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *answer = heap_caps_malloc(PKT_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const int s = pkt && answer ? open_socket(ANSWER_MS, false) : -1;
    esp_err_t err = s < 0 ? ESP_ERR_NO_MEM : ESP_ERR_TIMEOUT;
    const plug_kind_t kind = plug_kind(p->model);
    for (int attempt = 0; s >= 0 && attempt < 2 && err == ESP_ERR_TIMEOUT; attempt++) {
        miio_header_t h;
        err = hello(s, &addr, &h);
        if (err != ESP_OK) continue;
        p->device_id = h.device_id;
        // A device remembers the last ids: after a retry, jump ahead (python-miio does +100 on "user ack timeout").
        const uint32_t id = s_next_id;
        s_next_id += attempt ? 100 : 1;
        char json[200];
        const size_t jn = set ? plug_req_set(p, id, on, json, sizeof(json)) : plug_req_get(p, id, json, sizeof(json));
        if (!jn) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        err = request(s, &addr, token, &h, json, id, answer, PKT_MAX, pkt);
        if (err != ESP_OK) continue;
        bool now = on;
        const plug_reply_t r = plug_reply(kind, answer, strlen(answer), &now, NULL);
        if (r == PLUG_REPLY_OK) {
            if (state) *state = now;  // after a set: the value asked for (the answer carries no value)
        } else {
            // Counts only, never the text (it may hold the device's name): review rule F-03.
            ESP_LOGW(TAG, "device said no (kind=%d reply=%d len=%u)", (int)kind, (int)r, (unsigned)strlen(answer));
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }
    if (s >= 0) close(s);
    heap_caps_free(pkt);
    heap_caps_free(answer);
    memset(token, 0, sizeof(token));
    return err;
}

esp_err_t miio_lan_find(uint32_t device_id, char ip[16], int wait_ms) {
    struct sockaddr_in addr;
    to_addr("255.255.255.255", &addr);
    const int s = open_socket(300, true);
    if (s < 0) return ESP_ERR_NO_MEM;
    uint8_t pkt[MIIO_HEADER], buf[64];
    miio_hello(pkt);
    esp_err_t err = ESP_ERR_NOT_FOUND;
    const int64_t until = esp_timer_get_time() + (int64_t)wait_ms * 1000;
    int64_t next_hello = 0;
    while (err != ESP_OK && esp_timer_get_time() < until) {
        if (esp_timer_get_time() >= next_hello) {  // say it a few times: UDP broadcasts get lost
            sendto(s, pkt, sizeof(pkt), 0, (const struct sockaddr *)&addr, sizeof(addr));
            next_hello = esp_timer_get_time() + 700 * 1000;
        }
        struct sockaddr_in src;
        socklen_t src_len = sizeof(src);
        const int n = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&src, &src_len);
        miio_header_t h;
        if (n > 0 && miio_parse_header(buf, (size_t)n, &h) && h.device_id == device_id) {
            inet_ntoa_r(src.sin_addr, ip, 16);
            err = ESP_OK;
        }
    }
    close(s);
    return err;
}
