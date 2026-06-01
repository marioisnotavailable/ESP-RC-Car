---
tags: [debug, boot, psram, spiram, fix]
---

# Boot Error: PSRAM Abort

## Symptom

```
E (563) quad_psram: PSRAM ID read error: 0x00ffffff, PSRAM chip not found
E (566) cpu_start: Failed to init external RAM!
abort() was called at PC 0x40376cdf
```

Zusätzlich: Factory-Partition corrupt (`invalid segment length 0x5f6f6970` = "pio" ASCII → altes PlatformIO-Binary). Boot fällt korrekt auf `app0` (OTA) zurück, aber crasht dort wegen PSRAM.

## Ursache

`sdkconfig.defaults` hatte `CONFIG_SPIRAM_MODE_QUAD=y` (ESP-IDF Default).
Board ist **ESP32-S3 N16R8** → PSRAM ist **OPI** (Octal SPI), nicht Quad.
Falscher Modus → ID-Read liefert `0x00ffffff` → `abort()`.

| Config | Alt (falsch) | Neu (fix) |
|---|---|---|
| `CONFIG_SPIRAM_SUPPORT=y` | ✅ (alter Name) | entfernt |
| `CONFIG_ESP32S3_SPIRAM_SUPPORT=y` | ✅ (alter Name) | entfernt |
| `CONFIG_SPIRAM=y` | — | ✅ (neuer Name) |
| `CONFIG_SPIRAM_MODE_QUAD=y` | ✅ (falsch) | — |
| `CONFIG_SPIRAM_MODE_OCT=y` | — | ✅ (fix) |

## Fix

**`sdkconfig.defaults`** geändert:
```diff
-CONFIG_SPIRAM_SUPPORT=y
-CONFIG_ESP32S3_SPIRAM_SUPPORT=y
+CONFIG_SPIRAM=y
+CONFIG_SPIRAM_MODE_OCT=y
```

## Nach dem Fix

1. `sdkconfig.espidf_ota` löschen (oder `pio run -e espidf --target menuconfig`)
2. Neu bauen: `pio run -e espidf`
3. Flashen: `pio run -e espidf --target upload`

> Factory-Partition corrupt ist **unkritisch** — Boot-ROM fällt automatisch auf `app0` zurück. Kann mit `pio run -e espidf_factory --target upload` gefixt werden.
