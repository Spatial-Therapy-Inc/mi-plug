// See plug_logic.h.
#include "plug_logic.h"

#include <stdio.h>
#include <string.h>

#include "mjson.h"

static const char *const POWER_MODELS[] = {
    "chuangmi.plug.m1", "chuangmi.plug.v2", "chuangmi.plug.v3", "chuangmi.plug.m3", "chuangmi.plug.hmi205",
    "chuangmi.plug.hmi206", "chuangmi.plug.hmi208", "qmi.powerstrip.v1", "zimi.powerstrip.v2",
};

static size_t bounded_len(const char *s, size_t max) {
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

plug_kind_t plug_kind(const char *model) {
    if (!model) return PLUG_MIOT;
    if (!strcmp(model, "chuangmi.plug.v1")) return PLUG_ON;
    for (size_t i = 0; i < sizeof(POWER_MODELS) / sizeof(POWER_MODELS[0]); i++)
        if (!strcmp(model, POWER_MODELS[i])) return PLUG_POWER;
    return PLUG_MIOT;
}

bool plug_ip_ok(const char *ip) {
    int parts = 0, value = -1, digits = 0;
    for (const char *p = ip;; p++) {
        if (*p >= '0' && *p <= '9') {
            value = (value < 0 ? 0 : value) * 10 + (*p - '0');
            if (++digits > 3 || value > 255) return false;
        } else if (*p == '.' || *p == 0) {
            if (value < 0) return false;
            parts++;
            value = -1, digits = 0;
            if (*p == 0) break;
        } else {
            return false;
        }
    }
    return parts == 4;
}

const char *plug_check(const plug_t *p) {
    const size_t n = bounded_len(p->name, PLUG_NAME_MAX);
    if (n == 0 || n >= PLUG_NAME_MAX) return "name";
    if (!plug_ip_ok(p->ip)) return "ip";
    if (bounded_len(p->token, sizeof(p->token)) != 32) return "token";
    for (int i = 0; i < 32; i++) {
        const char c = p->token[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return "token";
    }
    if (plug_kind(p->model) == PLUG_MIOT && (p->siid == 0 || p->piid == 0)) return "siid";
    return NULL;
}

bool plug_list_put(plug_list_t *list, const plug_t *p) {
    const int at = plug_list_find(list, p->name);
    if (at >= 0 && !strcmp(list->items[at].name, p->name)) {
        list->items[at] = *p;
        return true;
    }
    if (list->count >= PLUG_MAX) return false;
    list->items[list->count++] = *p;
    return true;
}

bool plug_list_remove(plug_list_t *list, const char *name) {
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].name, name)) continue;
        memmove(&list->items[i], &list->items[i + 1], (size_t)(list->count - i - 1) * sizeof(plug_t));
        list->count--;
        memset(&list->items[list->count], 0, sizeof(plug_t));
        return true;
    }
    return false;
}

int plug_list_find(const plug_list_t *list, const char *name) {
    if (!name || !*name) return -1;
    for (int i = 0; i < list->count; i++)
        if (!strcmp(list->items[i].name, name)) return i;
    // 「台灯」 for 「卧室的台灯」: the name ends with what was said, after "的"
    const size_t n = strlen(name);
    for (int i = 0; i < list->count; i++) {
        const char *full = list->items[i].name;
        const size_t m = strlen(full);
        if (m > n + 3 && !strcmp(full + m - n, name) && !memcmp(full + m - n - 3, "的", 3)) return i;
    }
    return -1;
}

static size_t fit(int n, size_t cap) { return n > 0 && (size_t)n < cap ? (size_t)n : 0; }

size_t plug_req_get(const plug_t *p, uint32_t id, char *out, size_t cap) {
    switch (plug_kind(p->model)) {
    case PLUG_POWER:
        return fit(snprintf(out, cap, "{\"id\":%u,\"method\":\"get_prop\",\"params\":[\"power\"]}", (unsigned)id), cap);
    case PLUG_ON:
        return fit(snprintf(out, cap, "{\"id\":%u,\"method\":\"get_prop\",\"params\":[\"on\"]}", (unsigned)id), cap);
    default:
        return fit(snprintf(out, cap,
                            "{\"id\":%u,\"method\":\"get_properties\",\"params\":[{\"did\":\"power\",\"siid\":%u,"
                            "\"piid\":%u}]}",
                            (unsigned)id, p->siid, p->piid),
                   cap);
    }
}

size_t plug_req_set(const plug_t *p, uint32_t id, bool on, char *out, size_t cap) {
    switch (plug_kind(p->model)) {
    case PLUG_POWER:
        return fit(snprintf(out, cap, "{\"id\":%u,\"method\":\"set_power\",\"params\":[\"%s\"]}", (unsigned)id,
                            on ? "on" : "off"),
                   cap);
    case PLUG_ON:
        return fit(snprintf(out, cap, "{\"id\":%u,\"method\":\"%s\",\"params\":[]}", (unsigned)id,
                            on ? "set_on" : "set_off"),
                   cap);
    default:
        return fit(snprintf(out, cap,
                            "{\"id\":%u,\"method\":\"set_properties\",\"params\":[{\"did\":\"power\",\"siid\":%u,"
                            "\"piid\":%u,\"value\":%s}]}",
                            (unsigned)id, p->siid, p->piid, on ? "true" : "false"),
                   cap);
    }
}

plug_reply_t plug_reply(plug_kind_t kind, const char *json, size_t len, bool *on, uint32_t *id) {
    const char *end = json + len;
    if (id) {
        long long n = 0;
        const char *v = mj_get(json, end, "id");
        *id = v && mj_long(v, end, &n) && n > 0 ? (uint32_t)n : 0;
    }
    if (!mj_ws(json, end) || mj_ws(json, end) >= end || *mj_ws(json, end) != '{') return PLUG_REPLY_MALFORMED;
    if (mj_get(json, end, "error")) return PLUG_REPLY_ERROR;
    const char *result = mj_get(json, end, "result");
    const char *first = result ? mj_first(result, end) : NULL;
    if (!first) return result ? PLUG_REPLY_ERROR : PLUG_REPLY_MALFORMED;
    if (kind == PLUG_MIOT) {
        // [{"did":"power","siid":2,"piid":1,"code":0,"value":true}]
        long long code = 0;
        const char *c = mj_get(first, end, "code");
        if (c && (!mj_long(c, end, &code) || code != 0)) return PLUG_REPLY_ERROR;
        const char *v = mj_get(first, end, "value");
        if (v && on) {
            bool ok;
            const bool value = mj_bool(v, end, &ok);
            if (!ok) return PLUG_REPLY_MALFORMED;
            *on = value;
        }
        return PLUG_REPLY_OK;
    }
    // miIO: ["ok"] after a set; ["on"] / ["off"] or [true] / [false] after a get
    char text[8];
    if (mj_text(first, end, text, sizeof(text)) >= 0) {
        if (!strcmp(text, "ok")) return PLUG_REPLY_OK;
        if (!strcmp(text, "on") || !strcmp(text, "off")) {
            if (on) *on = !strcmp(text, "on");
            return PLUG_REPLY_OK;
        }
        return PLUG_REPLY_ERROR;
    }
    bool ok;
    const bool value = mj_bool(first, end, &ok);
    if (!ok) return PLUG_REPLY_MALFORMED;
    if (on) *on = value;
    return PLUG_REPLY_OK;
}
