#include <Arduino.h>
#include "ESPRCCar.h"

void setup() {
  Serial.begin(115200);             // Serielle Konsole starten
  rc_mrd_check_boot();              // Multi-Reset erkennen (3x Reset -> Config-Portal)
  rc_boot_log();                    // Firmware-Version ausgeben, 1s Startverzoegerung
  rc_motor_setup();                 // DRV8323S Motortreiber + PWM initialisieren
  pinMode(LED_PIN, OUTPUT);         // Status-LED Pin konfigurieren
  rc_fs_begin();                    // LittleFS mounten und Dateien auflisten
  rc_settings_load();               // Geraeteeinstellungen aus NVS laden
  rc_network_load();                // Gespeicherte WLAN-Netzwerke aus NVS laden
  rc_ota_setup(rc_wifi_setup());    // WLAN verbinden (oder AP-Portal starten), OTA planen
  rc_websocket_begin();             // WebSocket-Server auf Port 81 starten
  rc_udp_begin();                   // UDP-Discovery-Service starten
  rc_battery_setup();               // ADC fuer Batteriespannung konfigurieren
  lastCmdMs = millis();             // Failsafe-Timer initialisieren
}

void loop() {
  rc_mrd_loop();                    // Multi-Reset Zaehler nach Timeout zuruecksetzen
  rc_portal_loop();                 // DNS + HTTP fuer Config-Portal verarbeiten
  rc_websocket_loop();              // WebSocket-Befehle (Throttle/Steering) empfangen
  rc_udp_loop();                    // UDP-Discovery Anfragen beantworten + Beacon senden
  rc_ota_loop();                    // Periodisch auf Firmware-Updates pruefen
  rc_battery_loop();                // ADC auslesen, Spannung berechnen, Deep-Sleep pruefen
  rc_motor_loop();                  // Motor-Diagnose: Phasen A/B/Off durchschalten
  rc_motor_fault_check();           // DRV8323S nFAULT-Pin pruefen, Fehler loggen/clearen
  rc_websocket_broadcast_batt();    // Batterieprozent an alle WS-Clients senden
  rc_websocket_failsafe_check();    // Lenkung auf 0 setzen wenn kein Befehl kommt
  rc_led_loop(lastCmd.throttle);    // LED-Blinken je nach Gasstellung
  rc_serial_loop();                 // Serial-Befehle verarbeiten (help, status, ...)
}
