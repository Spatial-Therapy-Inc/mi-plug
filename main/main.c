// mi-plug（米家开关）: switches the user's own Xiaomi Wi-Fi devices (plugs, lights, fans…) on and off from the round
// screen, straight over the home network with the miIO protocol. A self-use prototype: nothing goes to the
// internet, and the devices (IP, token, model) are added over USB with `yk cmd miplug.add` (README).
//
// One device a page: tap the big circle or the side key to switch it, swipe left or right for the next one. Yay can
// open the app with {"action":"on|off|toggle","device":"台灯"}.
//
// Start-up follows the template: NVS -> yk_app_start -> board -> side key -> Yaykit Link -> screen -> console ->
// yk_app_ready -> Wi-Fi.
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "cJSON.h"
#include "cjk_font.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "miio_lan.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "plug_logic.h"
#include "yaykit_app.h"
#include "yaykit_key.h"
#include "yaykit_link.h"
#include "yaykit_ui.h"
#include "yaykit_wifi.h"
#include "yk_hint.h"

#define APP_SLUG "mi-plug"
static const char *TAG = "mi-plug";

#define NVS_NS "yk_miplug"
#define CIRCLE_D 220
#define CIRCLE_CY 225
#define SWIPE_PX 60

// ---------------------------------------------------------------- state (s_lock guards it; the UI reads a copy)

typedef enum { POWER_UNKNOWN, POWER_ON, POWER_OFF } power_t;
typedef enum { NOTE_NONE, NOTE_BUSY, NOTE_UNREACHABLE, NOTE_TOKEN, NOTE_REFUSED } note_t;

static SemaphoreHandle_t s_lock;
static plug_list_t s_list;
static power_t s_power[PLUG_MAX];
static note_t s_note[PLUG_MAX];
static int s_current;
static volatile bool s_dirty = true, s_rebuild = true;  // redraw the page / the whole screen
static bool s_en;                                          // English text (the account's language)
static char s_result[80];                                  // what Yay says when it is back ("" when nothing happened)

typedef enum { JOB_READ, JOB_SET, JOB_TOGGLE } job_type_t;
typedef struct {
    job_type_t type;
    int index;
    bool on;
} job_t;
static QueueHandle_t s_jobs;
static volatile bool s_key_tap;

static const char *T(const char *zh, const char *en) { return s_en ? en : zh; }

// ---------------------------------------------------------------- NVS (written only when the list changes)

static void store_load(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(s_list);
    if (nvs_get_blob(h, "devices", &s_list, &len) != ESP_OK || len != sizeof(s_list) || s_list.count > PLUG_MAX)
        memset(&s_list, 0, sizeof(s_list));
    uint8_t current = 0;
    if (nvs_get_u8(h, "current", &current) == ESP_OK && current < s_list.count) s_current = current;
    nvs_close(h);
}

/** Called with s_lock held, from a task whose stack is in internal RAM (flash writes). */
static void store_save(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "devices", &s_list, sizeof(s_list));
        if (err == ESP_OK) err = nvs_set_u8(h, "current", (uint8_t)s_current);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGW(TAG, "saving the devices failed: %s", esp_err_to_name(err));
}

// ---------------------------------------------------------------- the worker: all network work happens here

static void post(job_type_t type, int index, bool on) {
    const job_t job = {.type = type, .index = index, .on = on};
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) ESP_LOGW(TAG, "busy: job dropped");
}

