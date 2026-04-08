#pragma once

#include <Arduino.h>

#define CRASH_THRESHOLD   3       // Nach 3 Crashes → Safe Mode
#define BOOT_STABLE_MS    30000   // 30s stabiler Betrieb → Counter reset

extern bool safeMode;

void rc_recovery_check_boot();    // Crash-Counter prüfen, Safe Mode setzen
void rc_recovery_mark_stable();   // Counter zurücksetzen nach stabilem Betrieb
void rc_recovery_loop();          // Stabilitäts-Timer überwachen
void rc_rollback_confirm();       // OTA-Partition als gültig markieren
