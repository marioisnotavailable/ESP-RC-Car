#include "rc_serial.h"
#include "rc_settings.h"
#include "rc_battery.h"
#include "rc_motor.h"
#include "rc_network.h"
#include "rc_websocket.h"
#include "rc_httpapi.h"
#include "rc_ota.h"
#include "rc_console.h"
#include "rc_recovery.h"

LogFlags logFlags = { true, true, true, true, true, true, true, true };

static void printHelp() {
  console.println();
  console.println("=== Serial Commands ===");
  console.println("  help        — Diese Hilfe anzeigen");
  console.println("  status      — Batterie, WiFi, Motor Status");
  console.println("  settings    — Alle Einstellungen anzeigen");
  console.println("  reboot      — ESP neustarten");
  console.println("  ota         — OTA Update jetzt pruefen");
  console.println("  portal      — Config-Portal starten");
  console.println("  panel       — Config-Panel im aktuellen WLAN starten (ohne AP)");
  console.println("  wifi        — Gespeicherte Netzwerke anzeigen");
  console.println("  scan        — WLAN-Scan durchfuehren");
  console.println("  drv         — DRV8323S Register auslesen");
  console.println("  motor off   — Motor ausschalten");
  console.println("  motor a     — Motor Phase A aktivieren");
  console.println("  motor b     — Motor Phase B aktivieren");
  console.println("  log         — Log-Gruppen Status anzeigen");
  console.println("  log adc     — [ADC] Batterie-Logs toggeln");
  console.println("  log drv     — [DRV] Motor-Logs toggeln");
  console.println("  log fota    — [FOTA] OTA-Logs toggeln");
  console.println("  log warn    — [WARN] Warnungs-Logs toggeln");
  console.println("  log ws      — [WS] WebSocket-Logs toggeln");
  console.println("  log net     — [NET] Netzwerk-Logs toggeln");
  console.println("  log http    — [HTTP] HTTP-API-Logs toggeln");
  console.println("  log servo   — [SERVO] Steering-Logs toggeln");
  console.println("  log off     — Alle Loop-Logs aus");
  console.println("  log on      — Alle Loop-Logs an");
  console.println("  recovery    — Crash-Counter zuruecksetzen + Safe Mode verlassen");
  console.println("=======================");
  console.println();
}

static void printLogStatus() {
  console.println();
  console.println("--- Log Groups ---");
  console.printf("  [ADC]   Batterie:    %s\n", logFlags.adc   ? "ON" : "OFF");
  console.printf("  [DRV]   Motor:       %s\n", logFlags.drv   ? "ON" : "OFF");
  console.printf("  [FOTA]  OTA:         %s\n", logFlags.fota  ? "ON" : "OFF");
  console.printf("  [WARN]  Warnungen:   %s\n", logFlags.warn  ? "ON" : "OFF");
  console.printf("  [WS]    WebSocket:   %s\n", logFlags.ws    ? "ON" : "OFF");
  console.printf("  [NET]   Netzwerk:    %s\n", logFlags.net   ? "ON" : "OFF");
  console.printf("  [HTTP]  HTTP-API:    %s\n", logFlags.http  ? "ON" : "OFF");
  console.printf("  [SERVO] Steering:    %s\n", logFlags.servo ? "ON" : "OFF");
  console.println("------------------");
  console.println();
}

static void printStatus() {
  console.println();
  console.println("--- Status ---");
  console.printf("  Firmware:    %s\n", FOTA_CURRENT_VERSION);
  console.printf("  Safe Mode:   %s\n", safeMode ? "JA" : "Nein");
  console.printf("  Batterie:    %.2fV (%d%%)\n", vBatt_float_last, batteryPercent);
  console.printf("  WiFi Mode:   %s\n",
    (WiFi.getMode() & WIFI_MODE_STA) ? "STA" :
    (WiFi.getMode() & WIFI_MODE_AP)  ? "AP"  : "OFF");
  if (WiFi.status() == WL_CONNECTED) {
    console.printf("  SSID:        %s\n", WiFi.SSID().c_str());
    console.printf("  IP:          %s\n", WiFi.localIP().toString().c_str());
    console.printf("  RSSI:        %d dBm\n", WiFi.RSSI());
  } else if (WiFi.getMode() & WIFI_MODE_AP) {
    console.printf("  AP IP:       %s\n", WiFi.softAPIP().toString().c_str());
    console.printf("  AP Clients:  %d\n", WiFi.softAPgetStationNum());
  }
  console.printf("  WS Clients:  %d\n", ws.connectedClients());
  console.printf("  Throttle:    %d\n", lastCmd.throttle);
  console.printf("  Steer:       %d\n", lastCmd.steer);
  console.printf("  DRV Fault:   %s\n", drv.hasFault() ? "JA" : "Nein");
  console.printf("  Uptime:      %lus\n", millis() / 1000);
  console.println("--------------");
  console.println();
}

static void printSettings() {
  console.println();
  console.println("--- Settings ---");
  console.printf("  OTA:           %s (interval %lums)\n",
    settings.otaEnabled ? "ON" : "OFF", settings.otaIntervalMs);
  console.printf("  WiFi TX:       %d\n", settings.wifiTxPower);
  console.printf("  Failsafe:      %dms\n", settings.failsafeMs);
  console.printf("  Beacon:        %lums\n", settings.beaconIntervalMs);
  console.printf("  AP Prefix:     %s\n", settings.apPrefix);
  console.printf("  Always Panel:  %s\n", settings.alwaysStartPanel ? "JA" : "NEIN");
  console.printf("  Steer Invert:  %s\n", settings.steerInvert ? "JA" : "NEIN");
  console.printf("  Steer Gain:    %.2f\n", settings.steerGain);
  console.printf("  Steer DZ:      %d\n", settings.steerDeadzone);
  console.printf("  Steer Filter:  %.2f\n", settings.steerFilter);
  console.printf("  Batt Warn:     %.1fV\n", settings.battWarnV);
  console.printf("  Batt Off:      %.1fV\n", settings.battOffV);
  console.printf("  Max Throttle:  %d%%\n", settings.maxThrottlePct);
  console.printf("  ADC Corr:      %.4f\n", settings.adcCorrFactor);
  console.println("----------------");
  console.println();
}

