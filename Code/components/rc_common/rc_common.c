#include "rc_common.h"
#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG = "common";

QueueHandle_t       cmd_queue;
QueueHandle_t       batt_queue;
EventGroupHandle_t  rc_events;

void rc_common_init(void) {
    cmd_queue  = xQueueCreate(1, sizeof(Cmd));
    batt_queue = xQueueCreate(1, sizeof(int));
    rc_events  = xEventGroupCreate();

    configASSERT(cmd_queue);
    configASSERT(batt_queue);
    configASSERT(rc_events);

    esp_vfs_littlefs_conf_t lfs_conf = {
        .base_path              = "/littlefs",
        .partition_label        = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount             = false,
    };
    esp_err_t ret = esp_vfs_littlefs_register(&lfs_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(ret));
    } else {
        size_t total = 0, used = 0;
        esp_err_t info_ret = esp_littlefs_info("littlefs", &total, &used);
        if (info_ret != ESP_OK) {
            ESP_LOGW(TAG, "LittleFS info query failed: %s", esp_err_to_name(info_ret));
        } else {
            ESP_LOGI(TAG, "LittleFS mounted: %u KB total, %u KB used",
                     (unsigned)(total / 1024), (unsigned)(used / 1024));
        }
    }
}
