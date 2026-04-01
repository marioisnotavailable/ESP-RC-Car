#pragma once

#include <Arduino.h>

// Servo parameters
#define SERVO_CH        6
#define SERVO_FREQ      50
#define SERVO_RES_BITS  12

#define SERVO_MIN_US    1000
#define SERVO_MAX_US    2000
#define SERVO_MID_US    1500

#define SERVO_SLEW_US_PER_LOOP 0

extern float steerFilt;
extern int   currentServoUs;

void rc_steering_init_ledc();
void rc_steering_init_fallback();
void rc_steering_write_us(int targetUs);
void rc_steering_apply(int16_t steerInput);
