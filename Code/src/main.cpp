#include <Arduino.h>
#include "ESPRCCar.h"

void setup() {
  Serial.begin(115200);             // Serielle Konsole starten
  rc_recovery_check_boot();         // Crash-Counter pruefen, Safe Mode / Watchdog
  rc_mrd_check_boot();              // Multi-Reset erkennen (3x Reset -> Config-Portal)
  rc_boot_log();                    // Firmware-Version ausgeben, 1s Startverzoegerung

  if (!safeMode) {
    rc_motor_setup();               // DRV8323S Motortreiber + PWM initialisieren
  }

  rc_fs_begin();                    // LittleFS mounten und Dateien auflisten
  rc_settings_load();               // Geraeteeinstellungen aus NVS laden
  rc_network_load();                // Gespeicherte WLAN-Netzwerke aus NVS laden
  bool wifiConnected = rc_wifi_setup(); // WLAN verbinden (oder AP-Portal starten)
  rc_ota_setup(wifiConnected);      // OTA planen
  if (wifiConnected && settings.alwaysStartPanel) {
    rc_start_panel_sta();           // Config-Panel im bestehenden WLAN starten
  }
  rc_websocket_begin();             // WebSocket-Server auf Port 81 starten
  rc_udp_begin();                   // UDP-Discovery-Service starten

  if (!safeMode) {
    rc_battery_setup();             // ADC fuer Batteriespannung konfigurieren
  }
}

void loop() {
  rc_recovery_loop();               // Watchdog fuettern, Stabilitaets-Timer
  rc_mrd_loop();                    // Multi-Reset Zaehler nach Timeout zuruecksetzen
  rc_wifi_scan_loop();              // Async WiFi-Scan Ergebnisse einsammeln
  rc_portal_loop();                 // DNS + HTTP fuer Config-Portal verarbeiten
  rc_websocket_loop();              // WebSocket-Befehle (Throttle/Steering) empfangen
  rc_udp_loop();                    // UDP-Discovery Anfragen beantworten + Beacon senden
  rc_ota_loop();                    // Periodisch auf Firmware-Updates pruefen + ArduinoOTA
  rc_serial_loop();                 // Serial-Befehle verarbeiten (help, status, ...)

  if (safeMode) return;             // Safe Mode: nur Netzwerk + OTA + Terminal

  rc_battery_loop();                // ADC auslesen, Spannung berechnen, Deep-Sleep pruefen
  rc_motor_loop();                  // Motor-Diagnose: Phasen A/B/Off durchschalten
  rc_motor_fault_check();           // DRV8323S nFAULT-Pin pruefen, Fehler loggen/clearen
  rc_websocket_broadcast_batt();    // Batterieprozent an alle WS-Clients senden
  rc_websocket_failsafe_check();    // Lenkung auf 0 setzen wenn kein Befehl kommt
  rc_ntp_stamp_loop();              // NTP-Zeitstempel fuer verbundenes Netzwerk setzen
}
