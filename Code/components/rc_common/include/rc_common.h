#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"

// ── Shared Command struct ──
typedef struct {
    int16_t throttle;   // -1000 .. +1000
    int16_t steer;      // -1000 .. +1000
    uint8_t flags;
} Cmd;

// ── EventGroup Bits ──
#define WIFI_CONNECTED_BIT  BIT0
#define SAFE_MODE_BIT       BIT1
#define MOTOR_FAULT_BIT     BIT2

// ── Global Handles ──
extern QueueHandle_t       cmd_queue;
extern QueueHandle_t       batt_queue;
extern EventGroupHandle_t  rc_events;

// ── Pin Definitions ──
// DRV8323S SPI
#define PIN_DRV_MISO    5
#define PIN_DRV_MOSI    6
#define PIN_DRV_SCLK    7
#define PIN_DRV_EN      16
#define PIN_DRV_CS      15
#define PIN_DRV_FAULT   39

// Motor PWM (3-Phasen)
#define PIN_INHA   18
#define PIN_INLA   8
#define PIN_INHB   3
#define PIN_INLB   9
#define PIN_INHC   10
#define PIN_INLC   11

// Servo (GPIO5 Konflikt behoben → GPIO2)
#define PIN_SERVO       2

// Batterie ADC
#define BATT_ADC_CHANNEL  ADC_CHANNEL_0   // GPIO1

// Charge restart
#define PIN_CHARGE_RESTART  47

// ── Init ──
void rc_common_init(void);
