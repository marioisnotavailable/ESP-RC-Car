---
tags: [firmware, usb-pd, stusb4500, i2c, espidf]
---

# rc_pd — USB-C PD Sink (STUSB4500)

Component: `components/rc_pd/` · Task: `pd_task` (alle 2 s Status-Log)

## Pins

| Signal | GPIO |
|---|---|
| SDA | 16 |
| SCL | 17 |
| ALERT | 15 |
| RESET | 3 |
| VBUS ADC (Teiler) | 14 |

I2C Adresse `PD_I2C_ADDR`, 100 kHz, Legacy-Driver (`i2c_master_write_read_device`).

## Ziel-Profil

| PDO | Spannung | Strom | Rolle |
|---|---|---|---|
| 1 | 5 V | 1500 mA | Pflicht-PDO |
| 2 | 9 V | 1000 mA | Fallback |
| 3 | 12 V | 1000 mA | Prio 1 |

`DPM_PDO_NUMB = 3`. 1000 mA, damit jedes Netzteil das Profil akzeptiert.

## Was früher falsch war (gefixt, getestet)

1. **RESET-Pin nie auf 1** — `PIN_PD_RESET` high lässt VBUS abfallen / Chip falsch resetten. `rc_pd_init()` hält ihn dauerhaft auf 0.
2. **Zu kurze Wartezeit nach Power-On** — Chip lädt NVM. Jetzt 100 ms warten + bis zu 10 Versuche `DEVICE_ID` (0x25 / 0x21).
3. **PDO-Write überschrieb Config-Bits** — `rc_pd_set_pdo()` macht jetzt Read-Modify-Write (Maske `0x3FF00000` erhalten), wie `Update_PDO()` in STSW-STUSB003.
4. **PDO3 blieb NVM-Default 20 V** — bei nur 2 PDOs gewann PDO3 (höchste Prio). Jetzt alle 3 PDOs gesetzt.
5. **Soft_Reset unsauber** — jetzt: warten auf `PE_SNK_READY` (0x18), Latches (0x0B..0x16) leeren, `TX_HEADER_HIGH` + `LOW` schreiben, Command 0x26.
6. **Falscher Zeitpunkt** — Konfiguration erst im `pd_task`, wenn CC attached und (PDO3 ≠ 12 V oder erster `PE_SNK_READY`). Einmal pro Attach (`s_renegotiated_this_attach`).

## API

```c
esp_err_t rc_pd_init(void);
esp_err_t rc_pd_set_pdo(uint8_t pdo_num, uint16_t mv, uint16_t ma);
esp_err_t rc_pd_request_renegotiate(void);
esp_err_t rc_pd_get_status(rc_pd_status_t *out);
```

`rc_pd_status_t`: `cable_attached`, `attached` (RDO pos ≥ 1), `cap_mismatch` (RDO Bit 26), `object_pos`, `pe_fsm`, `op_current_ma`, `max_current_ma`, `voltage_mv` (echte ADC-Messung).

## Wichtige Register

| Reg | Name |
|---|---|
| 0x0B/0x0C | ALERT_STATUS_1 / MASK (Maske 0x1B) |
| 0x0E | PORT_STATUS_1 (Bit 0 = Attach) |
| 0x1A | PD_COMMAND_CTRL |
| 0x29 | PE_FSM (0x18 = PE_SNK_READY) |
| 0x2F | DEVICE_ID |
| 0x30/0x31/0x33 | RX_BYTE_CNT / RX_HEADER / RX_DATA_OBJ1 (Source-PDOs) |
| 0x51/0x52 | TX_HEADER LOW/HIGH |
| 0x70 | DPM_PDO_NUMB |
| 0x85 | DPM_SNK_PDO1 (+4 je PDO) |
| 0x91 | RDO_REG_STATUS |

## Debugging

Log-Zeile: `cc=… pe=0x18 contract=1 src_pdo=N mismatch=0 cur=…/… mA vbus=… mV`.
Erfolg: `pe=0x18`, `src_pdo` ≠ 1, `vbus` ≈ 12 V. `src PDOx`-Zeilen zeigen, was das Netzteil anbietet (kein 12 V → Netzteil-Limit).

Siehe auch [[Hardware/PCB]], [[rc_pins]], [[rc_battery]].
