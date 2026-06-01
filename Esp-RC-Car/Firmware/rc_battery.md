---
tags: [firmware, battery, adc, espidf]
---

# rc_battery — Batterie ADC (ESP-IDF)

Teil von `system_task` → `components/rc_system/rc_system.c`

## ESP-IDF API: adc_oneshot

```c
adc_oneshot_unit_init_cfg_t init = {.unit_id = ADC_UNIT_1};
adc_oneshot_new_unit(&init, &adc_handle);

adc_oneshot_chan_cfg_t cfg = {
    .bitwidth = ADC_BITWIDTH_DEFAULT,
    .atten    = ADC_ATTEN_DB_12,
};
adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_0, &cfg);
```

## Messung

| Parameter | Wert |
|---|---|
| ADC Kanal | ADC_CHANNEL_0 (GPIO1) |
| Samples | 500 |
| Delay zwischen Samples | 2ms |
| Gesamtdauer | ~1 Sekunde |

## Berechnung

```
avg_raw / 4095 * 3.1V * adc_corr_factor = v_adc
v_batt = v_adc * 3.0  (Spannungsteiler 1:3 auf PCB)
pct = (v_batt - 7.5V) / (8.39V - 7.5V) * 100
```

## Grenzen

| Wert | Spannung |
|---|---|
| 100% | 8.39 V |
| 0% | 7.50 V |
| Warnung (`batt_warn_v`) | 7.6 V (einstellbar) |
| Abschaltung (`batt_off_v`) | 7.5 V (einstellbar) |

## Output

`battery_percent` (volatile int) → `xQueueOverwrite(batt_queue)` → `comms_task` → WS broadcast
