---
tags: [firmware, motor, bldc, drv8323, ledc, freertos]
---

# rc_motor — BLDC Motor Steuerung (ESP-IDF)

Implementiert 6-Step Trapezkommutierung für den **Mini1410 2500kv BLDC Motor** als FreeRTOS Task.

## Dateien

- `components/rc_motor/include/rc_motor.h`
- `components/rc_motor/rc_motor.c`
- `components/rc_motor/include/drv8323.h` → [[drv8323]]
- `components/rc_motor/drv8323.c` → [[drv8323]]

## Task

| Parameter | Wert |
|---|---|
| Name | `motor_task` |
| Priorität | 10 (höchste) |
| Stack | 4 KB |

## PWM-Konfiguration (ESP-IDF LEDC)

| Parameter | Wert |
|---|---|
| Frequenz | 20 kHz |
| Auflösung | 10-bit (0..1023) |
| LEDC Timer | TIMER_0 |
| Kanäle | CHANNEL_0..5 |

## 6-Step Kommutierungstabelle

```
         INH_A INL_A INH_B INL_B INH_C INL_C
Step 0:    PWM   LOW   LOW   PWM  FLOAT FLOAT  → A+, B-
Step 1:    PWM   LOW  FLOAT FLOAT  LOW   PWM   → A+, C-
Step 2:   FLOAT FLOAT  PWM  LOW    LOW   PWM   → B+, C-
Step 3:    LOW   PWM   PWM  LOW   FLOAT FLOAT  → B+, A-
Step 4:    LOW   PWM  FLOAT FLOAT  PWM  LOW    → C+, A-
Step 5:   FLOAT FLOAT  LOW   PWM   PWM  LOW    → C+, B-
```

## Task Loop

```
1. SAFE_MODE_BIT prüfen → all_phases_off() + vTaskSuspend()
2. DRV8323 nFAULT prüfen → handle_fault() falls Fehler
3. xQueueReceive(cmd_queue) non-blocking
4. throttle == 0 → all_phases_off()
5. duty = |throttle| * 1023 / 1000
6. step±1 je nach Richtung
7. apply_phase(step, duty)
8. vTaskDelay(10ms)
```

## Safe Mode

Wenn `SAFE_MODE_BIT` gesetzt → Motor sofort aus + Task suspendiert sich.

## Fault Handling

```
drv8323_has_fault() → true
→ all_phases_off()
→ MOTOR_FAULT_BIT setzen
→ 500ms warten
→ drv8323_clear_faults()
→ MOTOR_FAULT_BIT löschen
```

## Pins (via [[rc_pins]])

| Signal | GPIO |
|---|---|
| INH_A | 18 |
| INL_A | 8 |
| INH_B | 3 |
| INL_B | 9 |
| INH_C | 10 |
| INL_C | 11 |

## Input

Kommt via [[rc_network]] (UDP) + [[rc_websocket]] → `cmd_queue`:
- `throttle`: -1000..+1000
- `steer`: wird von [[rc_steering]] verarbeitet

→ [[Hardware/Motor & Treiber]] für Hardware-Details
