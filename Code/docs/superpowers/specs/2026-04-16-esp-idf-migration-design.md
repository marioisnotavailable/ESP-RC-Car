# ESP-IDF Migration Design

**Datum:** 2026-04-16  
**Branch:** neu zu erstellen von `espFOTA`  
**Ziel:** Komplette Firmware-Neuentwicklung auf ESP-IDF (PlatformIO framework=espidf), strukturiert als FreeRTOS Task-Architektur

---

## Motivation

- **FreeRTOS Kontrolle:** Motor-Kommutierung mit garantierter Echtzeit-Priorität
- **Hardware-Kontrolle:** Direkter Zugriff auf ESP-IDF LEDC, SPI, ADC, OTA APIs
- **Performance:** Arduino-Overhead eliminieren (HAL-Schichten entfernen)

---

## Task-Architektur

Vier FreeRTOS Tasks, geschichtet nach Timing-Anforderungen:

| Task | Priorität | Stack | Verantwortung |
|---|---|---|---|
| `motor_task` | 10 (CRITICAL) | 4 KB | 6-Step BLDC Kommutierung, DRV8323S SPI, Fault-Handling |
| `comms_task` | 7 (HIGH) | 8 KB | WiFi, WebSocket Server, UDP Discovery, Failsafe |
| `system_task` | 5 (NORMAL) | 6 KB | Battery ADC, Recovery/Watchdog, Settings, Steering |
| `bg_task` | 2 (LOW) | 6 KB | FOTA/OTA, NTP, Serial Console |

### app_main Sequenz

```c
nvs_flash_init()
esp_netif_init()
esp_event_loop_create_default()
rc_events = xEventGroupCreate()
cmd_queue = xQueueCreate(4, sizeof(Cmd))
batt_queue = xQueueCreate(1, sizeof(int))

rc_recovery_check()    // Crash-Counter → ggf. SAFE_MODE_BIT setzen
rc_settings_load()     // NVS Settings laden
rc_fs_init()           // LittleFS mounten

xTaskCreate(motor_task, ...)
xTaskCreate(comms_task, ...)
xTaskCreate(system_task, ...)
xTaskCreate(bg_task, ...)

vTaskDelete(NULL)
```

---

## Inter-Task Kommunikation

```
comms_task
  WebSocket empfängt {throttle, steer, flags}
  └─► xQueueSend(cmd_queue) ──────────────► motor_task
                                              xQueueReceive(cmd_queue)
                                              └─► LEDC PWM anwenden

system_task
  ADC → batteryPercent berechnen
  └─► xQueueOverwrite(batt_queue) ────────► comms_task
                                              xQueuePeek(batt_queue)
                                              └─► WS broadcast {batt: X}

EventGroup: rc_events (xEventGroupHandle_t)
  Bit 0: WIFI_CONNECTED_BIT
  Bit 1: SAFE_MODE_BIT
  Bit 2: MOTOR_FAULT_BIT
```

---

## Projektstruktur

```
Code/
├── platformio.ini              framework = espidf
├── sdkconfig.defaults
├── partitions.csv              (unverändert)
├── src/
│   └── main.c                  app_main: init + 4 Tasks spawnen
└── components/
    ├── rc_common/              Shared: Cmd struct, queue handles, event group, Pins
    ├── rc_motor/               motor_task + DRV8323 SPI Treiber
    ├── rc_comms/               comms_task: WiFi, esp_http_server WS, UDP
    ├── rc_system/              system_task: ADC, NVS Settings, Recovery, Steering LEDC
    └── rc_bg/                  bg_task: esp_https_ota, NTP, UART Console
```

Jede Komponente hat:
```
components/rc_xxx/
├── CMakeLists.txt
├── include/rc_xxx.h
└── rc_xxx.c
```

---

## Arduino → ESP-IDF API Migration

| Arduino (aktuell) | ESP-IDF (neu) | Komponente |
|---|---|---|
| `Preferences` | `nvs_flash` + `nvs_open/get/set` | rc_system |
| `WiFi.h` | `esp_wifi` + `esp_netif` | rc_comms |
| `WebSocketsServer` | `esp_http_server` (WS built-in) | rc_comms |
| `WiFiUDP` (Discovery) | `lwip/sockets.h` UDP socket | rc_comms |
| `LEDC` | `ledc_timer_config` + `ledc_channel_config` | rc_motor, rc_system |
| `SPI` (DRV8323) | `spi_bus_initialize` + `spi_device_handle_t` | rc_motor |
| `Update.h` + `ArduinoOTA` | `esp_ota_ops` + `esp_https_ota` | rc_bg |
| `LittleFS` | `esp_littlefs` component | rc_system |
| `analogRead` | `adc_oneshot_read` | rc_system |
| `Serial` / `printf` | `ESP_LOGI` + `uart_driver_install` | rc_bg |
| `esp_timer` (loop timing) | `esp_timer_create` (one-shot/periodic) | rc_motor |

---

## Fehlerbehandlung & Safe Mode

### Safe Mode

```
rc_recovery_check() (in app_main, vor Task-Start):
  NVS Crash-Counter lesen
  Counter++, speichern
  wenn Counter >= 3 → xEventGroupSetBits(rc_events, SAFE_MODE_BIT)

motor_task:
  beim Start: xEventGroupWaitBits(SAFE_MODE_BIT)
  wenn gesetzt → vTaskSuspend(NULL)

system_task:
  esp_task_wdt_reset() in Loop
  nach 30s Uptime → Counter = 0 (rc_recovery_mark_stable)
```

### WebSocket Failsafe

```
comms_task prüft lastCmdMs in Loop:
  wenn (esp_timer_get_time() - lastCmdMs) > failsafe_us
  → xQueueSend(cmd_queue, {.throttle=0, .steer=0})
```

### Motor Fault (DRV8323 nFAULT)

```
motor_task prüft GPIO nFAULT (GPIO39, INPUT):
  Fault erkannt:
  → alle LEDC Kanäle duty=0
  → xEventGroupSetBits(rc_events, MOTOR_FAULT_BIT)
  → vTaskDelay(500ms)
  → drv8323_clear_faults() via SPI
  → retry
```

### OTA Rollback

```
bg_task nach erfolgreichem Boot:
  esp_ota_mark_app_valid_cancel_rollback()
  → verhindert automatischen Rollback bei nächstem Crash
```

---

## Bekannte Probleme aus dem alten Code

| Problem | Lösung in ESP-IDF Migration |
|---|---|
| GPIO5 Konflikt (MISO vs. Servo) | Servo-Pin auf freien GPIO umlegen (z.B. GPIO4) |
| Motor läuft nur im Diagnose-Modus | Echte Kommutierung via WebSocket throttle implementieren |
| `safeMode` global bool | Ersetzt durch `SAFE_MODE_BIT` im EventGroup |

---

## Build-Targets (platformio.ini)

| Environment | Beschreibung |
|---|---|
| `espidf` | Hauptfirmware (USB Serial) |
| `espidf_ota` | OTA Upload via `esp-rc-car.local` |
| `espidf_factory` | Factory Recovery (minimale Komponenten) |
