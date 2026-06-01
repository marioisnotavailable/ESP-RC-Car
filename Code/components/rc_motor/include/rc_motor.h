#pragma once
#include "rc_common.h"
#include "driver/ledc.h"

#define PWM_FREQ_HZ           20000
#define PWM_BITS              LEDC_TIMER_10_BIT
#define PWM_DUTY_MAX          1023

#define RAMP_START_US    5000   // 5ms start: duty=20% → enough to overcome static friction
#define RAMP_END_US      4000   // 4ms end — slower field, less wheel speed
#define RAMP_STEP_US      250   // ~22 steps total ramp
#define FAULT_CHECK_STEPS 50

void motor_task(void *arg);
