#include "rc_bg.h"
#include "rc_system.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "bg";

// ── FOTA ─────────────────────────────────────────────────────────────────────

static void do_fota(void)
{
    ESP_LOGI(TAG, "Checking for firmware update...");
    esp_http_client_config_t http_cfg = {
        .url = FOTA_FW_URL,
        .skip_cert_common_name_check = true,
        .timeout_ms = 10000,
    };
    esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };
    esp_err_t ret = esp_https_ota(&ota_cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA success — restarting");
        rc_recovery_mark_stable();
        esp_restart();
    } else {
        ESP_LOGW(TAG, "OTA failed or up to date: %s", esp_err_to_name(ret));
    }
}

// ── Serial Console ────────────────────────────────────────────────────────────

static void console_task(void *arg)
{
    char buf[128];
    int  pos = 0;

    for (;;) {
        int c = getchar();
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (c == '\r' || c == '\n') {
            buf[pos] = '\0';
            pos = 0;

            if (strlen(buf) == 0) {
                continue;
            }

            if (strcmp(buf, "status") == 0) {
                EventBits_t bits = xEventGroupGetBits(rc_events);
                ESP_LOGI(TAG, "Battery: %d%%  Safe-mode: %s  WiFi: %s",
                         battery_percent,
                         (bits & SAFE_MODE_BIT)     ? "yes" : "no",
                         (bits & WIFI_CONNECTED_BIT) ? "yes" : "no");
            } else if (strcmp(buf, "reset") == 0) {
                ESP_LOGI(TAG, "Resetting...");
                rc_recovery_mark_stable();
                esp_restart();
            } else if (strcmp(buf, "ota") == 0) {
                do_fota();
            } else {
                ESP_LOGI(TAG, "Commands: status, reset, ota");
            }
        } else {
            if (pos < (int)(sizeof(buf) - 1)) {
                buf[pos++] = (char)c;
            }
        }
    }
}

// ── Shared command executor ───────────────────────────────────────────────────

void rc_execute_command(const char *cmd, char *out, size_t out_len)
{
    if (!cmd || !out || out_len == 0) return;
    out[0] = '\0';

    if (strcmp(cmd, "status") == 0) {
        EventBits_t bits = xEventGroupGetBits(rc_events);
        snprintf(out, out_len,
                 "Battery: %d%%  Safe-mode: %s  WiFi: %s\n",
                 battery_percent,
                 (bits & SAFE_MODE_BIT)     ? "yes" : "no",
                 (bits & WIFI_CONNECTED_BIT) ? "yes" : "no");
    } else if (strcmp(cmd, "reset") == 0) {
        snprintf(out, out_len, "Resetting...\n");
        rc_recovery_mark_stable();
        esp_restart();
    } else if (strcmp(cmd, "ota") == 0) {
        snprintf(out, out_len, "Starting OTA check...\n");
        do_fota();
    } else {
        snprintf(out, out_len, "Commands: status, reset, ota\n");
    }
}

// ── Background Task ───────────────────────────────────────────────────────────

void bg_task(void *arg)
{
    // Mark current app as valid so rollback does not trigger on next boot
    esp_ota_mark_app_valid_cancel_rollback();

    // Serial console in its own low-priority task
    xTaskCreate(console_task, "console", 4096, NULL, 1, NULL);

    uint32_t last_ota_check = 0;

    while (1) {
        // Block until WiFi is connected
        xEventGroupWaitBits(rc_events, WIFI_CONNECTED_BIT,
                            pdFALSE, pdTRUE, portMAX_DELAY);

        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Periodic FOTA check
        if (settings.ota_enabled &&
            (last_ota_check == 0 ||
             (now - last_ota_check) > settings.ota_interval_ms)) {
            do_fota();
            last_ota_check = now;
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