static void printWifi() {
  console.println();
  console.printf("--- Gespeicherte Netzwerke (%d) ---\n", (int)savedNets.size());
  for (size_t i = 0; i < savedNets.size(); i++) {
    console.printf("  [%d] %s (pass len=%d)\n",
      (int)i, savedNets[i].ssid.c_str(), (int)savedNets[i].pass.length());
  }
  console.println();
}

static void printScan() {
  console.println("[WiFi] Scanning...");
  rc_wifi_scan();
  console.printf("--- Scan: %d Netzwerke ---\n", (int)lastScan.size());
  for (size_t i = 0; i < lastScan.size(); i++) {
    console.printf("  %s (%d dBm) %s\n",
      lastScan[i].ssid.c_str(), lastScan[i].rssi,
      lastScan[i].enc == 0 ? "OPEN" : "ENCRYPTED");
  }
  console.println();
}

static void printDrvRegisters() {
  console.println();
  console.println("--- DRV8323S Register ---");
  for (uint8_t reg = 0; reg <= 6; reg++) {
    uint16_t val = drv.readRegister(reg);
    console.printf("  Reg 0x%X = 0x%03X\n", reg, val);
  }
  console.printf("  nFAULT: %s\n", drv.hasFault() ? "LOW (Fault!)" : "HIGH (OK)");
  console.println("------------------------");
  console.println();
}

void rc_handle_command(const String& cmd) {
  if (cmd == "help")          printHelp();
  else if (cmd == "status")   printStatus();
  else if (cmd == "settings") printSettings();
  else if (cmd == "wifi")     printWifi();
  else if (cmd == "scan")     printScan();
  else if (cmd == "drv")      printDrvRegisters();
  else if (cmd == "log")      printLogStatus();
  else if (cmd == "log adc")   { logFlags.adc   = !logFlags.adc;   console.printf("[LOG] ADC:   %s\n", logFlags.adc   ? "ON" : "OFF"); }
  else if (cmd == "log drv")   { logFlags.drv   = !logFlags.drv;   console.printf("[LOG] DRV:   %s\n", logFlags.drv   ? "ON" : "OFF"); }
  else if (cmd == "log fota")  { logFlags.fota  = !logFlags.fota;  console.printf("[LOG] FOTA:  %s\n", logFlags.fota  ? "ON" : "OFF"); }
  else if (cmd == "log warn")  { logFlags.warn  = !logFlags.warn;  console.printf("[LOG] WARN:  %s\n", logFlags.warn  ? "ON" : "OFF"); }
  else if (cmd == "log ws")    { logFlags.ws    = !logFlags.ws;    console.printf("[LOG] WS:    %s\n", logFlags.ws    ? "ON" : "OFF"); }
  else if (cmd == "log net")   { logFlags.net   = !logFlags.net;   console.printf("[LOG] NET:   %s\n", logFlags.net   ? "ON" : "OFF"); }
  else if (cmd == "log http")  { logFlags.http  = !logFlags.http;  console.printf("[LOG] HTTP:  %s\n", logFlags.http  ? "ON" : "OFF"); }
  else if (cmd == "log servo") { logFlags.servo = !logFlags.servo; console.printf("[LOG] SERVO: %s\n", logFlags.servo ? "ON" : "OFF"); }
  else if (cmd == "log off")   { logFlags = {false, false, false, false, false, false, false, false}; console.println("[LOG] Alle Loop-Logs AUS"); }
  else if (cmd == "log on")    { logFlags = {true, true, true, true, true, true, true, true};          console.println("[LOG] Alle Loop-Logs AN"); }
  else if (cmd == "reboot") {
    console.println("[CMD] Reboot...");
    delay(200);
    ESP.restart();
  }
  else if (cmd == "ota") {
    console.println("[CMD] OTA Check gestartet...");
    rc_ota_loop();
  }
  else if (cmd == "portal") {
    console.println("[CMD] Config-Portal wird gestartet...");
    rc_start_portal();
  }
  else if (cmd == "recovery") {
    rc_recovery_mark_stable();
    console.println("[CMD] Crash-Counter reset, reboot fuer normalen Betrieb");
  }
  else if (cmd == "panel") {
    if (rc_start_panel_sta())
      console.println("[CMD] Config-Panel im STA-Modus aktiv");
    else
      console.println("[CMD] Config-Panel konnte nicht gestartet werden (WiFi verbunden?)");
  }
  else if (cmd == "motor off") {
    rc_motor_apply_phase(2);
    console.println("[CMD] Motor aus");
  }
  else if (cmd == "motor a") {
    rc_motor_apply_phase(0);
    console.println("[CMD] Motor Phase A aktiv");
  }
  else if (cmd == "motor b") {
    rc_motor_apply_phase(1);
    console.println("[CMD] Motor Phase B aktiv");
  }
  else {
    console.printf("[CMD] Unbekannt: '%s' — 'help' fuer Hilfe\n", cmd.c_str());
  }
}

void rc_serial_loop() {
  if (!Serial.available()) return;

  static String inputBuffer;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      inputBuffer.trim();
      if (inputBuffer.length() > 0) {
        rc_handle_command(inputBuffer);
        inputBuffer = "";
      }
    } else {
      inputBuffer += c;
    }
  }
}
