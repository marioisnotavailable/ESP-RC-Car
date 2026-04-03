#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

// Default OTA check interval
#define FOTA_CHECK_DEFAULT_MS 300000UL

struct DeviceSettings {
  // OTA
  bool     otaEnabled;
  uint32_t otaIntervalMs;
  // Network
  uint8_t  wifiTxPower;       // 0=low, 1=med, 2=high, 3=max
  uint16_t failsafeMs;
  uint32_t beaconIntervalMs;
  char     apPrefix[32];
  bool     alwaysStartPanel;
  // Steering
  bool     steerInvert;
  float    steerGain;         // 0.3 .. 1.5
  uint16_t steerDeadzone;     // 0 .. 200
  float    steerFilter;       // 0.5 .. 0.95
  // Battery
  float    battWarnV;
  float    battOffV;
  // Motor
  uint8_t  maxThrottlePct;    // 10..100
  // ADC calibration
  float    adcCorrFactor;
};

extern DeviceSettings settings;
extern uint32_t fotaCheckIntervalMs;

void rc_settings_load();
void rc_settings_save();
void rc_apply_wifi_tx_power();
