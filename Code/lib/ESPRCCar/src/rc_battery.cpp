#include "rc_battery.h"
#include "rc_settings.h"
#include "rc_pins.h"
#include "driver/gpio.h"
#include <driver/adc.h>
#include <esp_adc_cal.h>

// Spannungsteiler
#define R1            1800000.0f
#define R2            1000000.0f
#define DIVIDER_RATIO ((R1 + R2) / R2)

volatile int batteryPercent   = 0;
float vBatt_float_last        = 0.0f;
float vAdc_last               = 0.0f;

static esp_adc_cal_characteristics_t adc_chars;
static bool          cali_ok        = false;
static int           lowVoltageHits = 0;
static unsigned long lastEval       = 0;
static uint32_t      mvAccum        = 0;
static int           sampleCount    = 0;

static void adc_cali_init() {
  esp_adc_cal_value_t cal_type = esp_adc_cal_characterize(
    ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

  if (cal_type == ESP_ADC_CAL_VAL_EFUSE_VREF)
    Serial.println("[CAL] eFuse Vref Kalibrierung OK");
  else if (cal_type == ESP_ADC_CAL_VAL_EFUSE_TP)
    Serial.println("[CAL] eFuse Two Point Kalibrierung OK");
  else
    Serial.println("[CAL] Keine eFuse – Default Vref 1100mV");

  cali_ok = true;
}

void rc_battery_setup() {
  Serial.println("[BOOT] Batteriemonitor gestartet");

  gpio_config_t io_conf = {};
  io_conf.intr_type     = GPIO_INTR_DISABLE;
  io_conf.mode          = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask  = (1ULL << 1);
  io_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en    = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);

  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC_UB_CHANNEL, ADC_ATTEN_DB_11);
  adc_cali_init();

  lastEval = millis();
}

void rc_battery_loop() {
  int raw = adc1_get_raw(ADC_UB_CHANNEL);
  if (raw >= 0) {
    mvAccum += esp_adc_cal_raw_to_voltage(raw, &adc_chars);
    sampleCount++;
  }

  if (millis() - lastEval < RESULT_INTERVAL_MS) return;
  lastEval = millis();

  if (sampleCount == 0) {
    Serial.println("[ERR] Keine Samples");
    return;
  }

  float vAdc  = (float)(mvAccum / sampleCount) / 1000.0f;
  float vBatt = vAdc * DIVIDER_RATIO * settings.adcCorrFactor;

  float percent = ((vBatt - BATT_MIN_VOLTAGE) / BATT_VOLTAGE_RANGE) * 100.0f;
  percent = constrain(percent, 0.0f, 100.0f);
  batteryPercent    = (int)percent;
  vBatt_float_last  = vBatt;
  vAdc_last         = vAdc;

  Serial.printf("[ADC] V_ADC: %.3fV (%s) | V_Batt: %.2fV (vBatt=%d) | Samples: %d\n",
                vAdc, cali_ok ? "kalibriert" : "unkalibriert", vBatt, batteryPercent, sampleCount);

  mvAccum     = 0;
  sampleCount = 0;

  if (vBatt <= settings.battOffV) {
    lowVoltageHits++;
    Serial.printf("[WARN] Unterspannung %d/%d\n", lowVoltageHits, LOW_CONFIRM_COUNT);
  } else {
    lowVoltageHits = 0;
    if (vBatt <= settings.battWarnV)
      Serial.println("[WARN] Batterie niedrig!");
  }

  if (lowVoltageHits >= LOW_CONFIRM_COUNT) {
    Serial.println("[SLEEP] Batterie zu niedrig – Deep Sleep");
    esp_sleep_enable_timer_wakeup(4000000ULL);
    esp_deep_sleep_start();
  }
}
