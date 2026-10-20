#pragma once

// Feature-Auswahl fuer PCB-Bringup.
// Kein RC_TEST_* gesetzt  -> normale Firmware, alles laeuft.
// Mind. ein RC_TEST_* gesetzt -> nur diese Features laufen (kombinierbar).
//
//   RC_TEST_WIFI     comms_task: AP/STA, Webpanel, WebSocket, DNS, UDP-Discovery
//   RC_TEST_MOTOR    motor_task: DRV8323 (SPI) + 6-Step-PWM
//   RC_TEST_SERVO    steering_task: Servo-PWM
//   RC_TEST_BATTERY  system_task: Batterie-ADC + Low-Voltage-Cutoff
//   RC_TEST_PD       pd_task: STUSB4500 USB-PD (I2C)
//   RC_TEST_BG       bg_task: FOTA + serielle Konsole
//
// Setzen per platformio.ini (build_flags = -DRC_TEST_WIFI), siehe Envs test_*.

#if defined(RC_TEST_WIFI) || defined(RC_TEST_MOTOR) || defined(RC_TEST_SERVO) || \
    defined(RC_TEST_BATTERY) || defined(RC_TEST_BG) || defined(RC_TEST_PD)
#define RC_ISOLATED 1
#else
#define RC_ISOLATED 0
#endif

#if RC_ISOLATED
#  ifdef RC_TEST_WIFI
#    define RC_EN_WIFI 1
#  endif
#  ifdef RC_TEST_MOTOR
#    define RC_EN_MOTOR 1
#  endif
#  ifdef RC_TEST_SERVO
#    define RC_EN_SERVO 1
#  endif
#  ifdef RC_TEST_BATTERY
#    define RC_EN_BATTERY 1
#  endif
#  ifdef RC_TEST_BG
#    define RC_EN_BG 1
#  endif
#  ifdef RC_TEST_PD
#    define RC_EN_PD 1
#  endif
#else
#  define RC_EN_WIFI 1
#  define RC_EN_MOTOR 1
#  define RC_EN_SERVO 1
#  define RC_EN_BATTERY 1
#  define RC_EN_BG 1
#  define RC_EN_PD 1
#endif

#ifndef RC_EN_WIFI
#  define RC_EN_WIFI 0
#endif
#ifndef RC_EN_MOTOR
#  define RC_EN_MOTOR 0
#endif
#ifndef RC_EN_SERVO
#  define RC_EN_SERVO 0
#endif
#ifndef RC_EN_BATTERY
#  define RC_EN_BATTERY 0
#endif
#ifndef RC_EN_BG
#  define RC_EN_BG 0
#endif
#ifndef RC_EN_PD
#  define RC_EN_PD 0
#endif
