# LittleFS Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** SPIFFS-Partition auf LittleFS umstellen und statische Web-Assets (HTML/JS/CSS) über den bestehenden HTTP-Server in `rc_comms` ausliefern.

**Architecture:** LittleFS wird in `rc_common_init()` gemountet (Mountpoint `/littlefs`). Der bestehende `httpd`-Server in `rc_comms` bekommt einen catch-all Handler `/*`, der Dateien aus `/littlefs` streamt. WebSocket `/ws` bleibt unverändert.

**Tech Stack:** ESP-IDF, `esp_littlefs` (IDF-Komponente), `esp_http_server`, VFS

---

## Dateiübersicht

| Datei | Aktion |
|---|---|
| `Code/partitions.csv` | Modify: SubType `spiffs` → `littlefs` |
| `Code/sdkconfig.defaults` | Modify: LittleFS-Konfiguration hinzufügen |
| `Code/components/rc_common/CMakeLists.txt` | Modify: `esp_littlefs` zu REQUIRES |
| `Code/components/rc_common/rc_common.c` | Modify: LittleFS-Mount in `rc_common_init()` |
| `Code/components/rc_comms/rc_comms.c` | Modify: Static file handler + Content-Type |
| `Code/data/index.html` | Create: Placeholder für Flash-Test |

---

## Task 1: Partition Table — SubType ändern

**Files:**
- Modify: `Code/partitions.csv:7`

- [ ] **Schritt 1: SubType ersetzen**

`Code/partitions.csv` Zeile 7 ändern von:
```
spiffs,     data, spiffs,  0xC10000,  0x3E0000,
```
zu:
```
littlefs,   data, littlefs, 0xC10000,  0x3E0000,
```

> Der Partitionsname bleibt `littlefs` — `partition_label` im Code muss dazu passen (wird in Task 3 gesetzt).

- [ ] **Schritt 2: Build prüfen**

```bash
cd Code
idf.py build 2>&1 | tail -20
```

Erwartet: Build erfolgreich, keine Fehler bzgl. Partition Table.

- [ ] **Schritt 3: Commit**

```bash
git add Code/partitions.csv
git commit -m "feat(fs): change partition subtype from spiffs to littlefs"
```

---

## Task 2: sdkconfig.defaults — LittleFS-Optionen

**Files:**
- Modify: `Code/sdkconfig.defaults`

- [ ] **Schritt 1: LittleFS-Konfiguration anhängen**

Folgende Zeilen am Ende von `Code/sdkconfig.defaults` ergänzen:

```
CONFIG_LITTLEFS_MAX_PARTITIONS=1
CONFIG_LITTLEFS_PAGE_SIZE=256
CONFIG_LITTLEFS_OBJ_NAME_LEN=64
CONFIG_LITTLEFS_READ_SIZE=128
CONFIG_LITTLEFS_WRITE_SIZE=128
CONFIG_LITTLEFS_USE_MTIME=n
```

> `MTIME=n` spart RAM — Zeitstempel werden für den Webserver nicht gebraucht.

- [ ] **Schritt 2: Build prüfen**

```bash
cd Code
idf.py build 2>&1 | tail -20
```

Erwartet: Build erfolgreich.

- [ ] **Schritt 3: Commit**

```bash
git add Code/sdkconfig.defaults
git commit -m "feat(fs): add LittleFS sdkconfig defaults"
```

---

## Task 3: rc_common — esp_littlefs Dependency + Mount

**Files:**
- Modify: `Code/components/rc_common/CMakeLists.txt:4`
- Modify: `Code/components/rc_common/rc_common.c`

- [ ] **Schritt 1: Dependency hinzufügen**

`Code/components/rc_common/CMakeLists.txt` Zeile 4 ändern von:
```cmake
    REQUIRES freertos driver
```
zu:
```cmake
    REQUIRES freertos driver esp_littlefs
```

- [ ] **Schritt 2: Mount-Code in rc_common_init() einfügen**

`Code/components/rc_common/rc_common.c` vollständig ersetzen mit:

```c
#include "rc_common.h"
#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG = "common";

QueueHandle_t       cmd_queue;
QueueHandle_t       batt_queue;
EventGroupHandle_t  rc_events;

void rc_common_init(void) {
    cmd_queue  = xQueueCreate(1, sizeof(Cmd));
    batt_queue = xQueueCreate(1, sizeof(int));
    rc_events  = xEventGroupCreate();

    configASSERT(cmd_queue);
    configASSERT(batt_queue);
    configASSERT(rc_events);

    esp_vfs_littlefs_conf_t lfs_conf = {
        .base_path              = "/littlefs",
        .partition_label        = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount             = false,
    };
    esp_err_t ret = esp_vfs_littlefs_register(&lfs_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(ret));
    } else {
        size_t total = 0, used = 0;
        esp_littlefs_info("littlefs", &total, &used);
        ESP_LOGI(TAG, "LittleFS mounted: %u KB total, %u KB used",
                 (unsigned)(total / 1024), (unsigned)(used / 1024));
    }
}
```

- [ ] **Schritt 3: Build prüfen**

