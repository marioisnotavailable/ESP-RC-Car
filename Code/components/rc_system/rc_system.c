// ── rc_system.c ──────────────────────────────────────────────────────────────
// NVS Settings, Crash Recovery, Battery ADC, Servo Steering, System Task
// ─────────────────────────────────────────────────────────────────────────────

#include "rc_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "system";

// ── Globals ──────────────────────────────────────────────────────────────────

DeviceSettings settings = {
    .ota_enabled        = true,
    .ota_interval_ms    = 300000,
    .wifi_tx_power      = 3,
    .failsafe_ms        = 500,
    .beacon_interval_ms = 1000,
    .ap_prefix          = "ESP-RC-",
    .always_start_panel = false,
    .steer_invert       = false,
    .steer_gain         = 1.0f,
    .steer_deadzone     = 50,
    .steer_filter       = 0.7f,
    .batt_warn_v        = 7.6f,
    .batt_off_v         = 7.5f,
    .max_throttle_pct   = 100,
    .adc_corr_factor    = 1.0f,
};

volatile int   battery_percent = 0;
volatile float g_vbatt         = 0.0f;
static adc_oneshot_unit_handle_t adc_handle;

// ── NVS Settings ─────────────────────────────────────────────────────────────

void rc_settings_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("settings", NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "settings NVS open failed (first boot?): %s", esp_err_to_name(err));
        return;
    }

    // bool fields as uint8
    uint8_t u8;
    if (nvs_get_u8(h, "ota_enabled",        &u8) == ESP_OK) settings.ota_enabled        = (bool)u8;
    if (nvs_get_u8(h, "wifi_tx_power",      &u8) == ESP_OK) settings.wifi_tx_power      = u8;
    if (nvs_get_u8(h, "steer_invert",       &u8) == ESP_OK) settings.steer_invert       = (bool)u8;
    if (nvs_get_u8(h, "max_throttle_pct",   &u8) == ESP_OK) settings.max_throttle_pct   = u8;
    if (nvs_get_u8(h, "always_start_panel", &u8) == ESP_OK) settings.always_start_panel = (bool)u8;

    // uint32 fields
    uint32_t u32;
    if (nvs_get_u32(h, "ota_interval_ms",    &u32) == ESP_OK) settings.ota_interval_ms    = u32;
    if (nvs_get_u32(h, "failsafe_ms",        &u32) == ESP_OK) settings.failsafe_ms        = (uint16_t)u32;
    if (nvs_get_u32(h, "beacon_interval_ms", &u32) == ESP_OK) settings.beacon_interval_ms = u32;

    // float fields as blobs
    size_t sz = sizeof(float);
    float f;
    if (nvs_get_blob(h, "steer_gain",       &f, &sz) == ESP_OK) settings.steer_gain       = f;
    if (nvs_get_blob(h, "steer_filter",     &f, &sz) == ESP_OK) settings.steer_filter     = f;
    if (nvs_get_blob(h, "batt_warn_v",      &f, &sz) == ESP_OK) settings.batt_warn_v      = f;
    if (nvs_get_blob(h, "batt_off_v",       &f, &sz) == ESP_OK) settings.batt_off_v       = f;
    if (nvs_get_blob(h, "adc_corr_factor",  &f, &sz) == ESP_OK) settings.adc_corr_factor  = f;

    // string field
    size_t str_len = sizeof(settings.ap_prefix);
    nvs_get_str(h, "ap_prefix", settings.ap_prefix, &str_len);

    nvs_close(h);
    ESP_LOGI(TAG, "Settings loaded from NVS");
}

void rc_settings_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("settings", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "settings NVS open for write failed: %s", esp_err_to_name(err));
        return;
    }

    // bool fields as uint8
    nvs_set_u8(h, "ota_enabled",        (uint8_t)settings.ota_enabled);
    nvs_set_u8(h, "wifi_tx_power",      settings.wifi_tx_power);
    nvs_set_u8(h, "steer_invert",       (uint8_t)settings.steer_invert);
    nvs_set_u8(h, "max_throttle_pct",   settings.max_throttle_pct);
    nvs_set_u8(h, "always_start_panel", (uint8_t)settings.always_start_panel);

    // uint32 fields
    nvs_set_u32(h, "ota_interval_ms",    settings.ota_interval_ms);
    nvs_set_u32(h, "failsafe_ms",        (uint32_t)settings.failsafe_ms);
    nvs_set_u32(h, "beacon_interval_ms", settings.beacon_interval_ms);

    // float fields as blobs
    nvs_set_blob(h, "steer_gain",      &settings.steer_gain,      sizeof(float));
    nvs_set_blob(h, "steer_filter",    &settings.steer_filter,    sizeof(float));
    nvs_set_blob(h, "batt_warn_v",     &settings.batt_warn_v,     sizeof(float));
    nvs_set_blob(h, "batt_off_v",      &settings.batt_off_v,      sizeof(float));
    nvs_set_blob(h, "adc_corr_factor", &settings.adc_corr_factor, sizeof(float));

    // string field
    nvs_set_str(h, "ap_prefix", settings.ap_prefix);

    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Settings saved to NVS");
}

