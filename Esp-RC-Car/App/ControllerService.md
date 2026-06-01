---
tags: [app, flutter, gamepad, joystick]
---

# ControllerService

`ChangeNotifier` — Verarbeitet Joystick/Gamepad-Eingaben und sendet Steuerbefehle.

## Abhängigkeiten

- `ConnectionService` — zum Senden der Befehle

## Eingabequellen

| Quelle | Beschreibung |
|---|---|
| Touch-Joystick | `widgets/joystick.dart` — Touch-Screen |
| Gamepad | Physischer Controller (Xbox, PS, etc.) |
| Windows Gamepad | Native via `gamepad_channel.cpp` |

## Ausgabe

Sendet via `ConnectionService` an ESP32:

```json
{"throttle": -1000..1000, "steer": -1000..1000, "flags": 0}
```

## UI-Komponenten

| Datei | Beschreibung |
|---|---|
| `ui/gamepad_status.dart` | Zeigt ob Gamepad verbunden ist |
| `widgets/joystick.dart` | Touch-Joystick Widget |
| `ui/dev_panel.dart` | Developer-Panel (Debug-Infos, Einstellungen) |

## Flow

```
Touch / Gamepad Input
→ ControllerService
→ normalisieren auf -1000..+1000
→ ConnectionService.send(throttle, steer)
→ WebSocket → ESP32
```
