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
    ├── ui/dev_panel.dart      ← Developer Panel (+ Update-Button)
    ├── ui/gamepad_status.dart ← Gamepad-Verbindungsanzeige
    └── widgets/joystick.dart  ← Touch-Joystick Widget

update_service.dart            ← GitHub-Release Self-Update (Android)
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

## In-App Update (Android)

Die App sucht über die **GitHub Releases API** nach einer neueren Version und installiert sie selbst:

- `update_service.dart` — liest `releases/latest`, vergleicht mit der installierten Version (`package_info_plus`), lädt `EspRCCar.apk` (`http` + `path_provider`) und öffnet den Android-Installer (`open_filex`)
- ⟳-Button im [[DevPanel]] (nur Android) → "Update verfügbar"-Dialog → Download mit Fortschritt → Installation
- Voraussetzung: alle Releases mit **demselben Release-Key** signiert (CI-Signing via GitHub Secrets), sonst Signatur-Konflikt beim Update
- App-Version kommt im CI aus dem Release-Tag (`flutter build --build-name`), damit der Versionsvergleich stimmt

## Module

- [[ConnectionService]] — WebSocket + UDP Discovery
- [[ControllerService]] — Gamepad/Joystick Steuerlogik
- [[UpdateService]] — GitHub-Release Self-Update (Android)

## Windows Besonderheit

Native C++ Gamepad-Integration via `gamepad_channel.cpp/h` im Windows Runner.  
Ermöglicht echtes Gamepad (Xbox/PS Controller) auf Windows.
