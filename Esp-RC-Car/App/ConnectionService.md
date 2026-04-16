---
tags: [app, flutter, websocket, udp]
---

# ConnectionService

`ChangeNotifier` — Verwaltet WebSocket-Verbindung und UDP-Discovery zur ESP32.

## Verantwortlichkeiten

- UDP Broadcast senden → ESP32 IP-Adresse finden
- WebSocket-Verbindung aufbauen und halten
- Verbindungsstatus an UI weitergeben
- Batterie-Daten empfangen und weitergeben

## Verbindungsflow

```
1. UDP Broadcast → Port 49352: "ESP_RC_DISCOVER"
2. ESP32 antwortet: "ESP_RC_HERE [IP]"
3. WebSocket verbinden: ws://[IP]:81
4. Befehle senden / Batterie empfangen
```

## Kommunikationsprotokoll

### Senden (App → ESP32)

```json
{"throttle": 500, "steer": -200, "flags": 0}
```

Wertebereich: -1000 .. +1000

### Empfangen (ESP32 → App)

```json
{"batt": 85}
```

## Gegenseite auf ESP32

→ [[Firmware/rc_network]] (UDP Discovery)  
→ [[Firmware/rc_websocket]] (WebSocket Server)
