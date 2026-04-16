---
tags: [app, flutter, dart]
---

# Flutter App — Übersicht

Cross-platform Controller-App für das ESP-RC-Car, geschrieben in **Flutter/Dart**.

## Plattformen

- Android (APK: `App/final_build/EspRCCar.apk`)
- iOS (IPA: `App/final_build/EspRCCar.ipa`)
- Windows (mit nativem Gamepad-Support via `gamepad_channel.cpp`)
- macOS, Linux, Web

## Architektur

```
main.dart
├── MultiProvider
│   ├── ConnectionService    ← WebSocket + UDP Discovery
│   └── ControllerService   ← Gamepad/Joystick → Befehle
└── RCCarApp
    ├── ui/dev_panel.dart      ← Developer Panel
    ├── ui/gamepad_status.dart ← Gamepad-Verbindungsanzeige
    └── widgets/joystick.dart  ← Touch-Joystick Widget
```

## State Management

- `provider` Package
- `ConnectionService` (ChangeNotifier) — Verbindungsstatus
- `ControllerService` (ChangeNotifier) — Steuerbefehle

## UI-Ausrichtung

App startet immer im **Landscape-Modus** (links oder rechts):

```dart
SystemChrome.setPreferredOrientations([
  DeviceOrientation.landscapeLeft,
  DeviceOrientation.landscapeRight,
])
```

## Kommunikation

```
App → UDP Broadcast (Port 49352)  → ESP32 Discovery
App → WebSocket ws://[ip]:81      → Throttle/Steer Befehle
ESP32 → WebSocket                 → Batterie % empfangen
```

## Module

- [[ConnectionService]] — WebSocket + UDP Discovery
- [[ControllerService]] — Gamepad/Joystick Steuerlogik

## Windows Besonderheit

Native C++ Gamepad-Integration via `gamepad_channel.cpp/h` im Windows Runner.  
Ermöglicht echtes Gamepad (Xbox/PS Controller) auf Windows.
