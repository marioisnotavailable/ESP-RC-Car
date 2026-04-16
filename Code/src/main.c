#include <stdio.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rc_common.h"
#include "rc_system.h"

static const char *TAG = "main";

// Forward declarations (implementiert in jeweiliger Komponente)
void motor_task(void *arg);
void comms_task(void *arg);
void system_task(void *arg);
void bg_task(void *arg);

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

    ESP_LOGI(TAG, "ESP-RC-Car (ESP-IDF) starting...");

    xTaskCreate(motor_task,  "motor",  4096,  NULL, 10, NULL);
    xTaskCreate(comms_task,  "comms",  8192,  NULL, 7,  NULL);
    xTaskCreate(system_task, "system", 6144,  NULL, 5,  NULL);
    xTaskCreate(bg_task,     "bg",     6144,  NULL, 2,  NULL);

    vTaskDelete(NULL);
}
