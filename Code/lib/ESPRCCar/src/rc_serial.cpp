#include "rc_serial.h"
#include "rc_settings.h"
#include "rc_battery.h"
#include "rc_motor.h"
#include "rc_network.h"
#include "rc_websocket.h"
#include "rc_httpapi.h"
#include "rc_ota.h"

LogFlags logFlags = { true, true, true, true };

static void printHelp() {
  Serial.println();
  Serial.println("=== Serial Commands ===");
  Serial.println("  help        — Diese Hilfe anzeigen");
  Serial.println("  status      — Batterie, WiFi, Motor Status");
  Serial.println("  settings    — Alle Einstellungen anzeigen");
  Serial.println("  reboot      — ESP neustarten");
  Serial.println("  ota         — OTA Update jetzt pruefen");
  Serial.println("  portal      — Config-Portal starten");
  Serial.println("  wifi        — Gespeicherte Netzwerke anzeigen");
  Serial.println("  scan        — WLAN-Scan durchfuehren");
  Serial.println("  drv         — DRV8323S Register auslesen");
  Serial.println("  motor off   — Motor ausschalten");
  Serial.println("  motor a     — Motor Phase A aktivieren");
  Serial.println("  motor b     — Motor Phase B aktivieren");
  Serial.println("  log         — Log-Gruppen Status anzeigen");
  Serial.println("  log adc     — [ADC] Batterie-Logs toggeln");
  Serial.println("  log drv     — [DRV] Motor-Logs toggeln");
  Serial.println("  log fota    — [FOTA] OTA-Logs toggeln");
  Serial.println("  log warn    — [WARN] Warnungs-Logs toggeln");
  Serial.println("  log off     — Alle Loop-Logs aus");
  Serial.println("  log on      — Alle Loop-Logs an");
  Serial.println("=======================");
  Serial.println();
}

static void printLogStatus() {
  Serial.println();
  Serial.println("--- Log Groups ---");
  Serial.printf("  [ADC]  Batterie:    %s\n", logFlags.adc  ? "ON" : "OFF");
  Serial.printf("  [DRV]  Motor:       %s\n", logFlags.drv  ? "ON" : "OFF");
  Serial.printf("  [FOTA] OTA:         %s\n", logFlags.fota ? "ON" : "OFF");
  Serial.printf("  [WARN] Warnungen:   %s\n", logFlags.warn ? "ON" : "OFF");
  Serial.println("------------------");
  Serial.println();
}

static void printStatus() {
  Serial.println();
  Serial.println("--- Status ---");
  Serial.printf("  Firmware:    %s\n", FOTA_CURRENT_VERSION);
  Serial.printf("  Batterie:    %.2fV (%d%%)\n", vBatt_float_last, batteryPercent);
  Serial.printf("  WiFi Mode:   %s\n",
    (WiFi.getMode() & WIFI_MODE_STA) ? "STA" :
    (WiFi.getMode() & WIFI_MODE_AP)  ? "AP"  : "OFF");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("  SSID:        %s\n", WiFi.SSID().c_str());
    Serial.printf("  IP:          %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  RSSI:        %d dBm\n", WiFi.RSSI());
  } else if (WiFi.getMode() & WIFI_MODE_AP) {
    Serial.printf("  AP IP:       %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("  AP Clients:  %d\n", WiFi.softAPgetStationNum());
  }
  Serial.printf("  WS Clients:  %d\n", ws.connectedClients());
  Serial.printf("  Throttle:    %d\n", lastCmd.throttle);
  Serial.printf("  Steer:       %d\n", lastCmd.steer);
  Serial.printf("  DRV Fault:   %s\n", drv.hasFault() ? "JA" : "Nein");
  Serial.printf("  Uptime:      %lus\n", millis() / 1000);
  Serial.println("--------------");
  Serial.println();
}

static void printSettings() {
  Serial.println();
  Serial.println("--- Settings ---");
  Serial.printf("  OTA:           %s (interval %lums)\n",
    settings.otaEnabled ? "ON" : "OFF", settings.otaIntervalMs);
  Serial.printf("  WiFi TX:       %d\n", settings.wifiTxPower);
  Serial.printf("  Failsafe:      %dms\n", settings.failsafeMs);
  Serial.printf("  Beacon:        %lums\n", settings.beaconIntervalMs);
  Serial.printf("  AP Prefix:     %s\n", settings.apPrefix);
  Serial.printf("  Steer Invert:  %s\n", settings.steerInvert ? "JA" : "NEIN");
  Serial.printf("  Steer Gain:    %.2f\n", settings.steerGain);
  Serial.printf("  Steer DZ:      %d\n", settings.steerDeadzone);
  Serial.printf("  Steer Filter:  %.2f\n", settings.steerFilter);
  Serial.printf("  Batt Warn:     %.1fV\n", settings.battWarnV);
  Serial.printf("  Batt Off:      %.1fV\n", settings.battOffV);
  Serial.printf("  Max Throttle:  %d%%\n", settings.maxThrottlePct);
  Serial.printf("  ADC Corr:      %.4f\n", settings.adcCorrFactor);
  Serial.println("----------------");
  Serial.println();
}

