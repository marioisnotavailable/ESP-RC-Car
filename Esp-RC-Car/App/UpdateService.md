---
tags: [app, flutter, update, github]
---

# UpdateService

Sucht auf **GitHub Releases** nach einer neueren App-Version und installiert sie (nur Android).

## Verantwortlichkeiten

- `releases/latest` der GitHub API abfragen (Repo `marioisnotavailable/ESP-RC-Car`)
- Installierte Version via `package_info_plus` lesen und vergleichen
- Neuere `EspRCCar.apk` herunterladen (Fortschritt 0..1)
- Android-Paket-Installer öffnen (`open_filex`)

## Ablauf

```
1. GET api.github.com/repos/.../releases/latest
2. tag_name (z.B. v0.8.0) vs. installierte Version (package_info)
3. Asset "EspRCCar.apk" → browser_download_url
4. Download → temp-Verzeichnis (path_provider)
5. OpenFilex.open(apk) → Android Installer
```

## API

- `checkForUpdate()` → `AppUpdateInfo?` (null = bereits aktuell)
- `download(url, onProgress)` → Pfad zur heruntergeladenen APK

## Abhängigkeiten

- `http` — GitHub API + Download
- `package_info_plus` — installierte Version
- `path_provider` — temp-Verzeichnis
- `open_filex` — Installer öffnen

## Voraussetzungen

- Manifest-Permission `REQUEST_INSTALL_PACKAGES`
- Alle Releases mit **demselben Release-Key** signiert (sonst Signatur-Konflikt beim Update)
- App-Version im CI aus Release-Tag (`flutter build --build-name`)

## UI

→ [[DevPanel]] — ⟳-Button löst `checkForUpdate()` aus
