---
tags: [hardware, 3d-druck, chassis, esp-rc-car]
---

# 3D-Druck

## Eigenanteil: Chassis

Das Chassis ist der einzige mechanische Teil, der für das [[ESP-RC-Car]] **vollständig neu konstruiert** wurde — angepasst an die Form, Befestigungspunkte und Höhenlage der eigenen [[PCB]] sowie die Motor- und Akku-Position.

- Material: PLA
- Iterationen V2 – V18, bis Passgenauigkeit für PCB-Einschub, Motorhalterung und 2S-LiPo erreicht war
- Datei im Repo: `STL/Chassi V8.stl` (+ OBJ/MTL für Vorschau)
- Vorgehen: nach jedem Druck wurden Passungenauigkeiten an Motorhalterung und PCB-Aufnahme korrigiert, bis eine stabile Endversion entstand

## Vom Referenzprojekt übernommen

Aus dem Open-Source-Projekt [Build a 3D Printed Arduino RC Drift Car](https://www.instructables.com/Build-a-3D-Printed-Arduino-RC-Drift-Car-With-Smoke/) wurden unverändert übernommen:

- Allrad-Antriebsstrang inkl. **Differential**
- Radaufhängung
- Servo-gesteuerte Lenkmechanik

→ [[PCB]] für die Aufnahme im Chassis
→ [[Motor & Treiber]] für die Motorbefestigung
→ [[ESP-RC-Car]] für Projektkontext
