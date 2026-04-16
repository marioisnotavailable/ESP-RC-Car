---
tags: [overview, esp32, rc-car, espidf]
---

# ESP-RC-Car — Projektübersicht

Ein selbst entwickeltes, open-source Drift-Car für ein Schulprojekt (4BHELS).  
Gesteuert über ESP32-S3, angetrieben von einem BLDC-Motor, kontrolliert per Flutter-App über WebSocket.

> **Aktueller Branch:** `esp-idf-migration` — Arduino → ESP-IDF Migration abgeschlossen

---

## Systemarchitektur

```
[Flutter App] ──WebSocket Port 80──► [ESP32-S3 Firmware]
     │                                       │
     └── UDP Discovery :49352 ───────────────┘
                                             │
                         ┌───────────────────┼───────────────┐
                         ▼                   ▼               ▼
                   [DRV8323S]          [Servo GPIO4]   [Batterie ADC]
                   [BLDC Motor]
```

## FreeRTOS Task-Architektur

```
app_main() → init → rc_recovery_check() → rc_settings_load()
     │
     ├── motor_task   Prio 10  4KB  → [[Firmware/rc_motor]]
     ├── comms_task   Prio 7   8KB  → [[Firmware/rc_network]] + [[Firmware/rc_websocket]]
     ├── system_task  Prio 5   6KB  → [[Firmware/rc_battery]] + [[Firmware/rc_settings]] + [[Firmware/rc_steering]]
     └── bg_task      Prio 2   6KB  → [[Firmware/rc_ota]]
```

## Hauptkomponenten

### Firmware (ESP32-S3)
→ [[Firmware/Firmware Overview]]

| Komponente | Task | Funktion |
|---|---|---|
| [[Firmware/rc_motor]] | motor_task | 6-Step BLDC, DRV8323S SPI, LEDC |
| [[Firmware/rc_websocket]] | comms_task | WebSocket esp_http_server Port 80 |
| [[Firmware/rc_network]] | comms_task | WiFi esp_wifi, UDP Discovery |
| [[Firmware/rc_battery]] | system_task | ADC adc_oneshot, Batterie % |
| [[Firmware/rc_ota]] | bg_task | FOTA esp_https_ota |
| [[Firmware/rc_recovery]] | system_task | Crash-Counter, Safe Mode |
| [[Firmware/rc_steering]] | system_task | Servo LEDC, GPIO4 |
| [[Firmware/rc_settings]] | app_main | NVS DeviceSettings |
| [[Firmware/rc_pins]] | — | Pin-Definitionen ESP32-S3 |
| [[Firmware/drv8323]] | motor_task | DRV8323S SPI Treiber |

### App (Flutter)
→ [[App/App Overview]]

### Hardware
→ [[Hardware/PCB]] | [[Hardware/Motor & Treiber]]

---

## Technologie-Stack

| Bereich | Tech |
|---|---|
| MCU | ESP32-S3 (16MB Flash) |
| Firmware | PlatformIO + **ESP-IDF** Framework |
| RTOS | FreeRTOS (4 Tasks) |
| App | Flutter (Android, iOS, Windows, Web) |
| Motor | Mini1410 2500kv BLDC |
| Treiber | DRV8323SRTAR (SPI) |
| Kommunikation | WebSocket (Port 80), UDP Discovery (Port 49352) |
| OTA | GitHub Releases (esp_https_ota) |
| Einstellungen | ESP-IDF NVS |
| PCB | Altium Designer |

---

## Repo-Struktur

```
ESP-RC-Car/
├── App/          Flutter App (Dart)
├── Code/         ESP32 Firmware (PlatformIO + ESP-IDF)
│   ├── src/      main.c
│   └── components/
│       ├── rc_common/   Shared Types + Queues
│       ├── rc_motor/    Motor + DRV8323
│       ├── rc_comms/    WiFi + WebSocket + UDP
│       ├── rc_system/   ADC + NVS + Recovery + Servo
│       └── rc_bg/       FOTA + Console
├── PCB/          Altium Schaltplan + Layout
├── STL/          Chassis 3D-Druck (Chassi V8)
└── Esp-RC-Car/   Obsidian Vault (dieses Brain)
```
