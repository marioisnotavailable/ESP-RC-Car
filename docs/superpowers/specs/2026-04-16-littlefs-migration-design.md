# LittleFS Migration — Design Spec

**Date:** 2026-04-16
**Branch:** esp-idf-migration

---

## Ziel

Die bisher deklarierte (aber ungenutzte) SPIFFS-Partition wird durch LittleFS ersetzt.
LittleFS speichert die statischen Web-Assets (HTML, JS, CSS) für die Weboberfläche des RC-Cars.
Die Dateien werden über den bestehenden `httpd`-Server in `rc_comms` ausgeliefert.

---

## Architektur (Option C)

```
app_main()
  └─ rc_common_init()          ← LittleFS wird hier gemountet (/littlefs)
  └─ comms_task()
       └─ start_ws_server()    ← httpd startet, registriert /ws + catch-all
            ├─ /ws             ← WebSocket (unverändert)
            └─ /*              ← Static file handler (neu)
                                 liest aus /littlefs/<path>
```

---

## Komponenten & Änderungen

### 1. `partitions.csv`

| Name   | Type | SubType   | Offset    | Size     |
|--------|------|-----------|-----------|----------|
| spiffs | data | ~~spiffs~~ → **littlefs** | 0xC10000 | 0x3E0000 |

Nur der SubType-String ändert sich. Offset und Größe bleiben identisch.

### 2. `components/rc_common/CMakeLists.txt`

`esp_littlefs` zu `REQUIRES` hinzufügen.

### 3. `sdkconfig.defaults`

```
CONFIG_LITTLEFS_MAX_PARTITIONS=1
CONFIG_LITTLEFS_PAGE_SIZE=256
CONFIG_LITTLEFS_OBJ_NAME_LEN=64
CONFIG_LITTLEFS_READ_SIZE=128
CONFIG_LITTLEFS_WRITE_SIZE=128
```

### 4. `components/rc_common/rc_common.c` — Mount

```c
#include "esp_littlefs.h"

// in rc_common_init(), nach nvs_flash_init:
esp_vfs_littlefs_conf_t lfs_conf = {
    .base_path              = "/littlefs",
    .partition_label        = "spiffs",   // Label aus partitions.csv
    .format_if_mount_failed = true,
    .dont_mount             = false,
};
ESP_ERROR_CHECK(esp_vfs_littlefs_register(&lfs_conf));
```

> **Hinweis:** `partition_label` muss dem Namen in `partitions.csv` entsprechen (`spiffs`),
> unabhängig vom SubType. Der Name kann später bei Bedarf umbenannt werden.

### 5. `components/rc_comms/rc_comms.c` — Static File Handler

Neuer URI-Handler `/*` (catch-all), registriert **nach** `/ws`:

- Pfad `/` → öffnet `/littlefs/index.html`
- Pfad `/foo.js` → öffnet `/littlefs/foo.js`
- Content-Type wird anhand der Dateiendung gesetzt (`.html`, `.js`, `.css`, `.ico`)
- Datei nicht gefunden → 404
- Streaming per `httpd_resp_send_chunk` in 512-Byte-Blöcken

---

## Datenfluss

```
Browser GET /index.html
  → httpd catch-all handler
  → fopen("/littlefs/index.html")
  → httpd_resp_send_chunk (512 B Blöcke)
  → Browser

Browser WS /ws
  → ws_handler (unverändert)
```

---

## Flash-Layout (unverändert)

```
nvs       0x9000    0x5000
otadata   0xE000    0x2000
factory   0x10000   0x200000
app0      0x210000  0x500000
app1      0x710000  0x500000
littlefs  0xC10000  0x3E0000   ← ~4 MB für Web-Assets
coredump  0xFF0000  0x10000
```

---

## Nicht im Scope

- OTA-Update der Web-Assets (separates Feature)
- HTTPS / TLS
- Authentifizierung
- Komprimierung (gzip pre-compressed Assets)
