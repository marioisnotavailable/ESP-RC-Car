---
tags: [firmware, esp32, espidf, freertos]
---

# Firmware Overview

PlatformIO Projekt, **ESP-IDF Framework**, Ziel: **ESP32-S3-DevKitC-1**

> ⚠️ Branch `esp-idf-migration` — komplett von Arduino auf ESP-IDF migriert.

## Build-Targets

| Environment | Beschreibung |
|---|---|
| `espidf` | Hauptfirmware (USB Serial Upload) |
| `espidf_ota` | OTA Upload via `esp-rc-car.local` |
| `espidf_factory` | Factory Recovery Firmware |

OTA Passwort: `esprccar`

## Projektstruktur

```
Code/
├── platformio.ini       framework = espidf
├── sdkconfig.defaults   ESP-IDF Konfiguration
├── partitions.csv       Flash-Partitionierung
├── src/
│   └── main.c           app_main: Init + 4 Tasks spawnen
└── components/
    ├── rc_common/       Shared: Cmd, Queues, EventGroup, Pins
    ├── rc_motor/        motor_task + DRV8323 SPI
    ├── rc_comms/        comms_task: WiFi, WebSocket, UDP
    ├── rc_system/       system_task: ADC, NVS, Recovery, Servo
    └── rc_bg/           bg_task: FOTA, Serial Console
```

> Keine externen Libraries eingebettet. Alle ESP-IDF built-in APIs.

## Flash-Konfiguration

- Flash: 16MB QIO OPI
- Filesystem: SPIFFS (LittleFS removed)
- Partitions: `partitions.csv` (custom)

## ESP-IDF sdkconfig Highlights

- CPU: 240 MHz
- FreeRTOS: 1000 Hz Tick
- Watchdog: 10s Timeout
- WebSocket Support: aktiviert
- SPIRAM Support: aktiviert
- OTA Rollback: aktiviert (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`)

## FreeRTOS Task-Architektur

```
app_main()
  nvs_flash_init() → esp_netif_init() → rc_common_init()
  rc_recovery_check() → rc_settings_load()
  │
  ├── motor_task   Prio 10  4KB   BLDC Kommutierung + DRV8323
  ├── comms_task   Prio 7   8KB   WiFi + WebSocket + UDP
  ├── system_task  Prio 5   6KB   ADC + Settings + Recovery + Servo
  └── bg_task      Prio 2   6KB   FOTA + Serial Console
```

## Inter-Task Kommunikation

```
comms_task → xQueueOverwrite(cmd_queue)  → motor_task
system_task → xQueueOverwrite(batt_queue) → comms_task (broadcast)

rc_events (EventGroup):
  BIT0: WIFI_CONNECTED_BIT
  BIT1: SAFE_MODE_BIT    → motor_task suspendiert sich
  BIT2: MOTOR_FAULT_BIT  → DRV8323 Fault erkannt
```

## Boot-Sequenz

```
nvs_flash_init()
esp_netif_init() + esp_event_loop_create_default()
rc_common_init()        → Queues + EventGroup erstellen
rc_recovery_check()     → crash_cnt prüfen, ggf. SAFE_MODE_BIT setzen
rc_settings_load()      → NVS Settings laden
4x xTaskCreate()
vTaskDelete(NULL)
```

## Module

- [[rc_motor]] — BLDC Motor + DRV8323S
- [[rc_websocket]] — WebSocket (esp_http_server)
- [[rc_network]] — WiFi (esp_wifi) + UDP (lwip)
- [[rc_battery]] — Batterie ADC (adc_oneshot)
- [[rc_ota]] — FOTA (esp_https_ota)
- [[rc_recovery]] — Crash/Safe Mode (NVS)
- [[rc_steering]] — Servo (LEDC)
- [[rc_settings]] — NVS Settings
- [[rc_pins]] — Pin Definitionen
- [[drv8323]] — Gate Driver SPI
