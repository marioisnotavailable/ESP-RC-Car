---
tags: [hardware, pcb, altium, schematic]
---

# PCB Design

Custom PCB für das ESP-RC-Car, erstellt in **Altium Designer**.

## Dateien

| Datei | Beschreibung |
|---|---|
| `PCB/ESP-RC-Car.SchDoc` | Schaltplan |
| `PCB/ESP-RC-Car.PcbDoc` | PCB Layout |
| `PCB/ESP-RC-Car.BomDoc` | Stückliste |
| `PCB/ESP-RC-Car.PrjPcb` | Altium Projekt |

## Bekannte Fehler

Dokumentiert in `Fehler PCB.txt` im Repo-Root.

## Hauptkomponenten auf dem PCB

- **ESP32-S3** — MCU
- **DRV8323SRTAR** — 3-Phasen Gate Driver für BLDC
- **STUSB4500** — USB Power Delivery Controller
- **BMS IC** — Battery Management System
- **Lade-IC** — Ladelogik für 2S LiPo
- **Balancing ICs** — Zellenbalancer

## Stromversorgung

```
USB-C (PD) → STUSB4500 → Lade-IC → 2S LiPo
                                   ↓
                              BMS + Balancer
                                   ↓
                           ESP32-S3 + DRV8323S
```

## Bestellliste

Komponenten dokumentiert in `Bestellliste.txt`.

→ [[Motor & Treiber]] für Motor-/Treiber-Details
