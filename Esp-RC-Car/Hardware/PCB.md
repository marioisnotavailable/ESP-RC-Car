---
tags: [hardware, pcb, altium, schematic, esp-rc-car]
---

# PCB

Eigenentwickelte mehrlagige Platine für das [[ESP-RC-Car]], entworfen in **Altium Designer**.

## Versionen

Es existieren zwei Platinen-Versionen:

- **Erste Version** — bestellt und getestet im Dezember 2025
- **Zweite Version** — überarbeitet ab März 2026, mit korrigierter Beschaltung der unten beschriebenen Fehlerstellen sowie zusätzlichen Entkopplungskondensatoren, Jumper-Optionen und verbessertem thermischen Layout des LMR51430

### Fehler der ersten Version → Fixes in der zweiten Version

| Bauteil | Problem | Lösung |
|---|---|---|
| TPS2121 (IC4) | Pull-Beschaltung der Eingangspins falsch dimensioniert → undefiniertes Umschaltverhalten | Pull-Widerstände neu dimensioniert |
| STUSB4500 (IC6) | CC-Leitungen falsch terminiert → USB-PD-Aushandlung schlug fehl | CC-Pull-Down-Widerstände korrigiert, dediziertes Entkopplungsnetzwerk ergänzt |
| BQ29200 (IC7) | Falscher Footprint → IC nicht lötbar | Footprint korrigiert |
| MP2615 (IC2) | Bestückungsfehler → instabiles Ladeverhalten | Bestückung korrigiert |
| DRV8323 | Fehlerhafte Signalpegel, falsche Pull-up/-down-Beschaltung | Netzliste korrigiert |

## Hauptkomponenten

| Ref | Bauteil | Funktion |
|---|---|---|
| IC1 | **ESP32-S3-WROOM-1-N8** | Haupt-MCU: WiFi, SPI, I²C, PWM |
| IC2 | **MP2615AGQ-Z** | Synchroner Buck-Boost-Ladecontroller für 2S-LiPo; `CHARGE`-Pin vom ESP32-S3 nur zum Ein-/Ausschalten des Ladevorgangs, **keine** Ladestatus-Rückmeldung |
| IC3 | **S-82B2AAA-A8T2U** (ABLIC) | Primärer 2S-Zellschutz, Über-/Unterspannungsabschaltung der externen Schutz-MOSFETs |
| IC4 | **TPS2121RUXT** | Dual-Eingang-Power-Multiplexer USB-PD-VBUS ↔ Akku → VSYS, ST-Statusleitung an ESP32-S3 |
| IC5 | **DRV8323SRTAR** | 3-Phasen-Smart-Gate-Driver, per SPI konfigurierbar, 6 PWM-Eingänge, integrierte Stromsense-Verstärker, `nFAULT` |
| IC6 | **STUSB4500QTR** | Autonomer USB-PD-Sink-Controller (bis 100 W), I²C-Konfiguration des Profils (12 V), `ALERT` an GPIO8 |
| IC7 | **BQ29200DRBT** | Nur **passives Zellbalancing** für die 2S-LiPo-Konfiguration; Kurzschluss-/Überstromschutz übernimmt IC3 |
| PS1 | **LMR51430XDDCR** | 3,3 V Buck-Konverter (3 MHz, ~95 % Wirkungsgrad), Enable an GPIO3 |
| Q5–Q10 | 6× **IPD047N03LF2SATMA1** | 30 V / 120 A N-Kanal-MOSFETs in 3-Phasen-Vollbrücke |
| J1 | 3-poliger Stecker | 2S-LiPo-Akku |
| J2 | USB4215-03-A (USB-C, 24-polig) | USB-PD-Laden + USB-2.0-Daten |

## Energiepfad

```
USB-C (J2) ─► STUSB4500 (IC6, PD-Profil 12 V) ─► VBUS
                                                  │
       VBUS ─► MP2615 (IC2, Lade-Regler) ─► 2S LiPo (J1)
                                                  │
       VBUS ─┐                                    │
              ├─► TPS2121 (IC4, Power-Mux) ─► VSYS ──► LMR51430 (PS1) ─► 3,3 V
       VBAT ─┘                                                              │
                                                                            ▼
                                                                ESP32-S3, DRV8323, ...
```

Zellschutz: IC3 (S-82B2) steuert externe Schutz-MOSFETs, IC7 (BQ29200) macht ausschließlich das Balancing.

## Batterie-Telemetrie

Die ESP32-Firmware liest die Akkuspannung direkt über einen Spannungsteiler per ADC ein (kein I²C-Fuel-Gauge). Wert wird über WebSocket an die App gepusht.

## Schaltplan & Layout

Im Repo unter `PCB/`:

| Datei | Beschreibung |
|---|---|
| `PCB/ESP-RC-Car.SchDoc` | Schaltplan |
| `PCB/ESP-RC-Car.PcbDoc` | PCB-Layout |
| `PCB/ESP-RC-Car.PrjPcb` | Altium-Projekt |

Siehe auch [[Motor & Treiber]] für die Leistungsstufe und [[Firmware Overview|Firmware]] für die Software-Anbindung.
