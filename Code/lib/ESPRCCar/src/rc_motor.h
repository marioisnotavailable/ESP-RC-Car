#pragma once

#include <Arduino.h>
#include "drv8323.h"

// PWM configuration
static constexpr uint32_t PWM_FREQ      = 20000;   // 20 kHz
static constexpr uint8_t  PWM_BITS      = 10;      // 0..1023
static constexpr uint16_t PWM_DUTY      = 512;     // ~50%

// Commutation timing
static constexpr uint32_t MOTOR_HOLD_MS = 7000UL;  // hold per direction
static constexpr uint32_t STEP_SLOW_MS  = 100UL;   // ramp start interval
static constexpr uint32_t STEP_FAST_MS  = 10UL;    // ramp end / hold interval
static constexpr uint8_t  RAMP_STEPS    = 20;      // steps in ramp

extern DRV8323 drv;

void rc_motor_setup();
void rc_motor_loop();
void rc_motor_all_off();
void rc_motor_apply_phase(int step);
void rc_motor_fault_check();
