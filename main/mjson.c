// See mjson.h.
#include "mjson.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

const char *mj_ws(const char *p, const char *end) {
    while (p && p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
    return p;
}

static const char *skip_string(const char *p, const char *end) {
    // p at the opening quote
    for (p++; p < end; p++) {
        if (*p == '\\') {
            p++;
            continue;
        }
        if (*p == '"') return p + 1;
    }
    return NULL;
}

const char *mj_skip(const char *p, const char *end) {
    p = mj_ws(p, end);
    if (!p || p >= end) return NULL;
    if (*p == '"') return skip_string(p, end);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (p < end) {
            if (*p == '"') {
                p = skip_string(p, end);
                if (!p) return NULL;
                continue;
            }
            if (*p == '{' || *p == '[') depth++;
            else if (*p == '}' || *p == ']') {
                if (--depth == 0) return p + 1;
            }
            p++;
        }
        return NULL;
    }
    // a number, true, false or null
    const char *start = p;
    while (p < end && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
    return p > start ? p : NULL;
}

const char *mj_get(const char *obj, const char *end, const char *key) {
    const char *p = mj_ws(obj, end);
    if (!p || p >= end || *p != '{') return NULL;
    const size_t key_len = strlen(key);
    p = mj_ws(p + 1, end);
    while (p && p < end && *p != '}') {
        if (*p != '"') return NULL;
        const char *k = p + 1, *after = skip_string(p, end);
        if (!after) return NULL;
        const bool match = (size_t)(after - 1 - k) == key_len && !memcmp(k, key, key_len);
        p = mj_ws(after, end);
        if (!p || p >= end || *p != ':') return NULL;
        const char *value = mj_ws(p + 1, end);
        if (match) return value;
        p = mj_ws(mj_skip(value, end), end);
        if (!p || p >= end) return NULL;
        if (*p == ',') p = mj_ws(p + 1, end);
    }
    return NULL;
}

const char *mj_path(const char *doc, const char *end, ...) {
    va_list ap;
    va_start(ap, end);
    const char *v = doc, *key;
    while (v && (key = va_arg(ap, const char *)) != NULL) v = mj_get(v, end, key);
    va_end(ap);
    return v;
}

const char *mj_first(const char *arr, const char *end) {
    const char *p = mj_ws(arr, end);
    if (!p || p >= end || *p != '[') return NULL;
    p = mj_ws(p + 1, end);
    return p && p < end && *p != ']' ? p : NULL;
}

const char *mj_next(const char *item, const char *end) {
    const char *p = mj_ws(mj_skip(item, end), end);
    if (!p || p >= end || *p != ',') return NULL;
    return mj_ws(p + 1, end);
}

static size_t utf8(unsigned cp, char *out) {
    if (cp < 0x80) return out[0] = (char)cp, 1;
    if (cp < 0x800) return out[0] = (char)(0xc0 | cp >> 6), out[1] = (char)(0x80 | (cp & 63)), 2;
    if (cp < 0x10000) {
        out[0] = (char)(0xe0 | cp >> 12), out[1] = (char)(0x80 | ((cp >> 6) & 63)), out[2] = (char)(0x80 | (cp & 63));
        return 3;
    }
    out[0] = (char)(0xf0 | cp >> 18), out[1] = (char)(0x80 | ((cp >> 12) & 63));
    out[2] = (char)(0x80 | ((cp >> 6) & 63)), out[3] = (char)(0x80 | (cp & 63));
    return 4;
}

static int hex4(const char *p, const char *end, unsigned *out) {
    if (end - p < 4) return -1;
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        const char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return -1;
    }
    *out = v;
    return 0;
}

int mj_text(const char *v, const char *end, char *out, size_t cap) {
    v = mj_ws(v, end);
    if (!v || v >= end || cap == 0) return -1;
    size_t o = 0;
    if (*v != '"') {
        const char *after = mj_skip(v, end);
        if (!after || !((*v >= '0' && *v <= '9') || *v == '-')) return -1;
        const size_t n = (size_t)(after - v);
        if (n + 1 > cap) return -1;
        memcpy(out, v, n);
        out[n] = 0;
        return (int)n;
    }
    for (const char *p = v + 1; p < end; p++) {
        char tmp[4];
        size_t n = 1;
        if (*p == '"') {
            out[o] = 0;
            return (int)o;
        }
        if (*p == '\\') {
            if (++p >= end) return -1;
            switch (*p) {
            case 'n': tmp[0] = '\n'; break;
            case 't': tmp[0] = '\t'; break;
            case 'r': tmp[0] = '\r'; break;
            case 'b': tmp[0] = '\b'; break;
            case 'f': tmp[0] = '\f'; break;
            case 'u': {
                unsigned cp;
                if (hex4(p + 1, end, &cp)) return -1;
                p += 4;
                if (cp >= 0xd800 && cp < 0xdc00 && end - p > 6 && p[1] == '\\' && p[2] == 'u') {
                    unsigned lo;
                    if (!hex4(p + 3, end, &lo) && lo >= 0xdc00 && lo < 0xe000) {
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                        p += 6;
                    }
                }
                n = utf8(cp, tmp);
                break;
            }
            default: tmp[0] = *p; break;  // \" \\ \/
            }
        } else {
            tmp[0] = *p;
        }
        if (o + n + 1 > cap) return -1;
        memcpy(out + o, tmp, n);
        o += n;
    }
    return -1;
}

bool mj_bool(const char *v, const char *end, bool *ok) {
    v = mj_ws(v, end);
    bool good = false, value = false;
    if (v && end - v >= 4 && !memcmp(v, "true", 4)) good = value = true;
    else if (v && end - v >= 5 && !memcmp(v, "false", 5)) good = true;
    else {
        long long n;
        if (v && *v != '"' && mj_long(v, end, &n)) good = true, value = n != 0;
    }
    if (ok) *ok = good;
    return value;
}

bool mj_long(const char *v, const char *end, long long *out) {
    char buf[24];
    if (mj_text(v, end, buf, sizeof(buf)) <= 0) return false;
    char *stop;
    const long long n = strtoll(buf, &stop, 10);
    if (*stop) return false;
    *out = n;
    return true;
}
