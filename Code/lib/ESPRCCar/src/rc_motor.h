#pragma once

#include <Arduino.h>
#include "drv8323.h"

// PWM configuration
static constexpr uint32_t PWM_FREQ = 20000;  // 20 kHz
static constexpr uint8_t  PWM_BITS = 10;     // 0..1023
static constexpr uint16_t PWM_DUTY = 512;    // ~50%

// Diagnostic phase duration
static constexpr uint32_t MOTOR_DIR_SWITCH_MS = 5000UL;

// LEDC channels
static const int CH_PWM_A = 0;
static const int CH_PWM_B = 2;

extern DRV8323 drv;

void rc_motor_setup();
void rc_motor_loop();
void rc_motor_all_off();
void rc_motor_apply_phase(int phase);
void rc_motor_fault_check();
