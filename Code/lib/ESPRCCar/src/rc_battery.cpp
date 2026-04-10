#include "rc_battery.h"
#include "rc_settings.h"
#include "rc_serial.h"
#include "rc_console.h"
#include "rc_pins.h"
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

// Spannungsteiler
#define R1            1800000.0f
#define R2            1000000.0f
#define DIVIDER_RATIO ((R1 + R2) / R2)

volatile int batteryPercent   = 0;
float vBatt_float_last        = 0.0f;
float vAdc_last               = 0.0f;

static adc_oneshot_unit_handle_t adc_handle    = nullptr;
static adc_cali_handle_t         cali_handle   = nullptr;
static bool                      cali_ok       = false;

static int           lowVoltageHits = 0;
static unsigned long lastEval       = 0;
static unsigned long lastSampleMs   = 0;
static uint32_t      rawAccum       = 0;
static int           sampleCount    = 0;

static void adc_cali_init() {
  adc_cali_curve_fitting_config_t cf_cfg = {
    .unit_id  = ADC_UNIT_1,
    .chan     = ADC_CHANNEL_0,
    .atten    = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_12,
  };
  esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cf_cfg, &cali_handle);
  cali_ok = (ret == ESP_OK);
  console.printf("[CAL] Kalibrierung: %s\n", cali_ok ? "OK" : "FAIL");
}

void rc_battery_setup() {
  console.println("[BOOT] Batteriemonitor gestartet");

  adc_oneshot_unit_init_cfg_t unit_cfg = {
    .unit_id  = ADC_UNIT_1,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

  adc_oneshot_chan_cfg_t chan_cfg = {
    .atten    = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_12,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, (adc_channel_t)ADC_UB_CHANNEL, &chan_cfg));

  adc_cali_init();

  lastSampleMs = millis();
  lastEval     = millis();
}

void rc_battery_loop() {
  unsigned long now = millis();

  if ((unsigned long)(now - lastSampleMs) >= SAMPLE_INTERVAL_MS) {
    unsigned long elapsed = now - lastSampleMs;
    unsigned long steps = elapsed / SAMPLE_INTERVAL_MS;
    if (steps > 4) steps = 4;
    lastSampleMs += steps * SAMPLE_INTERVAL_MS;

    for (unsigned long i = 0; i < steps; ++i) {
      uint32_t subAccum = 0;
      int      subCount = 0;
      for (int s = 0; s < ADC_SUBSAMPLES_PER_SAMPLE; ++s) {
        int raw = 0;
        if (adc_oneshot_read(adc_handle, (adc_channel_t)ADC_UB_CHANNEL, &raw) == ESP_OK) {
          subAccum += (uint32_t)raw;
          subCount++;
        }
      }
      if (subCount > 0) {
        rawAccum += (subAccum + (uint32_t)(subCount / 2)) / (uint32_t)subCount;
        sampleCount++;
      }
    }
  }

  if ((unsigned long)(now - lastEval) < RESULT_INTERVAL_MS && sampleCount < SAMPLE_COUNT) return;
  lastEval = now;

  if (sampleCount <= 0) {
    console.println("[ERR] Keine ADC-Samples");
    return;
  }

  uint32_t avgRaw = (rawAccum + (uint32_t)(sampleCount / 2)) / (uint32_t)sampleCount;
  rawAccum    = 0;
  sampleCount = 0;

  float vAdc = 0.0f;
  if (cali_ok) {
    int mv = 0;
    adc_cali_raw_to_voltage(cali_handle, (int)avgRaw, &mv);
    vAdc = mv / 1000.0f;
  } else {
    vAdc = (float)avgRaw / 4095.0f * 3.3f;
  }

  float vBatt   = vAdc * DIVIDER_RATIO * settings.adcCorrFactor;
  float percent = ((vBatt - BATT_MIN_VOLTAGE) / BATT_VOLTAGE_RANGE) * 100.0f;
  percent = constrain(percent, 0.0f, 100.0f);

  batteryPercent   = (int)percent;
  vBatt_float_last = vBatt;
  vAdc_last        = vAdc;

  if (logFlags.adc)
    console.printf("[ADC] raw:%lu | V_ADC: %.3fV (%s) | V_Batt: %.2fV (%d%%) | Samples: %d\n",
      (unsigned long)avgRaw, vAdc, cali_ok ? "kalibriert" : "unkalibriert",
      vBatt, batteryPercent, sampleCount);

  if (vBatt <= settings.battOffV) {
    lowVoltageHits++;
    if (logFlags.warn)
      console.printf("[WARN] Unterspannung %d/%d\n", lowVoltageHits, LOW_CONFIRM_COUNT);
  } else {
    lowVoltageHits = 0;
    if (vBatt <= settings.battWarnV && logFlags.warn)
      console.println("[WARN] Batterie niedrig!");
  }

  if (lowVoltageHits >= LOW_CONFIRM_COUNT) {
    console.println("[SLEEP] Batterie zu niedrig – Deep Sleep");
    esp_sleep_enable_timer_wakeup(4000000ULL);
    esp_deep_sleep_start();
  }
}
