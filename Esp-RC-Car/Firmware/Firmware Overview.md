---
tags: [firmware, esp32, espidf, freertos, esp-rc-car]
---

# Firmware Overview

PlatformIO-Projekt auf **ESP-IDF**, Ziel: **ESP32-S3-WROOM-1-N8** (8 MB Flash). Hauptaufgaben: BLDC-Motoransteuerung, WiFi/WebSocket-Server, Servo-Lenkung, ADC-Batteriemessung, USB-PD-Konfiguration, OTA-Update, Crash-Recovery / Safe Mode.

Siehe auch: [[ESP-RC-Car|Projekt-Übersicht]], [[PCB]], [[Motor & Treiber]].

## Build-Environments

| Environment | Beschreibung |
|---|---|
| `espidf` | Hauptfirmware (USB Serial Upload) |
| `espidf_ota` | OTA-Upload via `esp-rc-car.local` |
| `espidf_factory` | Factory-Recovery-Firmware |

OTA-Passwort: `esprccar`.

## Projektstruktur

```
Code/
├── platformio.ini       framework = espidf
├── sdkconfig.defaults   ESP-IDF Konfiguration
├── partitions.csv       Flash-Partitionierung
├── src/main.c           app_main: Init + 4 Tasks spawnen
└── components/
    ├── rc_common/       Shared: Cmd, Queues, EventGroup, Pins
    ├── rc_motor/        motor_task + DRV8323 SPI
    ├── rc_comms/        comms_task: WiFi, WebSocket, UDP
    ├── rc_system/       system_task: ADC, NVS, Recovery, Servo
    └── rc_bg/           bg_task: FOTA, Serial Console
```

## Flash & Partitionen

- 16 MB QIO-OPI Flash
- Filesystem: **LittleFS** (Partition-Label `littlefs`, Mount `/littlefs`)
- Partitions: `partitions.csv` (custom, inkl. zwei OTA-App-Slots + Factory)

## sdkconfig Highlights

- CPU: 240 MHz
- FreeRTOS: 1000 Hz Tick
- Task-Watchdog: 10 s Timeout
- WebSocket-Support: aktiviert
- SPIRAM: aktiv im **OCT-Modus** (Board hat OPI-PSRAM)
- OTA-Rollback: `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`

## FreeRTOS Task-Architektur

```
app_main()
  nvs_flash_init() → esp_netif_init() → rc_common_init()
  rc_recovery_check() → rc_settings_load()
  │
  ├── motor_task   Prio 10  4 KB  BLDC Kommutierung + DRV8323
  ├── comms_task   Prio 7   8 KB  WiFi + WebSocket + UDP
  ├── system_task  Prio 5   6 KB  ADC + Settings + Recovery + Servo
  └── bg_task      Prio 2   6 KB  FOTA + Serial Console
```

### Inter-Task-Kommunikation

```
comms_task  → xQueueOverwrite(cmd_queue)  → motor_task
system_task → xQueueOverwrite(batt_queue) → comms_task (broadcast)

rc_events (EventGroup):
  BIT0: WIFI_CONNECTED_BIT
  BIT1: SAFE_MODE_BIT     → motor_task suspendiert sich
  BIT2: MOTOR_FAULT_BIT   → DRV8323 Fault erkannt
```

## WiFi & Konfiguration

Beim Boot versucht der ESP32-S3 sich mit einem in NVS gespeicherten WLAN zu verbinden. Wird kein bekanntes Netzwerk gefunden, startet er einen **Soft-AP** mit eingebettetem Webserver, über den per Browser SSID/Passwort hinterlegt werden können. Nach Neustart verbindet sich der MCU mit dem konfigurierten Netzwerk und betreibt dort den WebSocket-Server für die Steuerung. Der AP dient ausschließlich der Konfiguration — die Fahrzeugsteuerung läuft nur im gemeinsamen WLAN.

Discovery: Der ESP32-S3 sendet einen UDP-Beacon mit seinem WebSocket-Endpunkt; die App findet ihn darüber automatisch, als Fallback per TCP-Subnet-Scan.

## BLDC-Kommutierung

- Gate-Treiber: **DRV8323SRTAR** (TI) über dedizierten SPI-Bus
- 6 PWM-Kanäle via **LEDC** für die 3-Phasen-Vollbrücke (INH_A/B/C high-side, INL_A/B/C low-side)
- 6-Step-Trapezoid-Kommutierung mit Ramp-Up
- Fault-Erkennung über `nFAULT`-Pin → `MOTOR_FAULT_BIT`
- Phasenstromerfassung über drei Low-Side-Shunts mit den Stromsense-Ausgängen SOA/SOB/SOC

## Servo-Lenkung

Servo wird über LEDC-PWM angesteuert (~50 Hz, 1–2 ms Pulsbreite, Mitte 1,5 ms).

## Batterie & USB-PD

- `rc_battery`: 2S-LiPo-Spannung via ADC-Spannungsteiler an GPIO13/GPIO14 (`adc_oneshot`), Wert wird über WebSocket an verbundene App-Clients gepusht
- `rc_usbpd` (in `rc_system`): I²C-Konfiguration des **STUSB4500** (Sink-Profil 12 V), `ALERT` an GPIO8, I²C an GPIO9/GPIO10

## OTA

`rc_ota` nutzt `esp_https_ota` für signierte Firmware-Updates ohne USB-Kabel; Rollback ist im Bootloader aktiviert.

## Safe Mode / Recovery

- `rc_recovery_check()` liest einen NVS-Crash-Counter beim Boot
- Häufen sich Crashes, wird `SAFE_MODE_BIT` gesetzt → `motor_task` suspendiert sich, Fahrzeug bleibt steuerlos aber im Netz erreichbar
- Stabilisiert sich die Firmware, ruft das System `rc_recovery_mark_stable()` und der Counter wird zurückgesetzt

## Pin-Map (Auszug)

| Funktion | GPIO |
|---|---|
| DRV8323 SPI (SCLK / MOSI / MISO / SS) | 5 / 6 / 7 / 16 |
| DRV8323 nFAULT | 39 |
| DRV8323 CAL | 28 |
| INH_A / INH_B / INH_C | 18 / 3 / 10 |
| INL_A / INL_B / INL_C | 8 / 9 / 11 |
| I²C SDA / SCL (STUSB4500, BQ29200) | 21 / 47 |
| STUSB4500 ALERT | 8 |
| Batt ADC Zellen | 13 / 14 |
| LMR51430 Enable | 3 |

## Module

- [[rc_motor]] — BLDC + DRV8323S
- [[rc_websocket]] — WebSocket-Server (esp_http_server)
- [[rc_network]] — WiFi (esp_wifi) + UDP-Discovery
- [[rc_battery]] — Batterie-ADC
- [[rc_ota]] — FOTA (esp_https_ota)
- [[rc_recovery]] — Crash / Safe Mode
- [[rc_steering]] — Servo-LEDC
- [[rc_settings]] — NVS Settings
- [[rc_pins]] — Pin-Definitionen
- [[drv8323]] — Gate-Driver-SPI-Treiber
