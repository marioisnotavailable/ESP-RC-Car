#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <vector>

// WiFi timing
#define WIFI_CONNECT_TOTAL_MS  30000UL
#define WIFI_CONNECT_POLL_MS   250
#define SCAN_DWELL_MS          40

// UDP Discovery
#define DISCOVERY_PORT         49352
#define DISCOVERY_QUERY        "ESP_RC_DISCOVER"
#define DISCOVERY_RESP_PREFIX  "ESP_RC_HERE "

// Multi-Reset Detector
#define MRD_TIMEOUT_MS         8000UL
#define MRD_REQUIRED           3

struct WifiNet {
  String  ssid;
  String  pass;
  uint8_t bssid[6];
  int32_t channel;
};

struct ScanNet {
  String  ssid;
  int32_t rssi;
  uint8_t enc;
};

extern std::vector<WifiNet> savedNets;
extern std::vector<ScanNet> lastScan;
extern bool startConfigPortal;

// WiFi storage
void rc_network_load();
void rc_network_save();
bool rc_network_add(const String& ssid, const String& pass);
bool rc_network_delete(int idx);

// WiFi scanning
void rc_wifi_scan();

// WiFi connection (returns true if connected as STA)
bool rc_wifi_connect();

// Full WiFi setup: connect + print IP or start portal
bool rc_wifi_setup();

// Multi-Reset Detection
void rc_mrd_check_boot();
void rc_mrd_loop();

// UDP Discovery
void rc_udp_begin();
void rc_udp_loop();

// Helpers
IPAddress rc_current_ip();
