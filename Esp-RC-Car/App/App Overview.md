---
tags: [app, flutter, dart, esp-rc-car]
---

# Flutter App — Übersicht

Cross-platform Controller-App für das [[ESP-RC-Car]], geschrieben in **Flutter/Dart**. Steuert das Fahrzeug per WiFi-WebSocket in Echtzeit, zeigt die Batteriespannung an und erlaubt die Server-Suche im lokalen WLAN.

## Plattformen

- Android (Release-APK: `App/final_build/EspRCCar.apk`)
- iOS (CocoaPods-Integration, IPA: `App/final_build/EspRCCar.ipa`)
- Windows (nativer Gamepad-Support via `gamepad_channel.cpp`)
- macOS, Linux, Web (Flutter-Standard)

## Architektur

```
main.dart
├── MultiProvider
│   ├── ConnectionService    ← WebSocket + UDP/TCP-Discovery
│   └── ControllerService    ← Gamepad/Joystick → Steuerbefehle
└── RCCarApp
    ├── ui/dev_panel.dart      ← DevPanel (Diagnose + Update)
    ├── ui/gamepad_status.dart ← Gamepad-Verbindungsanzeige
    └── widgets/joystick.dart  ← Touch-Joystick-Widget

update_service.dart            ← GitHub-Release Self-Update (Android)
```

State-Management über das `provider`-Package; sowohl `ConnectionService` als auch `ControllerService` sind `ChangeNotifier`.

## UI-Ausrichtung

App startet immer im **Landscape-Modus**:

```dart
SystemChrome.setPreferredOrientations([
  DeviceOrientation.landscapeLeft,
  DeviceOrientation.landscapeRight,
])
```

## Steuerung

- Virtueller Joystick mit konfigurierbarer Empfindlichkeit / Totzone und Skalierungsfunktion (gegen Zittern)
- Physischer Gamepad-Controller als Alternative (Bluetooth/USB)
- 50 Hz Sendeloop mit EMA-Tiefpassfilterung
- Übertragung über WebSocket: Throttle/Steer/Flags als JSON oder Rohwerte

## Server-Discovery

1. Standardmäßig **UDP-Broadcast-Discovery** auf dem lokalen Netz → Fahrzeug antwortet mit seinem WebSocket-Endpunkt
2. Fallback: **TCP-Subnet-Scan** falls UDP blockiert wird
3. Zuletzt verwendete URL wird in `SharedPreferences` persistiert (Standard im Soft-AP-Modus: `ws://192.168.4.1:81/`)
4. Manuelle URL-Eingabe im DevPanel

## Batterieanzeige

Echtzeit-Anzeige der 2S-LiPo-Spannung mit prozentualer Restkapazität, Farbcode und Icon. Der Wert kommt aus dem `batt_queue` der Firmware und wird über WebSocket gepusht.

## DevPanel

- Verbindungsstatus, Latenz, empfangene Sensordaten
- Manuelle WebSocket-URL
- ⟳-Update-Button (nur Android)

## In-App-Update (Android)

- `update_service.dart` liest `releases/latest` über die GitHub-Releases-API, vergleicht mit der installierten Version (`package_info_plus`)
- Bei neuer Version: APK herunterladen (`http` + `path_provider`), Installer öffnen (`open_filex`)
- Voraussetzung: alle Releases mit demselben Signing-Key
- App-Version kommt im CI aus dem Release-Tag (`flutter build --build-name`)

## Bibliotheken

| Paket | Zweck |
|---|---|
| `web_socket_channel` | WebSocket zur ESP32-Firmware |
| `flutter_joystick` | virtueller Joystick |
| `gamepad` | physischer Controller |
| `shared_preferences` | Server-URL persistieren |
| `provider` | State-Management |
| `package_info_plus` / `http` / `path_provider` / `open_filex` | Self-Update |

## Module

- [[ConnectionService]] — WebSocket + UDP/TCP-Discovery
- [[ControllerService]] — Gamepad/Joystick-Steuerlogik
- [[UpdateService]] — GitHub-Release Self-Update (Android)