// ── Crash Recovery ────────────────────────────────────────────────────────────

void rc_recovery_check(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("recovery", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "recovery NVS open failed: %s", esp_err_to_name(err));
        return;
    }

    uint8_t crash_cnt = 0;
    nvs_get_u8(h, "crash_cnt", &crash_cnt); // ignore error on first boot
    crash_cnt++;
    nvs_set_u8(h, "crash_cnt", crash_cnt);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGW(TAG, "Crash counter: %d", crash_cnt);

    if (crash_cnt >= 3) {
        ESP_LOGW(TAG, "Too many crashes — entering SAFE_MODE");
        xEventGroupSetBits(rc_events, SAFE_MODE_BIT);
    }
}

void rc_recovery_mark_stable(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("recovery", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "recovery NVS open (mark_stable) failed: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_u8(h, "crash_cnt", 0);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "System marked stable — crash counter reset");
}

// ── Battery ADC ───────────────────────────────────────────────────────────────

static void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, BATT_ADC_CHANNEL, &chan_cfg));
    ESP_LOGI(TAG, "ADC initialised (channel %d)", BATT_ADC_CHANNEL);
}

static int read_battery_percent(void)
{
    int64_t sum = 0;
    const int samples = 500;

    for (int i = 0; i < samples; i++) {
        int raw = 0;
        adc_oneshot_read(adc_handle, BATT_ADC_CHANNEL, &raw);
        sum += raw;
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    float avg_raw = (float)(sum / samples);
    float v_adc   = (avg_raw / 4095.0f) * 3.1f * settings.adc_corr_factor;
    float v_batt  = v_adc * 3.0f; // voltage divider 1:3

    g_vbatt = v_batt;

    float pct = (v_batt - 7.5f) / (8.39f - 7.5f) * 100.0f;
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    return (int)pct;
}

// ── Servo Steering ────────────────────────────────────────────────────────────

static void steering_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_12_BIT,
        .freq_hz         = 50,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t chan_cfg = {
        .gpio_num   = PIN_SERVO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_6,
        .timer_sel  = LEDC_TIMER_1,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&chan_cfg));
    ESP_LOGI(TAG, "Servo LEDC initialised (GPIO %d, 50 Hz, 12-bit)", PIN_SERVO);
}

static void steering_apply(int16_t steer_input)
{
    float gain   = settings.steer_invert ? -settings.steer_gain : settings.steer_gain;
    float scaled = (float)steer_input * gain;

    // clamp to ±1000
    if (scaled >  1000.0f) scaled =  1000.0f;
    if (scaled < -1000.0f) scaled = -1000.0f;

    // map to pulse width: centre 1500 µs, range 1000–2000 µs
    int us   = 1500 + (int)(scaled * 0.5f);
    // duty for 12-bit @ 50 Hz: period = 20 000 µs, counts = 4096
    uint32_t duty = ((uint32_t)us * 4095UL) / 20000UL;

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_6, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_6);
}

// ── System Task ───────────────────────────────────────────────────────────────

void system_task(void *arg)
{
    adc_init();

    EventBits_t bits = xEventGroupGetBits(rc_events);
    if (!(bits & SAFE_MODE_BIT)) {
        steering_init();
    } else {
        ESP_LOGW(TAG, "SAFE_MODE active — steering disabled");
    }

    esp_task_wdt_add(NULL);

    TickType_t stable_start  = xTaskGetTickCount();
    bool       marked_stable = false;

    for (;;) {
        esp_task_wdt_reset();

        TickType_t now_ticks = xTaskGetTickCount();
        uint32_t   now_ms    = (uint32_t)(now_ticks * portTICK_PERIOD_MS);

        int pct = read_battery_percent();
        battery_percent = pct;
        xQueueOverwrite(batt_queue, &pct);

        bits = xEventGroupGetBits(rc_events);
        if (!(bits & SAFE_MODE_BIT)) {
            Cmd cmd = {0};
            if (xQueuePeek(cmd_queue, &cmd, 0) == pdTRUE) {
                steering_apply(cmd.steer);
            }
        }

        uint32_t stable_ms = (uint32_t)((xTaskGetTickCount() - stable_start) * portTICK_PERIOD_MS);
        if (!marked_stable && stable_ms > 30000) {
            rc_recovery_mark_stable();
            marked_stable = true;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
