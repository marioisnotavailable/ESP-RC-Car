# PCB-Bringup: Features einzeln testen

Für das neue PCB lässt sich jedes Feature einzeln flashen. Es laufen nur die
Tasks, die per Flag aktiviert sind. Alles andere wird nicht gestartet.

## Prinzip

- **Kein `RC_TEST_*` Flag gesetzt** → normale Firmware, alles läuft.
- **Mindestens ein `RC_TEST_*` Flag gesetzt** → nur diese Features laufen.
- Flags sind kombinierbar (z.B. Motor + Servo).
- Im Testmodus ist `rc_recovery_check()` aus. Bootloops auf dem Prototyp
  lösen also keinen Safe Mode aus.
- Beim Boot loggt die Firmware, was aktiv ist:
  `ISOLATED mode: wifi=1 motor=0 servo=0 battery=0 bg=0`

Umsetzung: `src/rc_features.h` (Flags) und `src/main.c` (Task-Start).

## Verfügbare Test-Envs

| Env | Flag | Läuft | Prüft am PCB |
|---|---|---|---|
| `test_wifi` | `RC_TEST_WIFI` | `comms_task` | AP/STA, Webpanel, WebSocket, DNS, UDP-Discovery |
| `test_motor` | `RC_TEST_MOTOR` | `motor_task` | DRV8323 (SPI), 6-Step-PWM, Fault-Pin |
| `test_servo` | `RC_TEST_SERVO` | `steering_task` | Servo-PWM (`PIN_SERVO`) |
| `test_battery` | `RC_TEST_BATTERY` | `system_task` | Batterie-ADC, Low-Voltage-Cutoff |
| `test_bg` | `RC_TEST_BG` | `bg_task` | FOTA, serielle Konsole |

Alle Envs erben von `espidf_cdc` (USB-CDC, kein Reset beim Upload).

## Benutzung

```bash
cd Code

# bauen
pio run -e test_wifi

# flashen
pio run -e test_wifi -t upload

# Log ansehen
pio device monitor -b 115200
```

Danach den Serial-Log prüfen: Die `ISOLATED mode`-Zeile muss genau das Feature
zeigen, das du testen willst.

## Kombinationen

Für mehrere Features gleichzeitig einen eigenen Env in `platformio.ini`
anlegen:

```ini
[env:test_motor_servo]
extends = env:espidf_cdc
build_flags =
    -DOTA_PASSWORD=\"esprccar\"
    -DRC_TEST_MOTOR
    -DRC_TEST_SERVO
```

## Was pro Test erwarten

**test_wifi**
- Log: `AP started: ESP-RC-xxxx` (Prefix aus `ap_prefix`).
- Mit dem AP verbinden, Webpanel unter `192.168.4.1`.
- Ohne Motor und Servo gibt es keine Reaktion auf Fahrbefehle. Das ist so gewollt.

**test_motor**
- `motor_task` initialisiert den DRV8323 über SPI. Ohne Befehle von `comms`
  bleibt der Motor in Ruhe.
- Fehler im Log (SPI, Fault) deuten auf Pins/Layout hin. Bekannte PCB-Fehler:
  zu wenig GND am Gate-Treiber.
- Zum Drehen testen nutze die bestehenden Modi `STEP_TEST` (`espidf_step_test`)
  bzw. `TEST_DRIVE` in `main.c`.

**test_servo**
- PWM auf `PIN_SERVO` (`rc_common.h`). Bekannter PCB-Fehler: Servo hängt auf
  einem unbrauchbaren Pin. Pin ggf. anpassen.
- Ohne `comms` liegt kein Lenkbefehl an, Servo bleibt in Mittelstellung.

**test_battery**
- `system_task` liest die Batteriespannung (`g_vbatt`).
- **Achtung:** Liegt `g_vbatt` über 0,5 V, aber unter `batt_off_v`, geht der
  ESP nach 3 Messungen in den Deep-Sleep (nur Reset/Power-Cycle weckt ihn).
  Bekannter PCB-Fehler: Spannungsteiler hängt auf einem Pin ohne ADC.
  Dann sind die Werte Unsinn.

**test_bg**
- Serielle Konsole: `status`, `reset`, `ota`.
- FOTA-Check läuft erst, wenn WLAN verbunden ist. Ohne `RC_TEST_WIFI`
  passiert also nichts außer der Konsole.

## Zurück zur normalen Firmware

Normale Envs verwenden (`espidf`, `espidf_ota`, `espidf_factory`). Dort ist
kein `RC_TEST_*` gesetzt, es läuft alles.

## Bekanntes Problem

Mit lokalem PlatformIO (`espressif32@6.12.0`, ESP-IDF 5.1.4) scheitert der
Build schon ohne Test-Flags mit `Failed to resolve component 'esp_driver_gpio'`.
Die Components fordern `esp_driver_*` an (ab ESP-IDF 5.2). Plattform
aktualisieren (`pio pkg update`) oder die Components auf `driver` umstellen.
