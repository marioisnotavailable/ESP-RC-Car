#include "rc_settings.h"

static Preferences prefsSettings;

DeviceSettings settings = {
  true, FOTA_CHECK_DEFAULT_MS,                       // OTA
  3, 400, 2000, "ESP-RC-Car-Setup-",                 // Network
  false, 1.0f, 30, 0.85f,                            // Steering
  7.9f, 7.4f,                                        // Battery
  100,                                                // Motor
  1.0f                                                // ADC calibration
};

uint32_t fotaCheckIntervalMs = FOTA_CHECK_DEFAULT_MS;

void rc_settings_load() {
  prefsSettings.begin("cfg", false);
  settings.otaEnabled      = prefsSettings.getBool("otaOn", true);
  settings.otaIntervalMs   = prefsSettings.getULong("otaInt", FOTA_CHECK_DEFAULT_MS);
  settings.wifiTxPower     = prefsSettings.getUChar("txPow", 3);
  settings.failsafeMs      = prefsSettings.getUShort("failMs", 400);
  settings.beaconIntervalMs= prefsSettings.getULong("beacInt", 2000);
  String ap = prefsSettings.getString("apPfx", "ESP-RC-Car-Setup-");
  strncpy(settings.apPrefix, ap.c_str(), sizeof(settings.apPrefix)-1);
  settings.apPrefix[sizeof(settings.apPrefix)-1] = '\0';
  settings.steerInvert     = prefsSettings.getBool("stInv", false);
  settings.steerGain       = prefsSettings.getFloat("stGain", 1.0f);
  settings.steerDeadzone   = prefsSettings.getUShort("stDz", 30);
  settings.steerFilter     = prefsSettings.getFloat("stFilt", 0.85f);
  settings.battWarnV       = prefsSettings.getFloat("bWarn", 7.9f);
  settings.battOffV        = prefsSettings.getFloat("bOff", 7.4f);
  settings.maxThrottlePct  = prefsSettings.getUChar("maxThr", 100);
  settings.adcCorrFactor   = prefsSettings.getFloat("adcCorr", 1.0f);
  prefsSettings.end();
  fotaCheckIntervalMs = settings.otaIntervalMs;
}

void rc_settings_save() {
  prefsSettings.begin("cfg", false);
  prefsSettings.putBool("otaOn", settings.otaEnabled);
  prefsSettings.putULong("otaInt", settings.otaIntervalMs);
  prefsSettings.putUChar("txPow", settings.wifiTxPower);
  prefsSettings.putUShort("failMs", settings.failsafeMs);
  prefsSettings.putULong("beacInt", settings.beaconIntervalMs);
  prefsSettings.putString("apPfx", settings.apPrefix);
  prefsSettings.putBool("stInv", settings.steerInvert);
  prefsSettings.putFloat("stGain", settings.steerGain);
  prefsSettings.putUShort("stDz", settings.steerDeadzone);
  prefsSettings.putFloat("stFilt", settings.steerFilter);
  prefsSettings.putFloat("bWarn", settings.battWarnV);
  prefsSettings.putFloat("bOff", settings.battOffV);
  prefsSettings.putUChar("maxThr", settings.maxThrottlePct);
  prefsSettings.putFloat("adcCorr", settings.adcCorrFactor);
  prefsSettings.end();
  fotaCheckIntervalMs = settings.otaIntervalMs;
}

void rc_apply_wifi_tx_power() {
  switch (settings.wifiTxPower) {
    case 0: WiFi.setTxPower(WIFI_POWER_8_5dBm);  break;
    case 1: WiFi.setTxPower(WIFI_POWER_15dBm);    break;
    case 2: WiFi.setTxPower(WIFI_POWER_17dBm);    break;
    default: WiFi.setTxPower(WIFI_POWER_19_5dBm); break;
  }
}
