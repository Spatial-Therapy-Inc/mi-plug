// Host tests for the template's app logic. Run: bash firmware/_template/test/run.sh
#include <stdio.h>
#include <string.h>

#include "../main/counter.h"

static int failures, checks;
#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                  \
    } while (0)

int main(void) {
    counter_t c = {0};
    char text[32];
    counter_tap(&c), counter_tap(&c), counter_tap(&c);
    counter_text(&c, false, text, sizeof(text));
    CHECK(!strcmp(text, "3 次"));
    CHECK(counter_reset(&c) && c.best == 3 && c.taps == 0);
    counter_tap(&c);
    counter_text(&c, true, text, sizeof(text));
    CHECK(!strcmp(text, "1 tap"));
    CHECK(!counter_reset(&c) && c.best == 3);
    printf("mi-plug host tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
