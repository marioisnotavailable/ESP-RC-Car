#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

// ── Pins & Hardware ──────────────────────────────────────────
#define ADC_UB_CHANNEL  ADC1_CHANNEL_0  // GPIO1 = ADC1_CH0

// ── Spannungsteiler ──────────────────────────────────────────
#define R1              1800000.0f
#define R2              1000000.0f
#define DIVIDER_RATIO   ((R1 + R2) / R2)

// ── Batterie-Schwellen ────────────────────────────────────────
#define POWER_WARN_MODE 7.9f
#define POWER_OFF_MODE  7.4f

// ── Sampling ─────────────────────────────────────────────────
#define SAMPLE_INTERVAL_MS  2
#define SAMPLE_COUNT        500
#define RESULT_INTERVAL_MS  1000
#define LOW_CONFIRM_COUNT   3

// ── Globals ──────────────────────────────────────────────────
static esp_adc_cal_characteristics_t adc_chars;
static bool     cali_ok        = false;
static int      lowVoltageHits = 0;

// ─────────────────────────────────────────────────────────────
void adc_cali_init()
{
    esp_adc_cal_value_t cal_type = esp_adc_cal_characterize(
        ADC_UNIT_1,
        ADC_ATTEN_DB_12,
        ADC_WIDTH_BIT_12,
        1100,           // default Vref falls eFuse leer
        &adc_chars
    );

    if (cal_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        Serial.println("[CAL] eFuse Vref Kalibrierung OK");
        cali_ok = true;
    } else if (cal_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        Serial.println("[CAL] eFuse Two Point Kalibrierung OK");
        cali_ok = true;
    } else {
        Serial.println("[CAL] Keine eFuse – Default Vref 1100mV");
        cali_ok = true; // chars trotzdem nutzbar
    }
}

// ─────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("[BOOT] Batteriemonitor gestartet");

    gpio_config_t io_conf = {};
    io_conf.intr_type     = GPIO_INTR_DISABLE;
    io_conf.mode          = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask  = (1ULL << 1);
    io_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en    = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_UB_CHANNEL, ADC_ATTEN_DB_12);
    adc_cali_init();
}

// ─────────────────────────────────────────────────────────────
void loop()
{
    static unsigned long lastEval = 0;
    static uint32_t      mvAccum  = 0;
    static int           count    = 0;

    // Sampling
    int raw = adc1_get_raw(ADC_UB_CHANNEL);
    if (raw >= 0) {
        mvAccum += esp_adc_cal_raw_to_voltage(raw, &adc_chars);
        count++;
    }

    if (millis() - lastEval < RESULT_INTERVAL_MS) return;
    lastEval = millis();

    if (count == 0) {
        Serial.println("[ERR] Keine Samples");
        return;
    }

    // ── Spannung berechnen ────────────────────────────────────
    float vAdc  = (float)(mvAccum / count) / 1000.0f;
    float vBatt = vAdc * DIVIDER_RATIO;

    Serial.printf("[ADC] V_ADC: %.3fV (%s) | V_Batt: %.2fV | Samples: %d\n",
                  vAdc, cali_ok ? "kalibriert" : "unkalibriert", vBatt, count);

    mvAccum = 0;
    count   = 0;

    // ── Schwellen prüfen ──────────────────────────────────────
    if (vBatt <= POWER_OFF_MODE) {
        lowVoltageHits++;
        Serial.printf("[WARN] Unterspannung %d/%d\n", lowVoltageHits, LOW_CONFIRM_COUNT);
    } else {
        lowVoltageHits = 0;
        if (vBatt <= POWER_WARN_MODE) {
            Serial.println("[WARN] Batterie niedrig!");
        }
    }

    if (lowVoltageHits >= LOW_CONFIRM_COUNT) {
        Serial.println("[SLEEP] Batterie zu niedrig – Deep Sleep");
        esp_sleep_enable_timer_wakeup(4000000ULL);
        esp_deep_sleep_start();
    }
}