---
tags: [firmware, hardware, pins, gpio]
---

# rc_pins — Pin-Definitionen ESP32-S3

Alle GPIO-Zuordnungen für den ESP32-S3-DevKitC-1.

## DRV8323S Gate Driver (SPI)

| Signal | GPIO | Beschreibung |
|---|---|---|
| MISO | 5 | ⚠️ Konflikt mit LENKUNG_PIN |
| MOSI | 6 | |
| SCLK | 7 | |
| EN | 16 | Driver Enable |
| CS | 15 | Chip Select |
| nFAULT | 39 | Fault-Signal (Input) |

## Motor PWM (3-Phasen)

| Signal | GPIO | Beschreibung |
|---|---|---|
| INH_A | 18 | Phase A High |
| INL_A | 8 | Phase A Low |
| INH_B | 3 | Phase B High |
| INL_B | 9 | Phase B Low |
| INH_C | 10 | Phase C High |
| INL_C | 11 | Phase C Low |

## Sonstige

| Signal | GPIO | Beschreibung |
|---|---|---|
| LENKUNG_PIN | 5 | ⚠️ Servo (deaktiviert — Konflikt mit MISO) |
| CHARGE_RESTART_PIN | 47 | Laderestart |
| ADC_UB_CHANNEL | GPIO1 | Batteriespannung ADC |

## Bekannte Konflikte

> **GPIO 5:** Wird sowohl als `PIN_DRV_MISO` als auch als `LENKUNG_PIN` definiert.  
> Servo ist deshalb **deaktiviert**. Muss in einer späteren Revision aufgelöst werden.

→ [[rc_steering]] für Details zum Servo
