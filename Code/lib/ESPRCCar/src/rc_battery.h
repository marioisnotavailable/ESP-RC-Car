#pragma once

#include <Arduino.h>

// Batterie-Kalibrierung
#define BATT_MAX_VOLTAGE   8.39f
#define BATT_MIN_VOLTAGE   7.5f
#define BATT_VOLTAGE_RANGE (BATT_MAX_VOLTAGE - BATT_MIN_VOLTAGE)

// Sampling
#define SAMPLE_INTERVAL_MS  2
#define SAMPLE_COUNT        500
#define RESULT_INTERVAL_MS  1000
#define LOW_CONFIRM_COUNT   3

// ADC stabilization (no temporal smoothing)
#define ADC_CLK_DIVIDER             255
#define ADC_SUBSAMPLES_PER_SAMPLE   3

extern volatile int batteryPercent;
extern float vBatt_float_last;
extern float vAdc_last;

void rc_battery_setup();
void rc_battery_loop();
