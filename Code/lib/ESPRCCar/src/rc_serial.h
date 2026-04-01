#pragma once

#include <Arduino.h>

// Log groups that can be toggled on/off
struct LogFlags {
  bool adc;   // [ADC] battery readings every 1s
  bool drv;   // [DRV] motor phase switches + faults
  bool fota;  // [FOTA] OTA check messages
  bool warn;  // [WARN] low voltage warnings
};

extern LogFlags logFlags;

// Serial command handler — call in loop()
void rc_serial_loop();
