// A tiny JSON reader over a text in place, plain C: no allocation, so a large answer (an account's device list)
// costs no internal RAM. Values are pointers into the text; `end` is one past its last character.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The first non-space character at or after p (end when none). */
const char *mj_ws(const char *p, const char *end);
/** One past the value starting at p (after spaces), NULL when malformed. */
const char *mj_skip(const char *p, const char *end);
/** In the object at `obj` ('{' after spaces), the value of `key`; NULL when missing or not an object. */
const char *mj_get(const char *obj, const char *end, const char *key);
/** Follows keys through nested objects: mj_path(doc, end, "result", "device_info", NULL). */
const char *mj_path(const char *doc, const char *end, ...);
/** The first element of the array at `arr`, NULL when empty or not an array. */
const char *mj_first(const char *arr, const char *end);
/** The element after `item` in its array, NULL after the last. */
const char *mj_next(const char *item, const char *end);
/** A string (unescaped, \\u to UTF-8) or a number's text into `out`. Returns its length, -1 otherwise or too long. */
int mj_text(const char *v, const char *end, char *out, size_t cap);
/** true / false; a number other than 0 is true. `ok` false (and false returned) for anything else. */
bool mj_bool(const char *v, const char *end, bool *ok);
/** A number (or a string holding one). */
bool mj_long(const char *v, const char *end, long long *out);

#ifdef __cplusplus
}
#endif
