---
tags: [firmware, recovery, safe-mode, nvs, espidf]
---

# rc_recovery — Crash Recovery & Safe Mode (ESP-IDF)

Teil von `rc_system` → `components/rc_system/rc_system.c`

## NVS Storage

- Namespace: `"recovery"`
- Key: `"crash_cnt"` (uint8)

## Logik

```
Boot → rc_recovery_check():
  crash_cnt++ (NVS)
  crash_cnt >= 3 → xEventGroupSetBits(rc_events, SAFE_MODE_BIT)

30s stabiler Betrieb → rc_recovery_mark_stable():
  crash_cnt = 0

Vor esp_restart() (OTA/Console):
  rc_recovery_mark_stable() → crash_cnt = 0
```

## Safe Mode Auswirkungen

| Task | Verhalten |
|---|---|
| `motor_task` | `all_phases_off()` + `vTaskSuspend()` |
| `comms_task` | läuft normal (WiFi + WS erreichbar) |
| `system_task` | kein Steering, ADC läuft |
| `bg_task` | OTA läuft (Safe Mode kann per OTA geheilt werden) |

## Funktionen

| Funktion | Beschreibung |
|---|---|
| `rc_recovery_check()` | Beim Boot: Counter erhöhen, Safe Mode setzen |
| `rc_recovery_mark_stable()` | Counter zurücksetzen |

→ OTA Rollback: [[rc_ota]]