static void run_job(const job_t *job) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (job->index < 0 || job->index >= s_list.count) {
        xSemaphoreGive(s_lock);
        return;
    }
    plug_t p = s_list.items[job->index];  // a copy: the network call runs without the lock
    const power_t before = s_power[job->index];
    s_note[job->index] = NOTE_BUSY;
    s_dirty = true;
    xSemaphoreGive(s_lock);

    if (!yk_wifi_wait_connected(15000)) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_note[job->index] = NOTE_UNREACHABLE;
        s_dirty = true;
        xSemaphoreGive(s_lock);
        return;
    }
    const bool set = job->type != JOB_READ;
    const bool want = job->type == JOB_TOGGLE ? before != POWER_ON : job->on;
    bool now = false;
    const uint32_t old_id = p.device_id;
    char old_ip[16];
    strcpy(old_ip, p.ip);
    esp_err_t err = miio_lan_power(&p, set, want, &now);
    if (err == ESP_ERR_TIMEOUT && p.device_id) {
        // No answer: the router may have given it a new address. Look for it by its device id.
        char ip[16];
        if (miio_lan_find(p.device_id, ip, 2500) == ESP_OK && strcmp(ip, p.ip)) {
            ESP_LOGI(TAG, "device %d moved to a new address", job->index);
            strcpy(p.ip, ip);
            err = miio_lan_power(&p, set, want, &now);
        }
    }
    ESP_LOGI(TAG, "%s device %d: %s", set ? "set" : "read", job->index, esp_err_to_name(err));

    xSemaphoreTake(s_lock, portMAX_DELAY);
    // The list may have changed meanwhile (yk cmd): only write back to the same device.
    if (job->index < s_list.count && !strcmp(s_list.items[job->index].name, p.name)) {
        plug_t *kept = &s_list.items[job->index];
        if (err == ESP_OK) {
            s_power[job->index] = now ? POWER_ON : POWER_OFF;
            s_note[job->index] = NOTE_NONE;
            if (set) snprintf(s_result, sizeof(s_result), T("%s已%s", "%s is %s"), p.name, now ? T("打开", "on") : T("关闭", "off"));
        } else {
            s_note[job->index] = err == ESP_ERR_INVALID_CRC        ? NOTE_TOKEN
                                 : err == ESP_ERR_INVALID_RESPONSE ? NOTE_REFUSED
                                                                   : NOTE_UNREACHABLE;
        }
        if (p.device_id != old_id || strcmp(p.ip, old_ip)) {  // learnt the id, or found it at a new address
            kept->device_id = p.device_id;
            strcpy(kept->ip, p.ip);
            store_save();
        }
        s_dirty = true;
    }
    xSemaphoreGive(s_lock);
}

static void worker(void *arg) {
    (void)arg;
    job_t job;
    for (;;) {
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) == pdTRUE) run_job(&job);
    }
}

// ---------------------------------------------------------------- the screen (LVGL task / board lock only)

static lv_obj_t *s_screen, *s_name, *s_circle, *s_big, *s_status, *s_dots[PLUG_MAX], *s_empty[2];
static lv_point_t s_press;
static bool s_pressing_circle;

static bool in_circle(const lv_point_t *p) {
    const int dx = p->x - YK_CX, dy = p->y - CIRCLE_CY;
    return dx * dx + dy * dy <= (CIRCLE_D / 2) * (CIRCLE_D / 2);
}

static void select_page(int index) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int count = s_list.count;
    if (count > 0) s_current = (index % count + count) % count;
    const bool unknown = count > 0 && s_power[s_current] == POWER_UNKNOWN;
    s_dirty = true;
    xSemaphoreGive(s_lock);
    if (unknown) post(JOB_READ, s_current, false);
}

static void on_touch(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (code == LV_EVENT_PRESSED) {
        s_press = p;
        // Pulled down from the top edge: the system's, never ours (review rule F-05).
        s_pressing_circle = p.y > 40 && in_circle(&p) && s_list.count > 0;
        if (s_pressing_circle) lv_obj_set_style_bg_opa(s_circle, LV_OPA_70, 0);  // shows within 100 ms
        yk_app_input();
        return;
    }
    if (code != LV_EVENT_RELEASED) return;
    if (s_pressing_circle) lv_obj_set_style_bg_opa(s_circle, LV_OPA_COVER, 0);
    if (s_press.y <= 40) return;
    const int dx = p.x - s_press.x, dy = p.y - s_press.y;
    if (dx > SWIPE_PX || dx < -SWIPE_PX) {
        if ((dx > 0 ? dx : -dx) > (dy > 0 ? dy : -dy)) select_page(s_current + (dx < 0 ? 1 : -1));
        return;
    }
    if (s_pressing_circle && in_circle(&p)) post(JOB_TOGGLE, s_current, false);
}

