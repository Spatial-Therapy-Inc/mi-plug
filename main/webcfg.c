// See webcfg.h.
#include "webcfg.h"

#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mi_plug.h"
#include "mp_crypto.h"

static const char *TAG = "webcfg";

#define BODY_MAX 1024

extern const char page_html_start[] asm("_binary_page_html_start");
extern const char page_html_end[] asm("_binary_page_html_end");

static httpd_handle_t s_server;
static char s_key[WEBCFG_KEY_LEN + 1];
static volatile int64_t s_last_us;

/** The key matches, in constant time. */
static bool key_ok(httpd_req_t *req) {
    char got[WEBCFG_KEY_LEN + 2] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-Key", got, sizeof(got)) != ESP_OK || strlen(got) != WEBCFG_KEY_LEN) return false;
    unsigned diff = 0;
    for (int i = 0; i < WEBCFG_KEY_LEN; i++) diff |= (unsigned)(got[i] ^ s_key[i]);
    return s_key[0] && diff == 0;
}

static esp_err_t send_json(httpd_req_t *req, const char *status, cJSON *doc) {
    char *text = cJSON_PrintUnformatted(doc);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const esp_err_t err = httpd_resp_sendstr(req, text ? text : "{}");
    cJSON_free(text);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *code, const char *field) {
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "error", code);
    if (field) cJSON_AddStringToObject(doc, "field", field);
    const esp_err_t err = send_json(req, status, doc);
    cJSON_Delete(doc);
    return err;
}

/** The request's JSON body (NULL when missing, too large or not JSON). */
static cJSON *read_body(httpd_req_t *req) {
    if (req->content_len == 0 || req->content_len > BODY_MAX) return NULL;
    char *buf = heap_caps_malloc(req->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) return NULL;
    size_t got = 0;
    while (got < req->content_len) {
        const int n = httpd_req_recv(req, buf + got, req->content_len - got);
        if (n <= 0) {
            heap_caps_free(buf);
            return NULL;
        }
        got += (size_t)n;
    }
    buf[got] = 0;
    cJSON *doc = cJSON_Parse(buf);
    memset(buf, 0, got);  // it may hold a token
    heap_caps_free(buf);
    return doc;
}

static esp_err_t page(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    // EMBED_TXTFILES ends the page with a NUL: not sent.
    return httpd_resp_send(req, page_html_start, page_html_end - page_html_start - 1);
}

/** Every /api/ call: the key first, then one of the operations (mi_plug.h). */
static esp_err_t api(httpd_req_t *req) {
    if (!key_ok(req)) return send_error(req, "403 Forbidden", "KEY", NULL);
    s_last_us = esp_timer_get_time();
    const char *op = req->uri + strlen("/api/");
    if (!strncmp(op, "devices", 7)) {
        cJSON *doc = cJSON_CreateObject();
        mp_describe(doc);
        const esp_err_t err = send_json(req, "200 OK", doc);
        cJSON_Delete(doc);
        return err;
    }
    if (req->method != HTTP_POST) return send_error(req, "405 Method Not Allowed", "METHOD", NULL);
    cJSON *body = read_body(req);
    if (!body) return send_error(req, "400 Bad Request", "BODY", NULL);
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(body, "name"));
    esp_err_t err;
    const char *field = NULL;
    bool replaced = false;
    if (!strcmp(op, "add")) {
        err = mp_add(body, &field, &replaced);
    } else if (!strcmp(op, "remove")) {
        err = name ? mp_remove(name) : ESP_ERR_INVALID_ARG;
    } else if (!strcmp(op, "rename")) {
        const char *to = cJSON_GetStringValue(cJSON_GetObjectItem(body, "to"));
        err = name && to ? mp_rename(name, to) : ESP_ERR_INVALID_ARG;
        field = "to";
    } else if (!strcmp(op, "switch")) {
        const cJSON *on = cJSON_GetObjectItem(body, "on");
        err = mp_switch(name, cJSON_IsBool(on) ? cJSON_IsTrue(on) : -1);
    } else {
        err = ESP_ERR_NOT_SUPPORTED;
    }
    // The body may hold a token: wipe it before freeing.
    cJSON *token = cJSON_GetObjectItem(body, "token");
    if (cJSON_IsString(token) && token->valuestring) memset(token->valuestring, 0, strlen(token->valuestring));
    cJSON_Delete(body);
    ESP_LOGI(TAG, "%s: %s", op, esp_err_to_name(err));
    switch (err) {
    case ESP_OK: {
        cJSON *doc = cJSON_CreateObject();
        if (replaced) cJSON_AddBoolToObject(doc, "replaced", true);
        mp_describe(doc);
        const esp_err_t sent = send_json(req, "200 OK", doc);
        cJSON_Delete(doc);
        return sent;
    }
    case ESP_ERR_INVALID_ARG: return send_error(req, "400 Bad Request", "BAD_FIELD", field ? field : "name");
    case ESP_ERR_INVALID_STATE: return send_error(req, "409 Conflict", "NAME_TAKEN", "to");
    case ESP_ERR_NO_MEM: return send_error(req, "409 Conflict", "FULL", NULL);
    case ESP_ERR_NOT_FOUND: return send_error(req, "404 Not Found", "NOT_FOUND", NULL);
    default: return send_error(req, "404 Not Found", "UNKNOWN", NULL);
    }
}

esp_err_t webcfg_start(char key[WEBCFG_KEY_LEN + 1]) {
    uint8_t raw[WEBCFG_KEY_LEN / 2];
    esp_fill_random(raw, sizeof(raw));
    mp_hex_encode(raw, sizeof(raw), s_key);
    memcpy(key, s_key, sizeof(s_key));
    s_last_us = esp_timer_get_time();
    if (s_server) return ESP_OK;  // running: only the key is new
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;  // internal RAM: the handlers save the list to NVS
    cfg.max_open_sockets = 3;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        s_server = NULL;
        memset(s_key, 0, sizeof(s_key));
        return err;
    }
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page},
        {.uri = "/api/*", .method = HTTP_GET, .handler = api},
        {.uri = "/api/*", .method = HTTP_POST, .handler = api},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) httpd_register_uri_handler(s_server, &routes[i]);
    ESP_LOGI(TAG, "config page on");
    return ESP_OK;
}

void webcfg_stop(void) {
    memset(s_key, 0, sizeof(s_key));
    if (!s_server) return;
    httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "config page off");
}

bool webcfg_running(void) { return s_server != NULL; }

int webcfg_idle_s(void) { return (int)((esp_timer_get_time() - s_last_us) / 1000000); }
