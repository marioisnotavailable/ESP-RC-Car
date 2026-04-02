#include "rc_ota.h"
#include "rc_settings.h"
#include "rc_serial.h"

static uint32_t nextFotaCheckMs = 0;

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

// ---- Flash one binary from URL into partition ----
static bool fotaFlashURL(const char* url, int type) {
  WiFiClientSecure client;
  client.setInsecure();
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

  int totalSize = http.getSize();
  Serial.printf("[OTA] Download size: %d bytes\n", totalSize);

  if (!Update.begin(totalSize > 0 ? (size_t)totalSize : UPDATE_SIZE_UNKNOWN, type)) {
    Serial.printf("[OTA] Update.begin failed: %s\n", Update.errorString());
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  int written = 0;
  uint32_t deadline = millis() + 60000UL;

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

// ---- Check & update from GitHub ----
static bool fotaCheckAndUpdate() {
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

  Serial.printf("[FOTA] Remote: %s | Local: %s\n", tag, FOTA_CURRENT_VERSION);

  if (!fotaIsNewer(tag, FOTA_CURRENT_VERSION)) {
    Serial.println("[FOTA] Firmware is up to date");
    return false;
  }

  Serial.printf("[FOTA] Updating %s -> %s\n", FOTA_CURRENT_VERSION, tag);

  Serial.println("[FOTA] Flashing LittleFS...");
  if (!fotaFlashURL(FOTA_FS_URL, U_SPIFFS)) {
    Serial.println("[FOTA] LittleFS flash FAILED — aborting");
    return false;
  }
  Serial.println("[FOTA] LittleFS OK");

  Serial.println("[FOTA] Flashing firmware...");
  if (!fotaFlashURL(FOTA_FW_URL, U_FLASH)) {
    Serial.println("[FOTA] Firmware flash FAILED");
    return false;
  }
  Serial.println("[FOTA] Firmware OK — restarting");

  delay(500);
  ESP.restart();
  return true;
}

// ---- Public API ----
void rc_boot_log() {
  delay(1000);
  Serial.println();
  Serial.println("========================================");
  Serial.println("       ESP-RC-Car Firmware");
  Serial.printf("       Version: %s\n", FOTA_CURRENT_VERSION);
  Serial.println("========================================");
  Serial.println();
  Serial.println("Serial Commands (type 'help' for details):");
  Serial.println("  status | settings | reboot | ota | portal");
  Serial.println("  wifi | scan | drv | motor off/a/b");
  Serial.println("  log off/on/adc/drv/fota/warn/ws/net/http/servo");
  Serial.println();
  Serial.println("[BOOT] Initializing DRV8323, Battery Monitor, WiFi & Control...");
}

void rc_ota_setup(bool connected) {
  if (connected && settings.otaEnabled) {
    nextFotaCheckMs = millis() + 15000UL;
    Serial.println("[FOTA] OTA check scheduled in 15s");
  } else if (!settings.otaEnabled) {
    Serial.println("[FOTA] Auto update disabled");
  }
}

void rc_ota_loop() {
  if (!settings.otaEnabled) return;
  if (WiFi.status() != WL_CONNECTED) {
    if (logFlags.fota) {
      static uint32_t lastWarn = 0;
      if (millis() - lastWarn > 30000) {
        Serial.println("[FOTA] Skipped — WiFi not connected");
        lastWarn = millis();
      }
    }
    return;
  }
  if (!(WiFi.getMode() & WIFI_MODE_STA)) return;

  uint32_t now = millis();
  if (now >= nextFotaCheckMs) {
    nextFotaCheckMs = now + fotaCheckIntervalMs;
    if (logFlags.fota) Serial.printf("[FOTA] Checking for update... (next in %lus)\n",
      fotaCheckIntervalMs / 1000);
    fotaCheckAndUpdate();
  }
}
