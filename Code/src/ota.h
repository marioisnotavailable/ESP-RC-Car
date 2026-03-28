// ota.h — Custom GitHub Releases OTA (no token, no manifest)
// Flow: GitHub API → tag_name → compare → download .bin/.littlefs.bin → flash → reboot
#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>

#define FOTA_API_URL "https://api.github.com/repos/marioisnotavailable/ESP-RC-Car/releases/latest"
#define FOTA_FW_URL  "https://github.com/marioisnotavailable/ESP-RC-Car/releases/latest/download/esp-rc-car.bin"
#define FOTA_FS_URL  "https://github.com/marioisnotavailable/ESP-RC-Car/releases/latest/download/esp-rc-car-littlefs.bin"

// ---- Semantic version comparison ----
struct SemVer { int major, minor, patch; };

static SemVer fotaParseVersion(const char* v) {
    SemVer s = {0, 0, 0};
    if (!v) return s;
    if (*v == 'v') v++;
    sscanf(v, "%d.%d.%d", &s.major, &s.minor, &s.patch);
    return s;
}

static bool fotaIsNewer(const char* remote, const char* local) {
    SemVer r = fotaParseVersion(remote);
    SemVer l = fotaParseVersion(local);
    if (r.major != l.major) return r.major > l.major;
    if (r.minor != l.minor) return r.minor > l.minor;
    return r.patch > l.patch;
}

// ---- Flash one binary from URL into `type` partition ----
static bool fotaFlashURL(const char* url, int type) {
    WiFiClientSecure client;
    client.setInsecure(); // No CA pinning — acceptable for RC-Car OTA
    client.setTimeout(30);

    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setTimeout(30000);
    if (!http.begin(client, url)) {
        Serial.printf("[OTA] http.begin failed: %s\n", url);
        return false;
    }
    http.addHeader("User-Agent", "ESP32-OTA/1.0");

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[OTA] HTTP %d for %s\n", code, url);
        http.end();
        return false;
    }

    int totalSize = http.getSize(); // -1 if unknown (chunked)
    Serial.printf("[OTA] Download size: %d bytes\n", totalSize);

    if (!Update.begin(totalSize > 0 ? (size_t)totalSize : UPDATE_SIZE_UNKNOWN, type)) {
        Serial.printf("[OTA] Update.begin failed: %s\n", Update.errorString());
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    int written = 0;
    uint32_t deadline = millis() + 60000UL; // 60s max per file

    while (http.connected() && (totalSize < 0 || written < totalSize)) {
        if (millis() > deadline) {
            Serial.println("[OTA] Timeout while downloading");
            http.end();
            Update.abort();
            return false;
        }
        int avail = stream->available();
        if (avail > 0) {
            int n = stream->readBytes(buf, min(avail, (int)sizeof(buf)));
            if (n > 0) {
                size_t w = Update.write(buf, n);
                if (w != (size_t)n) {
                    Serial.printf("[OTA] Write mismatch %d != %d: %s\n", (int)w, n, Update.errorString());
                    http.end();
                    Update.abort();
                    return false;
                }
                written += n;
            }
        } else {
            delay(1);
        }
    }

    http.end();

    if (!Update.end(true)) {
        Serial.printf("[OTA] Update.end failed: %s\n", Update.errorString());
        return false;
    }

    Serial.printf("[OTA] Flashed %d bytes OK\n", written);
    return true;
}

// ---- Main entry point — call from loop() ----
// Returns false if no update / error. On success, reboots and never returns.
static bool fotaCheckAndUpdate(const char* currentVersion) {
    // 1. Fetch latest release metadata from GitHub API
    WiFiClientSecure apiClient;
    apiClient.setInsecure();
    apiClient.setTimeout(10);

    HTTPClient apiHttp;
    apiHttp.setTimeout(10000);
    if (!apiHttp.begin(apiClient, FOTA_API_URL)) {
        Serial.println("[FOTA] API http.begin failed");
        return false;
    }
    apiHttp.addHeader("User-Agent", "ESP32-OTA/1.0");
    apiHttp.addHeader("Accept", "application/vnd.github+json");

    int code = apiHttp.GET();
    if (code != 200) {
        Serial.printf("[FOTA] API HTTP %d\n", code);
        apiHttp.end();
        return false;
    }

    // 2. Parse only tag_name (filter saves heap)
    JsonDocument filter;
    filter["tag_name"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, apiHttp.getStream(),
                                               DeserializationOption::Filter(filter));
    apiHttp.end();

    if (err) {
        Serial.printf("[FOTA] JSON error: %s\n", err.c_str());
        return false;
    }

    const char* tag = doc["tag_name"];
    if (!tag || strlen(tag) == 0) {
        Serial.println("[FOTA] tag_name missing");
        return false;
    }

    Serial.printf("[FOTA] Remote: %s | Local: %s\n", tag, currentVersion);

    // 3. Version check
    if (!fotaIsNewer(tag, currentVersion)) {
        Serial.println("[FOTA] Firmware is up to date");
        return false;
    }

    Serial.printf("[FOTA] Updating %s -> %s\n", currentVersion, tag);

    // 4. Flash LittleFS partition first
    Serial.println("[FOTA] Flashing LittleFS...");
    if (!fotaFlashURL(FOTA_FS_URL, U_SPIFFS)) {
        Serial.println("[FOTA] LittleFS flash FAILED — aborting");
        return false;
    }
    Serial.println("[FOTA] LittleFS OK");

    // 5. Flash firmware
    Serial.println("[FOTA] Flashing firmware...");
    if (!fotaFlashURL(FOTA_FW_URL, U_FLASH)) {
        Serial.println("[FOTA] Firmware flash FAILED");
        return false;
    }
    Serial.println("[FOTA] Firmware OK — restarting");

    delay(500);
    ESP.restart();
    return true; // unreachable
}
