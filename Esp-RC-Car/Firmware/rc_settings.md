---
tags: [firmware, settings, nvs, espidf]
---

# rc_settings — Geräteeinstellungen (ESP-IDF NVS)

Teil von `rc_system` → `components/rc_system/rc_system.c`

## NVS Namespace: `"settings"`

```c
typedef struct {
    bool     ota_enabled;       // Default: true
    uint32_t ota_interval_ms;   // Default: 300000 (5 min)
    uint8_t  wifi_tx_power;     // Default: 3 (max)
    uint16_t failsafe_ms;       // Default: 500ms
    uint32_t beacon_interval_ms;
    char     ap_prefix[32];     // Default: "ESP-RC-"
    bool     always_start_panel;
    bool     steer_invert;
    float    steer_gain;        // Default: 1.0
    uint16_t steer_deadzone;    // Default: 50
    float    steer_filter;      // Default: 0.7
    float    batt_warn_v;       // Default: 7.6V
    float    batt_off_v;        // Default: 7.5V
    uint8_t  max_throttle_pct;  // Default: 100
    float    adc_corr_factor;   // Default: 1.0
} DeviceSettings;
```

## NVS Keys

| Key | Typ | Feld |
|---|---|---|
| `ota_en` | uint8 | ota_enabled |
| `ota_iv` | uint32 | ota_interval_ms |
| `tx_pwr` | uint8 | wifi_tx_power |
| `fs_ms` | uint32 | failsafe_ms |
| `steer_inv` | uint8 | steer_invert |
| `max_thr` | uint8 | max_throttle_pct |
| `adc_corr` | blob (float) | adc_corr_factor |
| `batt_warn` | blob (float) | batt_warn_v |
| `batt_off` | blob (float) | batt_off_v |

## Funktionen

| Funktion | Beschreibung |
|---|---|
| `rc_settings_load()` | NVS lesen → `settings` struct befüllen |
| `rc_settings_save()` | `settings` struct → NVS schreiben |

Wird von `app_main` nach `rc_recovery_check()` aufgerufen.
