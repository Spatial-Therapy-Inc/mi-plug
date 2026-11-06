#include "counter.h"

#include <stdio.h>

void counter_tap(counter_t *c) { c->taps++; }

bool counter_reset(counter_t *c) {
    const bool best = c->taps > c->best;
    if (best) c->best = c->taps;
    c->taps = 0;
    return best;
}

void counter_text(const counter_t *c, bool english, char *out, int size) {
    if (english) snprintf(out, (size_t)size, c->taps == 1 ? "%d tap" : "%d taps", c->taps);
    else snprintf(out, (size_t)size, "%d 次", c->taps);
}
