#include <stdio.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rc_common.h"
#include "rc_system.h"

// Uncomment to run drive test (forward/stop/backward/stop loop) instead of normal app
// #define TEST_DRIVE

static const char *TAG = "main";

// Forward declarations (implementiert in jeweiliger Komponente)
void motor_task(void *arg);
void comms_task(void *arg);
void system_task(void *arg);
void bg_task(void *arg);

#ifdef TEST_DRIVE
static void test_drive_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(3000)); // wait for motor init

    Cmd cmd = {.throttle = 0, .steer = 0, .flags = 0};

    while (1) {
        ESP_LOGI(TAG, "TEST: forward");
        cmd.throttle = 500;
        for (int i = 0; i < 20; i++) {
            xQueueOverwrite(cmd_queue, &cmd);
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        ESP_LOGI(TAG, "TEST: stop");
        cmd.throttle = 0;
        xQueueOverwrite(cmd_queue, &cmd);
        vTaskDelay(pdMS_TO_TICKS(500));

        ESP_LOGI(TAG, "TEST: backward");
        cmd.throttle = -500;
        for (int i = 0; i < 20; i++) {
            xQueueOverwrite(cmd_queue, &cmd);
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        ESP_LOGI(TAG, "TEST: stop");
        cmd.throttle = 0;
        xQueueOverwrite(cmd_queue, &cmd);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
#endif

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    rc_common_init();
    rc_recovery_check();
    rc_settings_load();

#ifdef TEST_DRIVE
    ESP_LOGI(TAG, "ESP-RC-Car TEST_DRIVE mode");
    xTaskCreate(motor_task,     "motor",  4096, NULL, 10, NULL);
    xTaskCreate(system_task,    "system", 6144, NULL, 5,  NULL);
    xTaskCreate(test_drive_task,"test",   4096, NULL, 3,  NULL);
#else
    ESP_LOGI(TAG, "ESP-RC-Car (ESP-IDF) starting...");
    xTaskCreate(motor_task,  "motor",  4096,  NULL, 10, NULL);
    xTaskCreate(comms_task,  "comms",  8192,  NULL, 7,  NULL);
    xTaskCreate(system_task, "system", 6144,  NULL, 5,  NULL);
    xTaskCreate(bg_task,     "bg",     6144,  NULL, 2,  NULL);
#endif

    vTaskDelete(NULL);
}
