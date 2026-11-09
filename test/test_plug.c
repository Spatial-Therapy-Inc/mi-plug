// home-switch host tests: the crypto against known vectors, miIO packets byte for byte against a Python reference
// (python-miio's algorithm), the JSON reader, the device list and the requests and answers of each kind of device.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../main/miio_proto.h"
#include "../main/mjson.h"
#include "../main/mp_crypto.h"
#include "../main/plug_logic.h"

static int s_checks;
#define CHECK(x)                                                         \
    do {                                                                 \
        s_checks++;                                                      \
        if (!(x)) {                                                      \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); \
            return 1;                                                    \
        }                                                                \
    } while (0)

static void hex(const uint8_t *d, size_t n, char *out) { mp_hex_encode(d, n, out); }

static int test_crypto(void) {
    uint8_t md[16];
    char text[512];
    mp_md5((const uint8_t *)"", 0, md);
    hex(md, 16, text);
    CHECK(!strcmp(text, "d41d8cd98f00b204e9800998ecf8427e"));
    mp_md5((const uint8_t *)"The quick brown fox jumps over the lazy dog", 43, md);
    hex(md, 16, text);
    CHECK(!strcmp(text, "9e107d9d372bb6826bd81d3542a419d6"));
    uint8_t a200[200];
    memset(a200, 'a', sizeof(a200));
    mp_md5(a200, sizeof(a200), md);
    hex(md, 16, text);
    CHECK(!strcmp(text, "887f30b43b2867f4a9accceee7d16e6c"));
    mp_md5_ctx c;  // in parts, across block edges
    mp_md5_init(&c);
    mp_md5_update(&c, a200, 63);
    mp_md5_update(&c, a200, 1);
    mp_md5_update(&c, a200, 136);
    mp_md5_final(&c, md);
    hex(md, 16, text);
    CHECK(!strcmp(text, "887f30b43b2867f4a9accceee7d16e6c"));

    // FIPS-197 C.1, twice through CBC with a zero IV
    uint8_t key[16], iv[16] = {0}, block[32], back[32];
    CHECK(mp_hex_decode("000102030405060708090a0b0c0d0e0f", key, 16));
    CHECK(mp_hex_decode("00112233445566778899aabbccddeeff", block, 16));
    memcpy(block + 16, block, 16);
    uint8_t enc[32];
    mp_aes128_cbc_encrypt(key, iv, block, 32, enc);
    hex(enc, 32, text);
    CHECK(!strcmp(text, "69c4e0d86a7b0430d8cdb78070b4c55a7d7786be32d059a60ca8021a65dd9f09"));
    mp_aes128_cbc_decrypt(key, iv, enc, 32, back);
    CHECK(!memcmp(back, block, 32));
    CHECK(!mp_hex_decode("0g", key, 1));
    return 0;
}

static const char *TOKEN_HEX = "00112233445566778899aabbccddeeff";
static const char *PKT_HEX =
    "213100800000000024a1b3c40001e2401ccd6604dd5a17fcd8e25e720c1ba3937fe3a69e29c435227b660672ab46b6d92f3b34a3daaca2"
    "668064f8e48186e0553803daaf873ecffc135589dedd19d82a54b82baae970354142c4f7eeef2bb709cc1a87b904ce223bb51b9a604ab2"
    "6ff74d421f60bdbd06b23cbbd40d6d6e4b30";
static const char *ANS_HEX =
    "213100700000000024a1b3c40001e245a9c30e5152919d89d396e13e2222b4e8ab60548ea4226dbb3bc4c2969ebf70a096af0b1e2699c3"
    "dbdf5d2bbe8addf327ed35cce877073d62d2b3836e2e09a5016b2288266d55b497e41dbe35fc5ee7fb109e8def3507cb7a9a9a556e6b15"
    "c4cb";

