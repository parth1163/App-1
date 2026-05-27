/*
 * Application 1 — Setup, blink, web monitor
 *
 * What this scaffold gives you:
 * - A WORKING dual-core ESP32 application.
 * - Core 1 runs a blink_task that toggles an LED at 1 Hz with vTaskDelayUntil.
 * - Core 0 runs an HTTP server that reports the LED state to a browser.
 * - Wi-Fi is wired up for Wokwi's simulated "Wokwi-GUEST" access point.
 *
 * What you do:
 * 1. Rename the task / log / page strings to fit your CHOSEN THEME.
 * Search for YOURTHEME and replace every occurrence.
 * 2. Customize the HTML in handle_root() to match your theme.
 * 3. Optionally change LED_GPIO and BLINK_PERIOD_MS.
 * 4. Run it, take a screenshot of the web page, drop both in your README.
 *
 * You should NOT need to add new tasks or change the architecture for App 1.
 * That comes in App 2.
 *
 * ============================================================
 * Theme: Industrial (Ride-X Dispatch)
 * ============================================================
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "nvs_flash.h"

/* ---------- Configuration ---------- */
#define LED_GPIO          GPIO_NUM_2
#define BLINK_PERIOD_MS   1000          /* 1 Hz toggle */
#define HTTP_PORT         80

#define WIFI_SSID         "Wokwi-GUEST"
#define WIFI_PASS         ""             /* Wokwi virtual AP is open */

#define CONFIG_LOG_DEFAULT_LEVEL_INFO 1
#define CONFIG_LOG_MAXIMUM_LEVEL  5

static const char *TAG = "app1";

/* Shared state: single bool, atomic read on Xtensa.
 * (W6 will teach us why this is the only case where volatile-without-mutex is OK.) */
static volatile bool led_on = false;
static volatile uint32_t toggle_count = 0;

/* ---------- Blink task — runs on Core 1 (APP_CPU) ---------- */
static void blink_task(void *arg)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    /* vTaskDelayUntil is drift-free: we wake on a fixed schedule
     * even if our work took a variable amount of time. */
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(BLINK_PERIOD_MS);

    for (;;) {
        led_on = !led_on;
        toggle_count++;
        gpio_set_level(LED_GPIO, led_on);
        ESP_LOGI(TAG, "[Ride-X] dispatch_status = %s (cycle #%lu)",
                 led_on ? "READY" : "LOCKED", (unsigned long)toggle_count);

        vTaskDelayUntil(&last_wake, period);
    }
}

/* ---------- HTTP handler: live JSON state ---------- *
 * Returns: {"on":true|false,"toggles":N}
 * The page polls this 4x per second via fetch() &mdash; far faster and smoother
 * than a meta-refresh full-page reload, and a realistic pattern for embedded
 * dashboards. */
static esp_err_t handle_state(httpd_req_t *req)
{
    char buf[64];
    int n = snprintf(buf, sizeof(buf),
        "{\"on\":%s,\"toggles\":%lu}",
        led_on ? "true" : "false",
        (unsigned long)toggle_count);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

/* ---------- HTTP handler: root page (HTML shell only) ---------- *
 * The HTML is served once. JavaScript polls /state at 4 Hz and updates the
 * DOM in place &mdash; no full reload, no flicker, no Nyquist aliasing against
 * the 1 Hz blink. */
static esp_err_t handle_root(httpd_req_t *req)
{
    /* TODO: customize this HTML for your theme.
     * Examples:
     * - Avionics: "UAV-01 Beacon · status: ON"
     * - Medical:  "Heart-rate indicator · pulse: detected"
     * - Space:    "SAT-1 health beacon · t+00:42:17"
     * - Industrial: "Ride-X dispatch · READY"
     * - Security:   "Enclave-A integrity · OK"
     */
    static const char html[] =
        "<!DOCTYPE html>"
        "<html lang=\"en\"><head>"
        "<meta charset=\"utf-8\">"
        "<title>Ride-X Dispatch Readiness</title>"
        "<style>"
        "  body { font-family: -apple-system, sans-serif; background: #22252A; "
        "         color: #E2E8F0; padding: 2rem; }"
        "  h1 { color: #F59E0B; border-bottom: 3px solid #475569; "
        "       display: inline-block; padding-bottom: 4px; }"
        "  .state { font-size: 3em; font-weight: 700; margin: 1rem 0; "
        "           transition: color 120ms ease; }"
        "  .state.on  { color: #10B981; }"
        "  .state.off { color: #EF4444; }"
        "  .meta { color: #94A3B8; font-variant-numeric: tabular-nums; }"
        "  .dot { display:inline-block; width: 0.6em; height: 0.6em; "
        "         border-radius: 50%; margin-right: 0.4em; "
        "         vertical-align: middle; transition: background 120ms ease; }"
        "  .dot.on  { background: #10B981; box-shadow: 0 0 10px #10B981; }"
        "  .dot.off { background: #EF4444; box-shadow: 0 0 4px #EF4444; }"
        "</style></head>"
        "<body>"
        "<h1>Ride-X Dispatch Readiness</h1>"
        "<p>Beacon state:</p>"
        "<div id=\"state\" class=\"state off\">"
        "  <span id=\"dot\" class=\"dot off\"></span><span id=\"label\">--</span>"
        "</div>"
        "<p class=\"meta\">Toggles since boot: <span id=\"count\">0</span></p>"
        "<p class=\"meta\">Polling at 4 Hz via <code>/state</code> JSON endpoint.</p>"
        "<script>"
        "async function poll(){"
        "  try{"
        "    const r = await fetch('/state',{cache:'no-store'});"
        "    const s = await r.json();"
        "    const cls = s.on ? 'on' : 'off';"
        "    document.getElementById('state').className = 'state ' + cls;"
        "    document.getElementById('dot').className = 'dot ' + cls;"
        "    document.getElementById('label').textContent = s.on ? 'READY' : 'LOCKED';"
        "    document.getElementById('count').textContent = s.toggles;"
        "  }catch(e){/* ignore transient network blips */}"
        "}"
        "setInterval(poll, 250);"
        "poll();"
        "</script>"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = HTTP_PORT;
    cfg.core_id = 0;                    /* networking on Core 0 */
    cfg.task_priority = 5;
    cfg.stack_size = 8192;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) == ESP_OK) {
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = handle_root,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t state = {
            .uri = "/state",
            .method = HTTP_GET,
            .handler = handle_state,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &state);

        ESP_LOGI(TAG, "HTTP server started on port %d", HTTP_PORT);
    } else {
        ESP_LOGE(TAG, "HTTP server failed to start");
    }
    return server;
}

/* ---------- Wi-Fi event handler ---------- */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        start_webserver();
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* ---------- app_main — kicks everything off ---------- */
void app_main(void)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_LOGI(TAG, "==== App 1 [Ride-X Dispatch] starting ====");

    /* Start Wi-Fi + HTTP on Core 0 (PRO_CPU is networking by default in IDF) */
    wifi_init_sta();

    /* Pin the real-time blink task to Core 1 (APP_CPU). */
    xTaskCreatePinnedToCore(
        blink_task,        /* function */
        "dispatch",           /* name (max 16 chars) */
        2048,              /* stack — 2048 words = 8 KB */
        NULL,              /* parameters */
        5,                 /* priority */
        NULL,              /* task handle (we don't need it) */
        APP_CPU_NUM        /* Core 1 */
    );

    /* app_main returns; both cores keep running the tasks we created. */
}