static void build(void) {
    lv_obj_clean(s_screen);
    memset(s_dots, 0, sizeof(s_dots));
    s_name = yk_ui_label(s_screen, "", 33, 52, 400, YK_SIZE_HEADING, YK_INK);
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_DOT);

    s_circle = lv_obj_create(s_screen);
    lv_obj_set_size(s_circle, CIRCLE_D, CIRCLE_D);
    lv_obj_set_pos(s_circle, YK_CX - CIRCLE_D / 2, CIRCLE_CY - CIRCLE_D / 2);
    lv_obj_set_style_radius(s_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_circle, 0, 0);
    lv_obj_set_style_shadow_width(s_circle, 0, 0);
    lv_obj_set_style_pad_all(s_circle, 0, 0);
    lv_obj_set_clickable(s_circle, false);  // touches go to the screen (on_touch)
    lv_obj_set_scrollable(s_circle, false);
    s_big = yk_ui_label(s_circle, "", 0, (CIRCLE_D - YK_SIZE_HERO_MIN) / 2 - 8, CIRCLE_D, YK_SIZE_HERO_MIN, YK_INK);

    s_status = yk_ui_label(s_screen, "", 43, 350, 380, YK_SIZE_STATUS, YK_INK2);
    for (int i = 0; i < s_list.count && s_list.count > 1; i++) {
        lv_obj_t *d = lv_obj_create(s_screen);
        lv_obj_set_size(d, 10, 10);
        lv_obj_set_pos(d, YK_CX - (s_list.count * 18 - 8) / 2 + i * 18, 392);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_clickable(d, false);
        lv_obj_set_scrollable(d, false);
        s_dots[i] = d;
    }
    s_empty[0] = yk_ui_label(s_screen, T("还没有设备", "No devices yet"), 33, 190, 400, YK_SIZE_HEADING, YK_INK);
    s_empty[1] = yk_ui_label(s_screen, T("在电脑上用 USB 添加", "Add one over USB"), 33, 240, 400, YK_SIZE_MIN, YK_INK2);
}

static void show(void) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int count = s_list.count, current = s_current;
    char name[PLUG_NAME_MAX];
    snprintf(name, sizeof(name), "%s", count ? s_list.items[current].name : "");
    const power_t power = count ? s_power[current] : POWER_UNKNOWN;
    const note_t note = count ? s_note[current] : NOTE_NONE;
    xSemaphoreGive(s_lock);

    const bool empty = count == 0;
    lv_obj_t *const page[] = {s_name, s_circle, s_status};
    for (size_t i = 0; i < sizeof(page) / sizeof(page[0]); i++) lv_obj_set_hidden(page[i], empty);
    for (int i = 0; i < 2; i++) lv_obj_set_hidden(s_empty[i], !empty);
    if (empty) return;

    lv_label_set_text(s_name, name);
    const bool on = power == POWER_ON;
    lv_obj_set_style_bg_color(s_circle, lv_color_hex(on ? YK_SAGE : YK_PANEL), 0);
    lv_label_set_text(s_big, power == POWER_UNKNOWN ? "·" : on ? T("开", "ON") : T("关", "OFF"));
    lv_obj_set_style_text_color(s_big, lv_color_hex(on ? YK_PAPER : YK_INK2), 0);
    // A thin ring while it is waiting for the device (the spec's connecting colour).
    lv_obj_set_style_border_width(s_circle, note == NOTE_BUSY ? 6 : 0, 0);
    lv_obj_set_style_border_color(s_circle, lv_color_hex(YK_INK3), 0);

    const yk_wifi_state_t wifi = yk_wifi_state();
    const char *status;
    uint32_t color = YK_INK2;
    if (wifi == YK_WIFI_UNCONFIGURED) status = T("回 Tikpal 设置网络", "Set up Wi-Fi in Tikpal");
    else if (wifi != YK_WIFI_CONNECTED) status = T("正在连网络", "Connecting…");
    else if (note == NOTE_BUSY) status = power == POWER_UNKNOWN ? T("正在读取", "Checking…") : T("正在切换", "Switching…");
    else if (note == NOTE_UNREACHABLE) status = T("连不上这台设备", "Can't reach it"), color = YK_CORAL_TEXT;
    else if (note == NOTE_TOKEN) status = T("token 不对，重新添加", "Wrong token: add it again"), color = YK_CORAL_TEXT;
    else if (note == NOTE_REFUSED) status = T("设备不认这个开关", "It refused: check siid/piid"), color = YK_CORAL_TEXT;
    else status = on ? T("已打开 · 点一下关", "On · tap to turn off")
                : power == POWER_OFF ? T("已关闭 · 点一下开", "Off · tap to turn on")
                                     : T("点一下读取", "Tap to check");
    lv_label_set_text(s_status, status);
    lv_obj_set_style_text_color(s_status, lv_color_hex(color), 0);
    for (int i = 0; i < count && s_dots[i]; i++)
        lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(i == current ? YK_SAGE : YK_PANEL), 0);
}

