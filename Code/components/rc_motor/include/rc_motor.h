#pragma once
#include "rc_common.h"
#include "driver/ledc.h"

#define PWM_FREQ_HZ           20000
#define PWM_BITS              LEDC_TIMER_10_BIT
#define PWM_DUTY_MAX          1023

#define RAMP_PERIOD_MAX_MS    50
#define RAMP_PERIOD_MIN_MS     5
#define RAMP_DUTY_START       (PWM_DUTY_MAX / 10)
#define RAMP_DUTY_STEP         5
#define RAMP_PERIOD_STEP_MS    1
#define FAULT_CHECK_STEPS     20

void motor_task(void *arg);