```bash
cd Code
idf.py build 2>&1 | tail -30
```

Erwartet: Build erfolgreich, keine undefinierten Symbole.

- [ ] **Schritt 4: Commit**

```bash
git add Code/components/rc_common/CMakeLists.txt \
        Code/components/rc_common/rc_common.c
git commit -m "feat(fs): mount LittleFS in rc_common_init"
```

---

## Task 4: rc_comms — Static File Handler

**Files:**
- Modify: `Code/components/rc_comms/rc_comms.c`

Der neue Handler wird **nach** dem `/ws`-Handler registriert. `httpd` prüft Handler in Registrierungsreihenfolge — `/ws` greift zuerst, `/*` fängt alles andere ab.

- [ ] **Schritt 1: Helper-Funktion `content_type_for_path` einfügen**

Nach dem Block `#include ... <string.h>` am Anfang von `rc_comms.c` folgende Funktion einfügen (vor `wifi_event_handler`):

```c
static const char *content_type_for_path(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext)                          return "application/octet-stream";
    if (strcmp(ext, ".html") == 0)     return "text/html";
    if (strcmp(ext, ".js")   == 0)     return "application/javascript";
    if (strcmp(ext, ".css")  == 0)     return "text/css";
    if (strcmp(ext, ".ico")  == 0)     return "image/x-icon";
    if (strcmp(ext, ".json") == 0)     return "application/json";
    if (strcmp(ext, ".png")  == 0)     return "image/png";
    return "application/octet-stream";
}
```

- [ ] **Schritt 2: Static File Handler einfügen**

Direkt nach `content_type_for_path` folgende Funktion einfügen:

```c
static esp_err_t static_file_handler(httpd_req_t *req)
{
    /* Build filesystem path: /littlefs + uri, or /littlefs/index.html for "/" */
    char filepath[128];
    const char *uri = req->uri;
    if (strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }
    snprintf(filepath, sizeof(filepath), "/littlefs%s", uri);

    FILE *f = fopen(filepath, "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type_for_path(filepath));

    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, (ssize_t)n) != ESP_OK) {
            fclose(f);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0); /* terminate chunked response */
    return ESP_OK;
}
```

- [ ] **Schritt 3: Handler in `start_ws_server()` registrieren**

In `start_ws_server()`, nach `httpd_register_uri_handler(server, &ws_uri)` einfügen:

```c
    static const httpd_uri_t file_uri = {
        .uri      = "/*",
        .method   = HTTP_GET,
        .handler  = static_file_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &file_uri);
    ESP_LOGI(TAG, "Static file handler registered");
```

Außerdem `<stdio.h>` zu den Includes hinzufügen (für `fopen`/`fread`/`fclose`) — prüfen ob bereits vorhanden, sonst oben ergänzen:

```c
#include <stdio.h>
```

- [ ] **Schritt 4: Build prüfen**

```bash
cd Code
idf.py build 2>&1 | tail -30
```

Erwartet: Build erfolgreich.

- [ ] **Schritt 5: Commit**

```bash
git add Code/components/rc_comms/rc_comms.c
git commit -m "feat(comms): add LittleFS static file handler to httpd"
```

---

## Task 5: Placeholder-Webseite + Flash-Test

**Files:**
- Create: `Code/data/index.html`

ESP-IDF flasht Dateien aus dem `data/`-Verzeichnis automatisch auf die LittleFS-Partition, wenn `littlefs` als Partition-Tool konfiguriert ist. Dazu wird `CMakeLists.txt` (root) angepasst.

- [ ] **Schritt 1: data/-Verzeichnis und Placeholder erstellen**

`Code/data/index.html` erstellen:

```html
<!DOCTYPE html>
<html lang="de">
<head><meta charset="UTF-8"><title>ESP-RC-Car</title></head>
<body>
  <h1>ESP-RC-Car</h1>
  <p>LittleFS OK</p>
</body>
</html>
```

- [ ] **Schritt 2: LittleFS-Image in Root-CMakeLists.txt registrieren**

`Code/CMakeLists.txt` lesen, dann nach `project(...)` folgende Zeile einfügen:

```cmake
littlefs_create_partition_image(littlefs data FLASH_IN_PROJECT)
```

> `littlefs` ist der Partitionsname aus `partitions.csv`. `data` ist das Quellverzeichnis.

- [ ] **Schritt 3: Build + Flash**

```bash
cd Code
idf.py build flash monitor 2>&1 | grep -E "(LittleFS|littlefs|static|I \()"
```

Erwartet im Log:
```
I (xxx) common: LittleFS mounted: NNNN KB total, N KB used
I (xxx) comms: Static file handler registered
```

- [ ] **Schritt 4: Browser-Test**

ESP-IP im Browser aufrufen (z.B. `http://192.168.x.x/`).
Erwartet: `ESP-RC-Car — LittleFS OK` im Browser.

- [ ] **Schritt 5: Commit**

```bash
git add Code/data/index.html Code/CMakeLists.txt
git commit -m "feat(fs): add placeholder web UI and LittleFS flash image"
```
