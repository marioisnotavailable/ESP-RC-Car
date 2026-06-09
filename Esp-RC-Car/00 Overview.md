---
tags: [moc, esp-rc-car, diplomarbeit]
---

# ESP-RC-Car

Diplomarbeitsprojekt **HTL St. Pölten**, 4BHELS — Mario Haunold & Leon Dillinger.
Ferngesteuertes Elektrofahrzeug mit eigenentwickelter Platine, BLDC-Antrieb über DRV8323S, WiFi-WebSocket-Steuerung über eine Flutter-App und vollständigem Energiemanagement (USB-PD-Laden, 2S-LiPo, Zellbalancer).

## Kontext

- Schuljahr 2025/26 — Planung Sept. 2025, erste Platine bestellt/getestet Dez. 2025, überarbeitete Platine ab März 2026
- Abgabedatum 29.5.2026, Lehrer RZEP
- Eigene Mehrlagen-PCB in Altium Designer, eigene Firmware (PlatformIO/ESP-IDF), eigene App (Flutter/Dart)

## Inspiration

Mechanische Grundlage: Open-Source-Projekt [Build a 3D Printed Arduino RC Drift Car](https://www.instructables.com/Build-a-3D-Printed-Arduino-RC-Drift-Car-With-Smoke/) (Instructables). Übernommen: Allrad-Antriebsstrang mit Differential, Radaufhängung, Servo-Lenkmechanik. Neu entwickelt: das gesamte Chassis (an die eigene Platine angepasst) sowie die komplette Elektronik und Steuersoftware.

## Themen-Notizen

- [[Firmware Overview|Firmware]] — ESP32-S3, ESP-IDF, FreeRTOS-Tasks, BLDC-Kommutierung, Safe Mode
- [[PCB|Hardware / PCB]] — Schaltplan, ICs, MOSFETs, erste/zweite Platinen-Version
- [[Motor & Treiber]] — Mini1410 BLDC + DRV8323S Gate-Treiber
- [[App Overview|Flutter App]] — WebSocket-Steuerung, Joystick, Gamepad, Discovery, Batterieanzeige
- [[3D-Druck]] — Chassis-Iterationen, Restmechanik aus Referenzprojekt

## Kern-Stack

| Bereich | Technologie |
|---|---|
| MCU | ESP32-S3-WROOM-1-N8 (8 MB Flash) |
| Firmware | C++ / PlatformIO / ESP-IDF |
| Motor | BLDC Mini1410 2500 kV |
| Gate-Treiber | DRV8323SRTAR (SPI, 6 PWM) |
| Leistungsstufe | 6× IPD047N03LF2 (30 V N-Ch) |
| Energie | USB-PD via STUSB4500 + MP2615 Lader + 2S LiPo |
| Schutz | S-82B2 (Primärschutz) + BQ29200 (Balancing) |
| Power-Pfad | TPS2121 Power-Mux + LMR51430 3,3 V Buck |
| App | Flutter (Android/iOS/Windows), WebSocket + UDP-Discovery |
