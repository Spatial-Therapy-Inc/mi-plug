// Extension firmware template (yaykit/docs/development/firmware-guide.md 6, yay-device.md 6.10). It shows the
// order every extension firmware follows: NVS (never erased) -> yaykit_app (crash loop guard) -> board -> side
// key -> Yaykit Link -> screen -> console -> yk_app_ready(). The app itself is a tap counter: tap adds one, hold resets, a double
// click opens the app ring (yaykit_app), and the first time in the app the hint arrow says so (yk_hint APPS). Copy
// this directory, rename the slug and the project, then replace the counter with your app.
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "cjk_font.h"
#include "counter.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "yaykit_app.h"
#include "yaykit_key.h"
#include "yaykit_link.h"
#include "yaykit_ui.h"
#include "yk_hint.h"

#define APP_SLUG "mi-plug"
static const char *TAG = "mi-plug";

static counter_t s_count;
static int s_rounds, s_best_round = -1;  // rounds ended with a hold, and which one was the best (0-based)
static lv_obj_t *s_value;
static volatile bool s_tapped, s_held;  // set in the key task, acted on in the LVGL task

/** Through yaykit_app (yk_app_on_key): returns whether the event was used. A hold an app does not use asks
 *  「回到 Yay 说？」; this one uses both. */
static bool on_key(const yk_key_event_t *ev, void *ctx) {
    (void)ctx;
    if (ev->type == YK_KEY_TAP) s_tapped = true;
    else if (ev->type == YK_KEY_HOLD) s_held = true;
    // YK_KEY_DOUBLE is yaykit_app's: the app ring.
    return ev->type == YK_KEY_TAP || ev->type == YK_KEY_HOLD;
}

static void show(void) {
    char text[32];
    counter_text(&s_count, false, text, sizeof(text));
    lv_label_set_text(s_value, text);
}

/** LVGL timer: runs with the board lock held, so it may touch the screen. */
static void tick(lv_timer_t *timer) {
    (void)timer;
    if (s_tapped) {
        s_tapped = false;
        counter_tap(&s_count);
        printf("MI_PLUG_TAP count=%d\n", s_count.taps);
        show();
    }
    if (s_held) {
        s_held = false;
        s_rounds++;
        const bool best = counter_reset(&s_count);
        if (best) s_best_round = s_rounds - 1;
        printf("MI_PLUG_RESET best=%d new=%d\n", s_count.best, best);
        show();
    }
}

/** Leaving for Yay (a double click, app.exit): a record for the platform and what Yay shows and says when it is
 *  back. Runs in a task of yaykit_app's, cut after YK_APP_LEAVE_MS (400 ms): keep it to writing. */
static void on_leave(void *ctx) {
    (void)ctx;
    const int taps = s_count.taps, rounds = s_rounds + (taps > 0);
    if (!rounds) return;  // nothing happened: Yay just comes back
    char data[64];
    snprintf(data, sizeof(data), "{\"taps\":%d,\"rounds\":%d}", taps, rounds);
    // The record waits in the outbox until Yay is online (yk_app_record): the app needs no network.
    const bool recorded = yk_app_record("miplug.count", data) == ESP_OK;
    // The summary Yay draws in its ring: up to 3 numbers, a bead per round (the best one in coral).
    yk_summary_t summary;
    yk_summary_init(&summary);
    char value[12];
    snprintf(value, sizeof(value), "%d", rounds);
    yk_summary_stat(&summary, value, "轮");
    snprintf(value, sizeof(value), "%d", s_count.best > taps ? s_count.best : taps);
    yk_summary_stat(&summary, value, "最多");
    yk_summary_beads(&summary, rounds, &s_best_round, s_best_round >= 0);
    summary.recorded = recorded;
    // One sentence Yay says (not after 30 minutes), and data for the platform.
    char sentence[48];
    snprintf(sentence, sizeof(sentence), "数了 %d 轮", rounds);
    cJSON *doc = cJSON_Parse(data);
    yk_app_set_result_summary(sentence, &summary, doc);
    cJSON_Delete(doc);
}

