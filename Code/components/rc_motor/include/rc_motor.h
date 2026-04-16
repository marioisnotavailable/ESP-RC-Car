#pragma once
#include "rc_common.h"

#define PWM_FREQ_HZ    20000
#define PWM_BITS       LEDC_TIMER_10_BIT
#define PWM_DUTY_MAX   1023

void motor_task(void *arg);