static int test_miio(void) {
    uint8_t token[16], pkt[256], ref[256];
    char text[600];
    CHECK(mp_hex_decode(TOKEN_HEX, token, 16));

    uint8_t hello[32];
    miio_hello(hello);
    hex(hello, 32, text);
    CHECK(!strcmp(text, "21310020ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));

    plug_t p = {.name = "台灯", .ip = "192.168.31.20", .model = "cuco.plug.v3", .siid = 2, .piid = 1};
    char json[256];
    CHECK(plug_req_set(&p, 7, true, json, sizeof(json)) > 0);
    CHECK(!strcmp(json, "{\"id\":7,\"method\":\"set_properties\",\"params\":[{\"did\":\"power\",\"siid\":2,\"piid\":1,"
                        "\"value\":true}]}"));
    const size_t n = miio_pack(token, 0x24a1b3c4, 0x0001e240, json, pkt, sizeof(pkt));
    CHECK(n == 128 && n == miio_packet_size(strlen(json)));
    CHECK(mp_hex_decode(PKT_HEX, ref, n));
    CHECK(!memcmp(pkt, ref, n));
    CHECK(miio_pack(token, 1, 1, json, pkt, 100) == 0);  // too small

    // the device's answer
    const size_t an = strlen(ANS_HEX) / 2;
    CHECK(mp_hex_decode(ANS_HEX, ref, an));
    miio_header_t h;
    CHECK(miio_parse_header(ref, an, &h) && h.device_id == 0x24a1b3c4 && h.stamp == 0x0001e245 && h.length == an);
    char out[300];
    const int len = miio_unpack(token, ref, an, out, sizeof(out));
    CHECK(len > 0 && !strcmp(out, "{\"result\":[{\"did\":\"power\",\"siid\":2,\"piid\":1,\"code\":0,\"value\":true}],\"id\":7}"));
    bool on = false;
    uint32_t id = 0;
    CHECK(plug_reply(PLUG_MIOT, out, (size_t)len, &on, &id) == PLUG_REPLY_OK && on && id == 7);

    uint8_t wrong[16];
    memcpy(wrong, token, 16);
    wrong[0] ^= 1;
    CHECK(miio_unpack(wrong, ref, an, out, sizeof(out)) == -2);  // a wrong token
    CHECK(miio_unpack(token, ref, an, out, 20) == -3);
    ref[0] = 0;
    CHECK(miio_unpack(token, ref, an, out, sizeof(out)) == -1);
    CHECK(!miio_parse_header(hello, 10, &h));
    return 0;
}

static int test_json(void) {
    const char *doc = " {\"a\" : [1, {\"b\":\"x\\\"y\"}, true], \"name\":\"\\u53f0\\u706f\", \"n\": -12, \"s\":\"\\ud83d\\ude00\"} ";
    const char *end = doc + strlen(doc);
    char t[32];
    const char *a = mj_get(doc, end, "a");
    CHECK(a && *a == '[');
    const char *e1 = mj_first(a, end), *e2 = mj_next(e1, end), *e3 = mj_next(e2, end);
    CHECK(e1 && e2 && e3 && !mj_next(e3, end));
    CHECK(mj_text(mj_get(e2, end, "b"), end, t, sizeof(t)) == 3 && !strcmp(t, "x\"y"));
    bool ok;
    CHECK(mj_bool(e3, end, &ok) && ok);
    CHECK(mj_text(mj_get(doc, end, "name"), end, t, sizeof(t)) == 6 && !strcmp(t, "台灯"));
    CHECK(mj_text(mj_get(doc, end, "s"), end, t, sizeof(t)) == 4 && !strcmp(t, "\xf0\x9f\x98\x80"));
    long long n;
    CHECK(mj_long(mj_get(doc, end, "n"), end, &n) && n == -12);
    CHECK(!mj_get(doc, end, "missing"));
    CHECK(mj_text(mj_get(doc, end, "name"), end, t, 4) == -1);  // too long
    const char *bad = "{\"a\":[1,2";
    CHECK(!mj_skip(mj_get(bad, bad + strlen(bad), "a"), bad + strlen(bad)));
    return 0;
}

static int test_list_and_kinds(void) {
    CHECK(plug_kind("cuco.plug.v3") == PLUG_MIOT);
    CHECK(plug_kind("chuangmi.plug.212a01") == PLUG_MIOT);
    CHECK(plug_kind("chuangmi.plug.m1") == PLUG_POWER);
    CHECK(plug_kind("chuangmi.plug.v1") == PLUG_ON);
    CHECK(plug_kind("") == PLUG_MIOT && plug_kind(NULL) == PLUG_MIOT);

    CHECK(plug_ip_ok("192.168.31.20") && plug_ip_ok("10.0.0.1"));
    CHECK(!plug_ip_ok("192.168.31") && !plug_ip_ok("192.168.31.256") && !plug_ip_ok("1..2.3") && !plug_ip_ok("a.b.c.d") &&
          !plug_ip_ok("1.2.3.4.5") && !plug_ip_ok(""));

    plug_t p = {.name = "卧室的台灯", .ip = "192.168.31.20", .token = "00112233445566778899AABBCCDDEEFF",
                .model = "yeelink.light.lamp4", .siid = 2, .piid = 1};
    CHECK(plug_check(&p) == NULL);
    plug_t q = p;
    q.token[5] = 'x';
    CHECK(!strcmp(plug_check(&q), "token"));
    q = p;
    strcpy(q.ip, "192.168.1");
    CHECK(!strcmp(plug_check(&q), "ip"));
    q = p;
    q.siid = 0;
    CHECK(!strcmp(plug_check(&q), "siid"));
    q = p;
    q.name[0] = 0;
    CHECK(!strcmp(plug_check(&q), "name"));

    plug_list_t list = {0};
    CHECK(plug_list_put(&list, &p) && list.count == 1);
    q = p;
    strcpy(q.ip, "192.168.31.21");
    CHECK(plug_list_put(&list, &q) && list.count == 1 && !strcmp(list.items[0].ip, "192.168.31.21"));  // replaced
    CHECK(plug_list_find(&list, "卧室的台灯") == 0 && plug_list_find(&list, "台灯") == 0);
    CHECK(plug_list_find(&list, "灯") == -1 && plug_list_find(&list, "") == -1);
    for (int i = 1; i < PLUG_MAX; i++) {
        snprintf(q.name, sizeof(q.name), "插座%d", i);
        CHECK(plug_list_put(&list, &q));
    }
    strcpy(q.name, "多一个");
    CHECK(!plug_list_put(&list, &q) && list.count == PLUG_MAX);
    CHECK(plug_list_remove(&list, "卧室的台灯") && list.count == PLUG_MAX - 1 && !strcmp(list.items[0].name, "插座1"));
    CHECK(!plug_list_remove(&list, "卧室的台灯"));
    return 0;
}

static int test_requests(void) {
    char j[200];
    plug_t m1 = {.model = "chuangmi.plug.m1"}, v1 = {.model = "chuangmi.plug.v1"}, mi = {.model = "x", .siid = 3, .piid = 2};
    CHECK(plug_req_get(&m1, 1, j, sizeof(j)) && !strcmp(j, "{\"id\":1,\"method\":\"get_prop\",\"params\":[\"power\"]}"));
    CHECK(plug_req_set(&m1, 2, false, j, sizeof(j)) && !strcmp(j, "{\"id\":2,\"method\":\"set_power\",\"params\":[\"off\"]}"));
    CHECK(plug_req_get(&v1, 3, j, sizeof(j)) && !strcmp(j, "{\"id\":3,\"method\":\"get_prop\",\"params\":[\"on\"]}"));
    CHECK(plug_req_set(&v1, 4, true, j, sizeof(j)) && !strcmp(j, "{\"id\":4,\"method\":\"set_on\",\"params\":[]}"));
    CHECK(plug_req_get(&mi, 5, j, sizeof(j)) &&
          !strcmp(j, "{\"id\":5,\"method\":\"get_properties\",\"params\":[{\"did\":\"power\",\"siid\":3,\"piid\":2}]}"));
    CHECK(plug_req_set(&mi, 6, true, j, 20) == 0);  // too small

    bool on = true;
    uint32_t id;
#define REPLY(kind, text) plug_reply(kind, text, strlen(text), &on, &id)
    CHECK(REPLY(PLUG_POWER, "{\"result\":[\"off\"],\"id\":9}") == PLUG_REPLY_OK && !on && id == 9);
    CHECK(REPLY(PLUG_POWER, "{\"result\":[\"ok\"],\"id\":10}") == PLUG_REPLY_OK);
    CHECK(REPLY(PLUG_ON, "{\"result\":[true],\"id\":11}") == PLUG_REPLY_OK && on);
    CHECK(REPLY(PLUG_MIOT, "{\"result\":[{\"did\":\"power\",\"siid\":2,\"piid\":1,\"code\":0}],\"id\":12}") == PLUG_REPLY_OK);
    CHECK(REPLY(PLUG_MIOT, "{\"result\":[{\"did\":\"power\",\"siid\":9,\"piid\":9,\"code\":-4003}],\"id\":13}") == PLUG_REPLY_ERROR);
    CHECK(REPLY(PLUG_MIOT, "{\"error\":{\"code\":-9999,\"message\":\"user ack timeout\"},\"id\":14}") == PLUG_REPLY_ERROR && id == 14);
    CHECK(REPLY(PLUG_MIOT, "{\"result\":[],\"id\":15}") == PLUG_REPLY_ERROR);
    CHECK(REPLY(PLUG_MIOT, "not json") == PLUG_REPLY_MALFORMED);
#undef REPLY
    return 0;
}

int main(void) {
    if (test_crypto() || test_miio() || test_json() || test_list_and_kinds() || test_requests()) return 1;
    printf("home-switch host tests: %d checks passed\n", s_checks);
    return 0;
}
