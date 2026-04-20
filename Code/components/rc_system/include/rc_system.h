#pragma once
#include "rc_common.h"

typedef struct {
    bool     ota_enabled;
    uint32_t ota_interval_ms;
    uint8_t  wifi_tx_power;
    uint16_t failsafe_ms;
    uint32_t beacon_interval_ms;
    char     ap_prefix[32];
    bool     always_start_panel;
    bool     steer_invert;
    float    steer_gain;
    uint16_t steer_deadzone;
    float    steer_filter;
    float    batt_warn_v;
    float    batt_off_v;
    uint8_t  max_throttle_pct;
    float    adc_corr_factor;
} DeviceSettings;

extern DeviceSettings settings;
extern volatile int   battery_percent;
extern volatile float g_vbatt;

void rc_settings_load(void);
void rc_settings_save(void);
void rc_recovery_check(void);
void rc_recovery_mark_stable(void);
void system_task(void *arg);
