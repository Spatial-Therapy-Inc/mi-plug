// miIO over UDP on the home network (ESP-IDF, lwIP sockets): hello, one request, its answer. Blocking; call from
// home-switch's worker task, never from LVGL or the key task. Talks only to the device's own address on the LAN (and
// the LAN's broadcast address when looking for a device whose address changed); nothing goes to the internet.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "plug_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Reads (set = false) or switches (set = true, to `on`) the power of `p`. On ESP_OK, `*state` is the power now.
 *  `p->device_id` is filled from the hello. Errors: ESP_ERR_TIMEOUT (no answer), ESP_ERR_INVALID_CRC (the answer
 *  does not match the token: a wrong token), ESP_ERR_INVALID_RESPONSE (the device said no: a wrong siid/piid),
 *  ESP_ERR_INVALID_ARG (a bad token or address in `p`). */
esp_err_t miio_lan_power(plug_t *p, bool set, bool on, bool *state);

/** Says hello to the whole LAN and listens `wait_ms` for the device `device_id`; its address into `ip`. For a device
 *  that got a new address from the router. ESP_ERR_NOT_FOUND when it did not answer. */
esp_err_t miio_lan_find(uint32_t device_id, char ip[16], int wait_ms);

#ifdef __cplusplus
}
#endif
