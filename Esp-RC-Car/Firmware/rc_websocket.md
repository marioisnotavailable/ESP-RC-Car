---
tags: [firmware, websocket, espidf, http_server]
---

# rc_websocket — WebSocket Server (ESP-IDF)

Teil von `comms_task` → `components/rc_comms/rc_comms.c`

WebSocket-Server via **esp_http_server** auf **Port 80**.

## API

```c
httpd_uri_t ws_uri = {
    .uri          = "/ws",
    .method       = HTTP_GET,
    .handler      = ws_handler,
    .is_websocket = true,
};
```

## Empfangenes JSON (App → ESP32)

```json
{"throttle": -1000..1000, "steer": -1000..1000}
```

→ `xQueueOverwrite(cmd_queue, &cmd)`

## Gesendetes JSON (ESP32 → App)

```json
{"batt": 0..100}
```

Gesendet jede Sekunde via `broadcast_battery()`.

## Failsafe

500ms ohne Kommando → `xQueueOverwrite(cmd_queue, {0, 0, 0})`

→ Gegenseite: [[App/ConnectionService]]
