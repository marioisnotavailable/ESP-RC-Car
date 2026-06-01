---
tags: [firmware, steering, servo, ledc]
---

# rc_steering — Servo Lenkung

Steuert das Lenkservo über **ESP32 LEDC** (PWM).

> ⚠️ **Konflikt:** `LENKUNG_PIN 5` kollidiert mit `PIN_DRV_MISO 5`.  
> Servo ist aktuell **deaktiviert** bis Pin-Konflikt gelöst ist.

## Servo-Parameter

| Konstante | Wert | Beschreibung |
|---|---|---|
| `SERVO_CH` | 6 | LEDC Kanal |
| `SERVO_FREQ` | 50 Hz | Standard-Servo Frequenz |
| `SERVO_RES_BITS` | 12 | PWM Auflösung |
| `SERVO_MIN_US` | 1000 µs | Linksanschlag |
| `SERVO_MAX_US` | 2000 µs | Rechtsanschlag |
| `SERVO_MID_US` | 1500 µs | Mittelstellung |
| `SERVO_SLEW_US_PER_LOOP` | 0 | Slew-Rate Limit (0 = sofort) |

## Globale Variablen

| Variable | Beschreibung |
|---|---|
| `steerFilt` | Gefilterter Lenkwert (IIR) |
| `currentServoUs` | Aktueller Servo-Pulswert in µs |

## Funktionen

| Funktion | Beschreibung |
|---|---|
| `rc_steering_init_ledc()` | LEDC Kanal konfigurieren |
| `rc_steering_init_fallback()` | Fallback-Initialisierung |
| `rc_steering_write_us(targetUs)` | Servo direkt ansteuern (µs) |
| `rc_steering_apply(steerInput)` | -1000..+1000 → Servo-Pulswert |

## Einstellungen (via [[rc_settings]])

| Setting | Beschreibung |
|---|---|
| `steerInvert` | Lenkrichtung umkehren |
| `steerGain` | Verstärkung (0.3..1.5) |
| `steerDeadzone` | Totzone (0..200) |
| `steerFilter` | IIR Filter Koeffizient (0.5..0.95) |

## Eingabe

Kommt via [[rc_websocket]]: `lastCmd.steer` (-1000..+1000)
