// What both ways of managing home-switch's devices use: `yk cmd homeswitch.*` over USB (main.c) and the config page on
// the home network (webcfg.c). Each call takes the app's lock itself; call from a task whose stack is in internal
// RAM (the list is saved to NVS). Tokens never come back out.
#pragma once

#include <stdbool.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** {"devices":[{"name","ip","model","siid","piid","power":"on|off|unknown","note":"busy|unreachable|token|refused"?}],
 *  "current":n} into `reply`. */
void mp_describe(cJSON *reply);
/** {"name","ip","token","model"?,"siid"?,"piid"?}: adds, or replaces the device of the same name. On
 *  ESP_ERR_INVALID_ARG `*field` names the bad field; ESP_ERR_NO_MEM when the list is full. */
esp_err_t mp_add(const cJSON *args, const char **field, bool *replaced);
esp_err_t mp_remove(const char *name);
/** ESP_ERR_INVALID_ARG for an empty or too long name, ESP_ERR_INVALID_STATE when `new_name` is taken. */
esp_err_t mp_rename(const char *name, const char *new_name);
/** on: 1 on, 0 off, -1 the other way. Queued for the worker; ESP_ERR_NOT_FOUND for an unknown name (NULL: the
 *  device on screen). */
esp_err_t mp_switch(const char *name, int on);

#ifdef __cplusplus
}
#endif
