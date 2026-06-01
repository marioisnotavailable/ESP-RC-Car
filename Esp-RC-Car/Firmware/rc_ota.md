---
tags: [firmware, ota, fota, espidf]
---

# rc_ota — FOTA (ESP-IDF)

Teil von `bg_task` → `components/rc_bg/rc_bg.c`

Firmware Over-The-Air via **esp_https_ota**.

## FOTA URL

```
https://github.com/marioisnotavailable/ESP-RC-Car/releases/latest/download/esp-rc-car.bin
```

## ESP-IDF API

```c
esp_http_client_config_t http_cfg = {
    .url = FOTA_FW_URL,
    .skip_cert_common_name_check = true,
    .timeout_ms = 10000,
};
esp_https_ota_config_t ota_cfg = {.http_config = &http_cfg};
esp_err_t ret = esp_https_ota(&ota_cfg);
```

## Ablauf

```
bg_task wartet auf WIFI_CONNECTED_BIT
→ periodisch (settings.ota_interval_ms, default 5 min) do_fota()
→ ESP_OK → rc_recovery_mark_stable() + esp_restart()
→ Fehler → log + weiter
```

## OTA Rollback

Beim Start:
```c
esp_ota_mark_app_valid_cancel_rollback();
```
Verhindert automatischen Rollback wenn Firmware stabil läuft.

## Serial Console Trigger

```
ota   → sofortiger FOTA-Check
```

→ [[rc_recovery]] für Rollback-Mechanismus
