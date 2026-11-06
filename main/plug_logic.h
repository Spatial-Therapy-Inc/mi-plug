// mi-plug's logic in plain C (host tests run it): the devices the user added, how each model is switched, the JSON
// requests sent over miIO and how their answers read.
//
// A device is a Xiaomi Wi-Fi device on the home network: its IP, its 32-hex token (the user takes it once from their
// own account, README), its model, and which property is its power (MIoT siid/piid, 2/1 for most plugs, lights,
// fans). The token is a secret: nothing here prints it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLUG_MAX 8
#define PLUG_NAME_MAX 48

typedef enum {
    PLUG_MIOT,   // set_properties / get_properties on siid, piid (most devices since 2019)
    PLUG_POWER,  // miIO set_power ["on"|"off"], get_prop ["power"] (chuangmi.plug.m1/v2/v3/m3/hmi*, powerstrips)
    PLUG_ON,     // miIO set_on / set_off, get_prop ["on"] (chuangmi.plug.v1)
} plug_kind_t;

typedef struct {
    char name[PLUG_NAME_MAX];
    char ip[16];
    char token[33];
    char model[48];
    uint32_t device_id;  // learnt from the device's hello (0 until then); finds it again when its IP changes
    uint8_t siid, piid;
} plug_t;

typedef struct {
    uint8_t count;
    plug_t items[PLUG_MAX];
} plug_list_t;

/** How `model` is switched (unknown models: MIoT). */
plug_kind_t plug_kind(const char *model);

/** A device to add, checked: a name (1..47 bytes), an IPv4 address, 32 hex characters of token. NULL when good,
 *  otherwise the field that is wrong ("name", "ip", "token", "siid"). */
const char *plug_check(const plug_t *p);
bool plug_ip_ok(const char *ip);

/** Adds or replaces (same name) a device. False when the list is full. */
bool plug_list_put(plug_list_t *list, const plug_t *p);
/** Removes by name; false when there is none. */
bool plug_list_remove(plug_list_t *list, const char *name);
/** The index of `name` (exact, then ignoring a missing "的" prefix of rooms: "台灯" finds "卧室的台灯"), -1 when none. */
int plug_list_find(const plug_list_t *list, const char *name);

/** The request reading the power ({"id":…,"method":…,"params":…}). Returns the length, 0 when `cap` is too small. */
size_t plug_req_get(const plug_t *p, uint32_t id, char *out, size_t cap);
/** The request switching it on or off. */
size_t plug_req_set(const plug_t *p, uint32_t id, bool on, char *out, size_t cap);

typedef enum {
    PLUG_REPLY_OK,
    PLUG_REPLY_ERROR,      // the device answered with an error (a wrong siid/piid, a busy device)
    PLUG_REPLY_MALFORMED,  // not JSON we understand
} plug_reply_t;

/** An answer to plug_req_get / plug_req_set (`kind` the device's). For a get, `on` is the power (set only when
 *  OK); `id`, when not NULL, gets the answer's "id" (0 without one). */
plug_reply_t plug_reply(plug_kind_t kind, const char *json, size_t len, bool *on, uint32_t *id);

#ifdef __cplusplus
}
#endif
