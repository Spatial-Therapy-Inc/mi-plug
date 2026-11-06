// The template's app logic, kept in plain C so it runs in host tests (test/run.sh): a tap counter that a
// hold resets. Replace it with your app's logic; keep ESP-IDF calls out of these files.
#pragma once

#include <stdbool.h>

typedef struct {
    int taps;
    int best;
} counter_t;

void counter_tap(counter_t *c);
/** Resets the count; returns true when it was a new best. */
bool counter_reset(counter_t *c);
/** One short line for the screen: "3 次" / "3 taps". */
void counter_text(const counter_t *c, bool english, char *out, int size);
