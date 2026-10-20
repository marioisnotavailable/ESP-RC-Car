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

// ── Pin Definitions (neues PCB) ──
// DRV8323S SPI
#define PIN_DRV_CS      37   // SS
#define PIN_DRV_SCLK    38
#define PIN_DRV_MOSI    39
#define PIN_DRV_MISO    40   // Pinliste sagt "IO20" (Tippfehler, IO20 = USB DP) -> 40 angenommen
#define PIN_DRV_EN      36   // Pull-up = DRV enabled
#define PIN_DRV_FAULT   6    // nFAULT
#define PIN_DRV_SOC     2
#define PIN_DRV_CAL     35
#define PIN_DRV_SOA     41
#define PIN_DRV_SOB     42

// Motor PWM (3-Phasen)
#define PIN_INHA   9
#define PIN_INLA   10
#define PIN_INHB   11
#define PIN_INLB   12
#define PIN_INHC   13
#define PIN_INLC   21

// Servo
#define PIN_SERVO       48

// Batterie ADC (GPIO8 = ADC1 CH7), VBUS ADC (GPIO14 = ADC2 CH3)
#define BATT_ADC_CHANNEL  ADC_CHANNEL_7
#define PIN_VBATADC       8
#define VBAT_DIVIDER      ((27.0f + 12.0f) / 12.0f)    // R43 27k / R44 12k = 3.25
#define VBUS_DIVIDER      ((390.0f + 27.0f) / 27.0f)   // R37 390k / R38 27k = 15.44
#define PIN_VBUSADC       14

// STUSB4500 USB-PD (I2C)
#define PIN_PD_SDA    16
#define PIN_PD_SCL    17
#define PIN_PD_RESET  3    // Pull-up = Reset
#define PIN_PD_ALERT  15

// Charge (Pull-down = Laden aktiv)
#define PIN_CHARGE_RESTART  18

// Sonstiges
#define PIN_MUX_ST    7
#define PIN_LED_DATA  4
#define PIN_LED_CLK   5

// ── Init ──
void rc_common_init(void);
