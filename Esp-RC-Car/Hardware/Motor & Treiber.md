---
tags: [hardware, motor, bldc, drv8323]
---

# Motor & Treiber

## BLDC Motor: Mini1410 2500kv

| Eigenschaft | Wert |
|---|---|
| Typ | Brushless DC (BLDC) |
| Modell | Mini1410 |
| KV-Zahl | 2500 kV |
| Phasen | 3 |
| Kommutierung | 6-Step Trapezoid (sensorlos) |

**KV-Bedeutung:** 2500 RPM pro Volt → bei 8V ca. 20.000 RPM

## Gate Driver: DRV8323SRTAR

| Eigenschaft | Wert |
|---|---|
| Hersteller | Texas Instruments |
| Interface | SPI |
| Phasen | 3 (6 Halbbrücken-FETs) |
| Fault-Output | nFAULT (GPIO39) |
| Versorgung | bis 60V |

## Firmware-Integration

```
rc_motor_setup()
  → DRV8323.begin()     ← SPI init, Driver aufwecken
  → LEDC attach         ← 6 PWM-Kanäle (INH/INL × 3)

rc_motor_loop()
  → apply_phase(step)   ← 6-Step Kommutierung
  → Ramp-Up Logik

rc_motor_fault_check()
  → drv.hasFault()
  → drv.readFault1/2()
  → drv.clearFaults()
```

→ [[Firmware/rc_motor]] für Software-Details  
→ [[Firmware/drv8323]] für SPI-Treiber  
→ [[PCB]] für Schaltplan

## 3D-Druck Chassis

- Datei: `STL/Chassi V8.stl`
- Format: STL + OBJ + MTL
- Version: V8

Der Motor wird in das Chassis eingepresst/verschraubt.