static void printWifi() {
  Serial.println();
  Serial.printf("--- Gespeicherte Netzwerke (%d) ---\n", (int)savedNets.size());
  for (size_t i = 0; i < savedNets.size(); i++) {
    Serial.printf("  [%d] %s (pass len=%d)\n",
      (int)i, savedNets[i].ssid.c_str(), (int)savedNets[i].pass.length());
  }
  Serial.println();
}

static void printScan() {
  Serial.println("[WiFi] Scanning...");
  rc_wifi_scan();
  Serial.printf("--- Scan: %d Netzwerke ---\n", (int)lastScan.size());
  for (size_t i = 0; i < lastScan.size(); i++) {
    Serial.printf("  %s (%d dBm) %s\n",
      lastScan[i].ssid.c_str(), lastScan[i].rssi,
      lastScan[i].enc == 0 ? "OPEN" : "ENCRYPTED");
  }
  Serial.println();
}

static void printDrvRegisters() {
  Serial.println();
  Serial.println("--- DRV8323S Register ---");
  for (uint8_t reg = 0; reg <= 6; reg++) {
    uint16_t val = drv.readRegister(reg);
    Serial.printf("  Reg 0x%X = 0x%03X\n", reg, val);
  }
  Serial.printf("  nFAULT: %s\n", drv.hasFault() ? "LOW (Fault!)" : "HIGH (OK)");
  Serial.println("------------------------");
  Serial.println();
}

static void handleCommand(const String& cmd) {
  if (cmd == "help")          printHelp();
  else if (cmd == "status")   printStatus();
  else if (cmd == "settings") printSettings();
  else if (cmd == "wifi")     printWifi();
  else if (cmd == "scan")     printScan();
  else if (cmd == "drv")      printDrvRegisters();
  else if (cmd == "log")      printLogStatus();
  else if (cmd == "log adc")  { logFlags.adc  = !logFlags.adc;  Serial.printf("[LOG] ADC:  %s\n", logFlags.adc  ? "ON" : "OFF"); }
  else if (cmd == "log drv")  { logFlags.drv  = !logFlags.drv;  Serial.printf("[LOG] DRV:  %s\n", logFlags.drv  ? "ON" : "OFF"); }
  else if (cmd == "log fota") { logFlags.fota = !logFlags.fota; Serial.printf("[LOG] FOTA: %s\n", logFlags.fota ? "ON" : "OFF"); }
  else if (cmd == "log warn") { logFlags.warn = !logFlags.warn; Serial.printf("[LOG] WARN: %s\n", logFlags.warn ? "ON" : "OFF"); }
  else if (cmd == "log off")  { logFlags = {false, false, false, false}; Serial.println("[LOG] Alle Loop-Logs AUS"); }
  else if (cmd == "log on")   { logFlags = {true, true, true, true};     Serial.println("[LOG] Alle Loop-Logs AN"); }
  else if (cmd == "reboot") {
    Serial.println("[CMD] Reboot...");
    delay(200);
    ESP.restart();
  }
  else if (cmd == "ota") {
    Serial.println("[CMD] OTA Check gestartet...");
    rc_ota_loop();
  }
  else if (cmd == "portal") {
    Serial.println("[CMD] Config-Portal wird gestartet...");
    rc_start_portal();
  }
  else if (cmd == "motor off") {
    rc_motor_apply_phase(2);
    Serial.println("[CMD] Motor aus");
  }
  else if (cmd == "motor a") {
    rc_motor_apply_phase(0);
    Serial.println("[CMD] Motor Phase A aktiv");
  }
  else if (cmd == "motor b") {
    rc_motor_apply_phase(1);
    Serial.println("[CMD] Motor Phase B aktiv");
  }
  else {
    Serial.printf("[CMD] Unbekannt: '%s' — 'help' fuer Hilfe\n", cmd.c_str());
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
        handleCommand(inputBuffer);
        inputBuffer = "";
      }
    } else {
      inputBuffer += c;
    }
  }
}