/** LVGL timer: runs with the board lock held. */
static void tick(lv_timer_t *timer) {
    (void)timer;
    static yk_wifi_state_t last_wifi = YK_WIFI_OFF;
    const yk_wifi_state_t wifi = yk_wifi_state();
    if (wifi != last_wifi) {
        // Connected now: read the page we are on.
        if (wifi == YK_WIFI_CONNECTED && s_list.count && s_power[s_current] == POWER_UNKNOWN) post(JOB_READ, s_current, false);
        last_wifi = wifi;
        s_dirty = true;
    }
    if (s_key_tap) {
        s_key_tap = false;
        if (s_list.count) post(JOB_TOGGLE, s_current, false);
    }
    if (s_rebuild) {
        s_rebuild = false;
        build();
        s_dirty = true;
    }
    if (s_dirty) {
        s_dirty = false;
        show();
    }
}

// ---------------------------------------------------------------- side key, leaving, launch

static bool on_key(const yk_key_event_t *ev, void *ctx) {
    (void)ctx;
    if (ev->type == YK_KEY_TAP) {
        s_key_tap = true;  // acted on in the LVGL timer
        return true;
    }
    return false;  // a hold: the system asks 「回到 Yay 说？」; the double click is the app ring
}

static void on_leave(void *ctx) {
    (void)ctx;
    if (s_result[0]) yk_app_set_result(s_result, NULL);  // what was switched, for Yay to say
}

/** {"action":"on|off|toggle","device":"台灯"} from Yay. */
static void apply_launch(const yk_launch_t *launch) {
    cJSON *params = cJSON_Parse(launch->params);
    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(params, "action"));
    const char *device = cJSON_GetStringValue(cJSON_GetObjectItem(params, "device"));
    int index = s_current;
    if (device) {
        const int found = plug_list_find(&s_list, device);
        if (found >= 0) index = found;
    }
    if (s_list.count) {
        s_current = index;
        if (action && !strcmp(action, "on")) post(JOB_SET, index, true);
        else if (action && !strcmp(action, "off")) post(JOB_SET, index, false);
        else if (action && !strcmp(action, "toggle")) post(JOB_TOGGLE, index, false);
    }
    printf("MI_PLUG_LAUNCH action=%s found=%d\n", action ? action : "-", device ? plug_list_find(&s_list, device) >= 0 : -1);
    cJSON_Delete(params);
}

// ---------------------------------------------------------------- yk cmd miplug.*: adding devices over USB

