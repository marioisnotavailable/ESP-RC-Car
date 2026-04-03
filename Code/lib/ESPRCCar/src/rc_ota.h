// rc_ota.h — OTA update management + GitHub Releases flashing + ArduinoOTA
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>

#ifndef FOTA_CURRENT_VERSION
#define FOTA_CURRENT_VERSION "v0.0.0"
#endif

#define FOTA_API_URL "https://api.github.com/repos/marioisnotavailable/ESP-RC-Car/releases/latest"
#define FOTA_FW_URL  "https://github.com/marioisnotavailable/ESP-RC-Car/releases/latest/download/esp-rc-car.bin"
#define FOTA_FS_URL  "https://github.com/marioisnotavailable/ESP-RC-Car/releases/latest/download/esp-rc-car-littlefs.bin"

// Print firmware version (call early in setup)
void rc_boot_log();

// Call after WiFi connect to schedule first check
void rc_ota_setup(bool connected);

// Call in loop() — handles periodic checks automatically
void rc_ota_loop();
