---
tags: [firmware, wifi, network, udp, espidf]
---

# rc_network — WiFi + UDP Discovery (ESP-IDF)

Teil von `comms_task` → `components/rc_comms/rc_comms.c`

## ESP-IDF APIs

| Funktion | ESP-IDF API |
|---|---|
| WiFi initialisieren | `esp_wifi_init` + `esp_netif_create_default_wifi_sta` |
| Event Handler | `esp_event_handler_register` |
| Verbinden | `esp_wifi_set_mode + set_config + start + connect` |
| IP warten | `xEventGroupWaitBits(rc_events, WIFI_CONNECTED_BIT)` |
| UDP Discovery | `lwip/sockets.h` — `socket + bind + recvfrom + sendto` |

## WiFi Credentials

Gespeichert in NVS namespace `"wifi"`:
- Key `"ssid0"` → SSID
- Key `"pass0"` → Passwort

## UDP Discovery

| Parameter | Wert |
|---|---|
| Port | 49352 |
| Query | `ESP_RC_DISCOVER` |
| Response | `ESP_RC_HERE [IP]` |

Läuft als eigener Sub-Task `udp_discovery_task` (Prio 4).

## Events

| Event | Auslöser |
|---|---|
| `WIFI_CONNECTED_BIT` gesetzt | IP erhalten |
| `WIFI_CONNECTED_BIT` gelöscht | Verbindung verloren → reconnect |

## Timeout

30 Sekunden für WiFi-Verbindung. Danach ohne WiFi weiter (nur WS-Server lokal erreichbar).

→ Gegenseite App: [[App/ConnectionService]]
