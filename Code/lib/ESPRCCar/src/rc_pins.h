#pragma once

// ── DRV8323S (gate driver) SPI pins ──
static constexpr uint8_t PIN_DRV_MISO  = 5;
static constexpr uint8_t PIN_DRV_MOSI  = 6;
static constexpr uint8_t PIN_DRV_SCLK  = 7;
static constexpr uint8_t PIN_DRV_EN    = 16;
static constexpr uint8_t PIN_DRV_CS    = 15;
static constexpr uint8_t PIN_DRV_FAULT = 39;

// ── Motor PWM pins (3-phase bridge) ──
static constexpr uint8_t PIN_INHA = 18;
static constexpr uint8_t PIN_INLA = 8;
static constexpr uint8_t PIN_INHB = 3;
static constexpr uint8_t PIN_INLB = 9;
static constexpr uint8_t PIN_INHC = 10;
static constexpr uint8_t PIN_INLC = 11;

// ── Servo / Steering ──
#define LENKUNG_PIN 5   // NOTE: conflicts with PIN_DRV_MISO, servo disabled

// ── Charging restart ──
#define CHARGE_RESTART_PIN 47

// ── ADC (battery) ──
#define ADC_UB_CHANNEL ADC1_CHANNEL_0  // GPIO1
