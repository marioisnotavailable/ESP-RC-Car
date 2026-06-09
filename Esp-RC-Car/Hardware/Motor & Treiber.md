---
tags: [hardware, motor, bldc, drv8323, esp-rc-car]
---

# Motor & Treiber

## BLDC-Motor: Mini1410 2500 kV

| Eigenschaft | Wert |
|---|---|
| Typ | Brushless DC (BLDC) |
| Modell | Mini1410 |
| KV-Zahl | 2500 kV |
| Phasen | 3 |
| Kommutierung | 6-Step-Trapezoid |

KV-Bedeutung: 2500 RPM pro Volt → bei 8,4 V (voller 2S-LiPo) bis ~21 000 RPM mechanisch idealisiert.

## Gate-Treiber: DRV8323SRTAR

| Eigenschaft | Wert |
|---|---|
| Hersteller | Texas Instruments |
| Topologie | 3-Phasen-Smart-Gate-Driver |
| Konfiguration | über dedizierten SPI-Bus vom ESP32-S3 |
| PWM-Eingänge | 6 (INH_A/B/C high-side, INL_A/B/C low-side) |
| Stromsense | integrierte Verstärker (SOA, SOB, SOC) mit drei Low-Side-Shunts |
| Fehlerausgang | `nFAULT` an GPIO39 |
| Kalibrierung | über `CAL`-Pin an GPIO28 |
| Bootstrapping | SHA/GHA, SHB/GHB, SHC/GHC |

## Leistungsstufe

6× **IPD047N03LF2SATMA1** (Infineon, 30 V, 120 A N-Kanal) als 3-Phasen-Vollbrücke. Drei Shunt-Widerstände in den Low-Side-Pfaden für die Phasenstrommessung.

## Firmware-Integration

```
rc_motor_setup()
  → DRV8323 SPI init, Driver konfigurieren
  → LEDC: 6 PWM-Kanäle (INH/INL × 3)

motor_task (Prio 10)
  → cmd_queue → throttle/steer übernehmen
  → apply_phase(step) — 6-Step-Kommutierung
  → Ramp-Up für sanften Anlauf

Fault-Pfad
  → drv8323_has_fault() / read_fault1/2() / clear_faults()
  → MOTOR_FAULT_BIT in rc_events
```

→ [[Firmware Overview]] für Gesamtarchitektur
→ [[PCB]] für die Hardware-Seite
→ [[3D-Druck]] zur Motoraufnahme im Chassis

## Messungen

Phasenmessung mit 3-Kanal-Oszilloskop zeigt versetzte PWM-Schaltflanken zwischen den drei Phasen — klassisches BLDC-Kommutierungsmuster.