static esp_err_t on_command(const char *name, const cJSON *args, cJSON *reply, const char **code, void *ctx) {
    (void)ctx;
    yk_app_input();
    esp_err_t err = yk_app_command(name, args, reply, code);  // app.exit, app.recovery, app.status
    if (err != ESP_ERR_NOT_SUPPORTED) return err;
    if (!strcmp(name, "miplug.status")) {
        cJSON_AddNumberToObject(reply, "taps", s_count.taps);
        cJSON_AddNumberToObject(reply, "best", s_count.best);
        return ESP_OK;
    }
    *code = "UNKNOWN_COMMAND";
    return ESP_ERR_NOT_SUPPORTED;
}

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
    yk_ui_register_snapshot();
    const esp_console_cmd_t heap = {.command = "heap", .help = "available memory", .func = heap_cmd};
    ESP_ERROR_CHECK(esp_console_cmd_register(&heap));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

void app_main(void) {
    // 1. NVS: never erase it (it is shared with every app and holds the Wi-Fi credentials).
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) ESP_LOGE(TAG, "NVS unavailable (%s); running without saved settings", esp_err_to_name(err));
    // 2. yaykit_app first, so that a crash anywhere below counts towards the crash loop guard (it also subscribes
    //    for the double click home).
    ESP_ERROR_CHECK(yk_app_start(&(yk_app_config_t){.slug = APP_SLUG, .version = APP_VERSION}));
    yk_app_on_leave(on_leave, NULL);
    // 3. Board and screen (dark until the first frame: no flash of a blank screen after the switch), then the side key.
    if (!board_display_start_opts(&(board_display_opts_t){.start_dark = true})) {
        ESP_LOGE(TAG, "display failed");
        return;
    }
    // delay_tap: the first press of a double click (the app ring) is not a tap; a tap comes 0.45 s after its release.
    ESP_ERROR_CHECK(yk_key_init(&(yk_key_config_t){.delay_tap = true}));
    ESP_ERROR_CHECK(yk_app_on_key(on_key, NULL));
    // 4. Yaykit Link: `yk info` and `yk cmd` over USB (BLE stays off until asked for).
    const yk_link_config_t link = {.board = "tikpal-r1", .app_slug = APP_SLUG, .app_version = APP_VERSION,
                                   .layout = "yaykit-v1", .idle_seconds = 90, .on_command = on_command};
    ESP_ERROR_CHECK(yk_link_init(&link));
    // 5. Launch parameters from Yay (read once).
    yk_launch_t launch;
    if (yk_app_launch(&launch) == ESP_OK && launch.present) printf("MI_PLUG_LAUNCH intent=%s params=%s\n", launch.intent, launch.params);

    // 6. The screen; then confirm the image (the bootloader would roll back an app that never gets here).
    board_lock(-1);
    ESP_ERROR_CHECK(cjk_font_init());
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_scrollable(screen, false);
    // The round-screen spec's colours and sizes (yk_round.h): the same on every firmware.
    lv_obj_set_style_bg_color(screen, lv_color_hex(YK_PAPER), 0);
    yk_ui_label(screen, "计数", 0, 100, 466, YK_SIZE_STATUS, YK_SAGE_TEXT);
    s_value = yk_ui_label(screen, "", 0, 190, 466, 44, YK_INK);
    yk_ui_label(screen, "短按加一 · 按住清零", 0, 330, 466, YK_SIZE_MIN, YK_INK2);
    show();
    lv_screen_load(screen);
    lv_refr_now(NULL);
    // Yay's settings through yaykit_app (0.1.13): the brightness (capped while the battery is low), and as needed
    // yk_app_screen_off_s() for the screen-off, yk_app_volume() / yk_app_set_volume(), yk_app_english(). The battery
    // guard (sleep when it runs out) comes with yk_app_start() and yk_app_ready() by itself.
    board_brightness_set(yk_app_brightness());
    // The first time in this app: an arrow at the side key, 「双击：应用和回家」 (yk_hint, P24), until the app ring was
    // opened once. Any press takes it away (yaykit_app).
    if (yk_hint_should(YK_HINT_APPS)) yk_hint_show(screen, YK_HINT_APPS, NULL);
    lv_timer_create(tick, 50, NULL);
    board_unlock();
    // 7. The console, then yk_app_ready(): its banner tells tools the app is up, so the console must already
    //    listen (a command sent right after the banner was lost when the console came second).
    start_console();
    yk_app_ready();
}
