// mi-plug's config page on the home network (esp_http_server, port 80): the phone scans the QR code on the round
// screen, http://<the round screen's address>/#k=<key>, and adds, imports, renames, removes and tries devices there.
//
// The page itself is public; every /api/ call carries the key (X-Key) that the round screen made when it showed
// the code. A new key each time; the server stops (and the key with it) when the round screen closes the page or
// after WEBCFG_IDLE_S without a call. Talks only to the home network: nothing goes to the internet.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEBCFG_KEY_LEN 16
#define WEBCFG_IDLE_S 600

/** Starts the server with a new key (WEBCFG_KEY_LEN hex characters and a NUL into `key`). */
esp_err_t webcfg_start(char key[WEBCFG_KEY_LEN + 1]);
/** Stops it; the key stops working. */
void webcfg_stop(void);
bool webcfg_running(void);
/** Seconds since the last /api/ call (or the start). */
int webcfg_idle_s(void);

#ifdef __cplusplus
}
#endif
