#include "rc_ota.h"
#include "rc_settings.h"
#include "rc_serial.h"
#include "rc_console.h"

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
    console.printf("[OTA] http.begin failed: %s\n", url);
    return false;
  }
  http.addHeader("User-Agent", "ESP32-OTA/1.0");

  int code = http.GET();
  if (code != 200) {
    console.printf("[OTA] HTTP %d for %s\n", code, url);
    http.end();
    return false;
  }

  int totalSize = http.getSize();
  console.printf("[OTA] Download size: %d bytes\n", totalSize);

  if (!Update.begin(totalSize > 0 ? (size_t)totalSize : UPDATE_SIZE_UNKNOWN, type)) {
    console.printf("[OTA] Update.begin failed: %s\n", Update.errorString());
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  int written = 0;
  uint32_t deadline = millis() + 60000UL;

  while (http.connected() && (totalSize < 0 || written < totalSize)) {
    if (millis() > deadline) {
      console.println("[OTA] Timeout while downloading");
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
          console.printf("[OTA] Write mismatch %d != %d: %s\n", (int)w, n, Update.errorString());
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
    console.printf("[OTA] Update.end failed: %s\n", Update.errorString());
    return false;
  }

  console.printf("[OTA] Flashed %d bytes OK\n", written);
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
    console.println("[FOTA] API http.begin failed");
    return false;
  }
  apiHttp.addHeader("User-Agent", "ESP32-OTA/1.0");
  apiHttp.addHeader("Accept", "application/vnd.github+json");

  int code = apiHttp.GET();
  if (code != 200) {
    console.printf("[FOTA] API HTTP %d\n", code);
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
    console.printf("[FOTA] JSON error: %s\n", err.c_str());
    return false;
  }

  const char* tag = doc["tag_name"];
  if (!tag || strlen(tag) == 0) {
    console.println("[FOTA] tag_name missing");
    return false;
  }

  console.printf("[FOTA] Remote: %s | Local: %s\n", tag, FOTA_CURRENT_VERSION);

  if (!fotaIsNewer(tag, FOTA_CURRENT_VERSION)) {
    console.println("[FOTA] Firmware is up to date");
    return false;
  }

  console.printf("[FOTA] Updating %s -> %s\n", FOTA_CURRENT_VERSION, tag);

  console.println("[FOTA] Flashing LittleFS...");
  if (!fotaFlashURL(FOTA_FS_URL, U_SPIFFS)) {
    console.println("[FOTA] LittleFS flash FAILED — aborting");
    return false;
  }
  console.println("[FOTA] LittleFS OK");

  console.println("[FOTA] Flashing firmware...");
  if (!fotaFlashURL(FOTA_FW_URL, U_FLASH)) {
    console.println("[FOTA] Firmware flash FAILED");
    return false;
  }
  console.println("[FOTA] Firmware OK — restarting");

  delay(500);
  ESP.restart();
  return true;
}

// ---- ArduinoOTA setup ----
static void setupArduinoOTA() {
  ArduinoOTA.setHostname("esp-rc-car");
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);
#endif

  ArduinoOTA.onStart([]() {
    const char* type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
    console.printf("[OTA] ArduinoOTA start: %s\n", type);
  });
  ArduinoOTA.onEnd([]() {
    console.println("\n[OTA] ArduinoOTA done — restarting");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    console.printf("[OTA] Progress: %u%%\r", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    const char* msg = "Unknown";
    if (error == OTA_AUTH_ERROR)    msg = "Auth Failed";
    if (error == OTA_BEGIN_ERROR)   msg = "Begin Failed";
    if (error == OTA_CONNECT_ERROR) msg = "Connect Failed";
    if (error == OTA_RECEIVE_ERROR) msg = "Receive Failed";
    if (error == OTA_END_ERROR)     msg = "End Failed";
    console.printf("[OTA] Error: %s\n", msg);
  });

  ArduinoOTA.begin();
  console.println("[OTA] ArduinoOTA ready");
}

// ---- Public API ----
void rc_boot_log() {
  delay(1000);
  console.println();
  console.println("========================================");
  console.println("       ESP-RC-Car Firmware");
  console.printf("       Version: %s\n", FOTA_CURRENT_VERSION);
  console.println("========================================");
  console.println();
  console.println("Serial Commands (type 'help' for details):");
  console.println("  status | settings | reboot | ota | portal | panel");
  console.println("  wifi | scan | drv | motor off/a/b");
  console.println("  log off/on/adc/drv/fota/warn/ws/net/http/servo");
  console.println();
  console.println("[BOOT] Initializing DRV8323, Battery Monitor, WiFi & Control...");
}

void rc_ota_setup(bool connected) {
  if (connected) {
    setupArduinoOTA();
    if (settings.otaEnabled) {
      nextFotaCheckMs = millis() + 15000UL;
      console.println("[FOTA] OTA check scheduled in 15s");
    } else {
      console.println("[FOTA] Auto update disabled");
    }
  } else if (!settings.otaEnabled) {
    console.println("[FOTA] Auto update disabled");
  }
}

void rc_ota_loop() {
  // ArduinoOTA always runs when WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }

  if (!settings.otaEnabled) return;
  if (WiFi.status() != WL_CONNECTED) {
    if (logFlags.fota) {
      static uint32_t lastWarn = 0;
      if (millis() - lastWarn > 30000) {
        console.println("[FOTA] Skipped — WiFi not connected");
        lastWarn = millis();
      }
    }
    return;
  }
  if (!(WiFi.getMode() & WIFI_MODE_STA)) return;

  uint32_t now = millis();
  if (now >= nextFotaCheckMs) {
    nextFotaCheckMs = now + fotaCheckIntervalMs;
    if (logFlags.fota) console.printf("[FOTA] Checking for update... (next in %lus)\n",
      fotaCheckIntervalMs / 1000);
    fotaCheckAndUpdate();
  }
}