static void describe(cJSON *reply) {
    cJSON *arr = cJSON_AddArrayToObject(reply, "devices");
    for (int i = 0; i < s_list.count; i++) {
        const plug_t *p = &s_list.items[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", p->name);
        cJSON_AddStringToObject(o, "ip", p->ip);
        cJSON_AddStringToObject(o, "model", p->model);
        cJSON_AddNumberToObject(o, "siid", p->siid);
        cJSON_AddNumberToObject(o, "piid", p->piid);
        cJSON_AddStringToObject(o, "power", s_power[i] == POWER_ON ? "on" : s_power[i] == POWER_OFF ? "off" : "unknown");
        cJSON_AddItemToArray(arr, o);  // the token never goes out
    }
}

static int number_or(const cJSON *args, const char *key, int fallback) {
    const cJSON *v = cJSON_GetObjectItem(args, key);
    return cJSON_IsNumber(v) ? v->valueint : fallback;
}

static esp_err_t plug_command(const char *name, const cJSON *args, cJSON *reply, const char **code) {
    if (!strcmp(name, "miplug.add")) {
        plug_t p = {0};
        const char *n = cJSON_GetStringValue(cJSON_GetObjectItem(args, "name"));
        const char *ip = cJSON_GetStringValue(cJSON_GetObjectItem(args, "ip"));
        const char *token = cJSON_GetStringValue(cJSON_GetObjectItem(args, "token"));
        const char *model = cJSON_GetStringValue(cJSON_GetObjectItem(args, "model"));
        snprintf(p.name, sizeof(p.name), "%s", n ? n : "");
        snprintf(p.ip, sizeof(p.ip), "%s", ip ? ip : "");
        snprintf(p.token, sizeof(p.token), "%s", token ? token : "");
        snprintf(p.model, sizeof(p.model), "%s", model ? model : "");
        const int siid = number_or(args, "siid", 2), piid = number_or(args, "piid", 1);
        p.siid = (uint8_t)(siid > 0 && siid < 256 ? siid : 0);
        p.piid = (uint8_t)(piid > 0 && piid < 256 ? piid : 0);
        const char *bad = plug_check(&p);
        if (bad) {
            cJSON_AddStringToObject(reply, "field", bad);
            *code = "BAD_DEVICE";
            return ESP_ERR_INVALID_ARG;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const int before = plug_list_find(&s_list, p.name);
        const bool replaced = before >= 0 && !strcmp(s_list.items[before].name, p.name);
        const bool ok = plug_list_put(&s_list, &p);
        const int at = plug_list_find(&s_list, p.name);
        if (ok && at >= 0) {
            s_power[at] = POWER_UNKNOWN, s_note[at] = NOTE_NONE;
            s_current = at;
            store_save();
        }
        describe(reply);
        xSemaphoreGive(s_lock);
        memset(&p, 0, sizeof(p));
        if (!ok) {
            *code = "FULL";
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddBoolToObject(reply, "replaced", replaced);
        s_rebuild = true;
        post(JOB_READ, at, false);
        return ESP_OK;
    }
    if (!strcmp(name, "miplug.remove")) {
        const char *n = cJSON_GetStringValue(cJSON_GetObjectItem(args, "name"));
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const int at = n ? plug_list_find(&s_list, n) : -1;
        const bool ok = at >= 0 && !strcmp(s_list.items[at].name, n) && plug_list_remove(&s_list, n);
        if (ok) {
            for (int i = at; i < PLUG_MAX - 1; i++) s_power[i] = s_power[i + 1], s_note[i] = s_note[i + 1];
            s_power[PLUG_MAX - 1] = POWER_UNKNOWN, s_note[PLUG_MAX - 1] = NOTE_NONE;
            if (s_current >= s_list.count) s_current = s_list.count ? s_list.count - 1 : 0;
            store_save();
        }
        describe(reply);
        xSemaphoreGive(s_lock);
        if (!ok) {
            *code = "NOT_FOUND";
            return ESP_ERR_NOT_FOUND;
        }
        s_rebuild = true;
        return ESP_OK;
    }
    if (!strcmp(name, "miplug.list")) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        describe(reply);
        cJSON_AddNumberToObject(reply, "current", s_current);
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }
    if (!strcmp(name, "miplug.set")) {  // {"name"?, "on": true|false} or no "on" to toggle
        const char *n = cJSON_GetStringValue(cJSON_GetObjectItem(args, "name"));
        const cJSON *on = cJSON_GetObjectItem(args, "on");
        const int at = n ? plug_list_find(&s_list, n) : s_current;
        if (at < 0 || at >= s_list.count) {
            *code = "NOT_FOUND";
            return ESP_ERR_NOT_FOUND;
        }
        if (cJSON_IsBool(on)) post(JOB_SET, at, cJSON_IsTrue(on));
        else post(JOB_TOGGLE, at, false);
        cJSON_AddStringToObject(reply, "queued", s_list.items[at].name);
        return ESP_OK;
    }
    *code = "UNKNOWN_COMMAND";
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t on_command(const char *name, const cJSON *args, cJSON *reply, const char **code, void *ctx) {
    (void)ctx;
    yk_app_input();
    esp_err_t err = yk_app_command(name, args, reply, code);  // app.exit, app.recovery, app.status
    if (err != ESP_ERR_NOT_SUPPORTED) return err;
    err = yk_wifi_command(name, args, reply, code);  // wifi.scan, wifi.join (the shared network)
    if (err != ESP_ERR_NOT_FOUND) return err;
    return plug_command(name, args, reply, code);
}

// ---------------------------------------------------------------- console, start-up

static int heap_cmd(int argc, char **argv) {
    (void)argc, (void)argv;
    printf("HEAP internal=%u internal_min=%u psram=%u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return 0;
}

static void start_console(void) {
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    cfg.prompt = APP_SLUG ">";
    cfg.task_stack_size = 8192;
    esp_console_dev_usb_serial_jtag_config_t usb = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usb, &cfg, &repl));
    esp_console_register_help_command();
    ESP_ERROR_CHECK(yk_link_register_console());
    ESP_ERROR_CHECK(yk_app_register_console());
    ESP_ERROR_CHECK(yk_wifi_register_console());
    yk_ui_register_snapshot();
    const esp_console_cmd_t heap = {.command = "heap", .help = "available memory", .func = heap_cmd};
    ESP_ERROR_CHECK(esp_console_cmd_register(&heap));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

void app_main(void) {
    // 1. NVS: never erased (shared with every app; it holds the Wi-Fi credentials).
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) ESP_LOGE(TAG, "NVS unavailable (%s); running without saved devices", esp_err_to_name(err));
    // 2. yaykit_app first (crash loop guard, the double click home, the battery guard).
    ESP_ERROR_CHECK(yk_app_start(&(yk_app_config_t){.slug = APP_SLUG, .version = APP_VERSION}));
    yk_app_on_leave(on_leave, NULL);
    s_en = yk_app_english();
    s_lock = xSemaphoreCreateMutex();
    s_jobs = xQueueCreate(4, sizeof(job_t));
    store_load();
    // 3. Board and screen (dark until the first frame), then the side key.
    if (!board_display_start_opts(&(board_display_opts_t){.start_dark = true})) {
        ESP_LOGE(TAG, "display failed");
        return;
    }
    board_rtc_init();
    ESP_ERROR_CHECK(yk_key_init(&(yk_key_config_t){.delay_tap = true}));
    ESP_ERROR_CHECK(yk_app_on_key(on_key, NULL));
    // 4. Yaykit Link: `yk info` and `yk cmd` over USB (BLE stays off).
    const yk_link_config_t link = {.board = "tikpal-r1", .app_slug = APP_SLUG, .app_version = APP_VERSION,
                                   .layout = "yaykit-v1", .idle_seconds = 90, .on_command = on_command};
    ESP_ERROR_CHECK(yk_link_init(&link));
    // 5. Launch parameters from Yay (read once; acted on by the worker once Wi-Fi is up).
    yk_launch_t launch;
    const bool launched = yk_app_launch(&launch) == ESP_OK && launch.present;

    // 6. The screen; then confirm the image.
    board_lock(-1);
    ESP_ERROR_CHECK(cjk_font_init());
    s_screen = lv_obj_create(NULL);
    lv_obj_set_scrollable(s_screen, false);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(YK_PAPER), 0);
    lv_obj_add_event_cb(s_screen, on_touch, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_screen, on_touch, LV_EVENT_RELEASED, NULL);
    s_rebuild = false;
    build();
    show();
    lv_screen_load(s_screen);
    lv_refr_now(NULL);
    board_brightness_set(yk_app_brightness());
    if (yk_hint_should(YK_HINT_APPS)) yk_hint_show(s_screen, YK_HINT_APPS, NULL);
    lv_timer_create(tick, 50, NULL);
    board_unlock();
    // 7. The console, then ready; the worker (its stack in internal RAM: it writes NVS) and Wi-Fi last.
    start_console();
    yk_app_ready();
    xTaskCreate(worker, "miplug", 6144, NULL, 4, NULL);
    ESP_ERROR_CHECK(yk_wifi_start());
    if (launched) apply_launch(&launch);
}
