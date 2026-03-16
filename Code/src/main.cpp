#include <Arduino.h>
#include <FastLED.h>
#include <driver/adc.h>

#define ADC_UB 2          // Pin fuer Spannungsmessung
#define VOLTAGE_LEVEL 3.3  // Referenzspannung des ADC
#define R1 1800000           // Widerstand R1 in Ohm
#define R2 1000000           // Widerstand R2 in Ohm
#define POWER_WARN_MODE 7.9 // Spannung fuer Warnung
#define POWER_OFF_MODE 7.4  // Spannung fuer Abschaltung
#define WAITTIME 10        // Wartezeit zwischen den Messungen in ms
#define ADC_MAX 4095.0     // 12-bit ADC Maximalwert
#define FILTER_ALPHA 0.15  // Exponentialfilter fuer stabilere Anzeige
#define ADC_BURST_SAMPLES 64  // Mehrfachmessung pro Zyklus fuer ruhige Werte
#define ADC_BURST_DELAY_US 100
#define STARTUP_GRACE_MS 10000 // Zeit nach Boot ohne Auto-Abschaltung
#define MIN_VALID_ADC 20.0     // Unterhalb ist Messung wahrscheinlich nicht verbunden
#define MIN_VALID_BATTERY_V 5.0 // Unterhalb ist bei 2S Setup die Messung ungueltig
#define NUM_LEDS 1         // Anzahl der LEDs

float newbatterie = 0;
float batterie_filtered = 0;
bool filter_initialized = false;
uint32_t adc_sum = 0;
int count = 0;
int batterie_low_cont = 0;
unsigned long last_sample_ms = 0;
bool battery_measurement_valid = false;
adc1_channel_t adc_channel = ADC1_CHANNEL_1; // GPIO2 on ESP32-S3

static bool initAdc()
{
  if (adc1_config_width(ADC_WIDTH_BIT_12) != ESP_OK)
  {
    return false;
  }

  if (adc1_config_channel_atten(adc_channel, ADC_ATTEN_DB_11) != ESP_OK)
  {
    return false;
  }

  return true;
}

static int readAdcBurstMean(float *out_mean, int *out_samples)
{
  uint64_t local_sum = 0;

  for (int i = 0; i < ADC_BURST_SAMPLES; i++)
  {
    int raw = adc1_get_raw(adc_channel);
    if (raw >= 0)
    {
      local_sum += (uint32_t)raw;
    }
    delayMicroseconds(ADC_BURST_DELAY_US);
  }

  *out_mean = (float)local_sum / (float)ADC_BURST_SAMPLES;
  *out_samples = ADC_BURST_SAMPLES;
  return 1;
}

void setup()
{
  Serial.begin(115200);
  if (!initAdc())
  {
    Serial.println("ADC init failed");
  }
  delay(200);

  esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
  Serial.print("Wakeup cause: ");
  Serial.println((int)wakeup_cause);

  //FastLED.addLeds<NEOPIXEL, 2>(leds, NUM_LEDS); // Pin für die LEDs

}

void loop()
{

  unsigned long now = millis();
  if (now - last_sample_ms >= WAITTIME)
  {
    last_sample_ms = now;
    float adc_chunk_mean = 0;
    int adc_chunk_samples = 0;

    if (readAdcBurstMean(&adc_chunk_mean, &adc_chunk_samples))
    {
      adc_sum += (uint32_t)(adc_chunk_mean * adc_chunk_samples);
      count += adc_chunk_samples;
    }

    if (count >= 200)
    {
      float adc_avg = (float)adc_sum / (float)count;
      newbatterie = (adc_avg / ADC_MAX) * (VOLTAGE_LEVEL * (R2 + R1) / R2);
      battery_measurement_valid = (adc_avg > MIN_VALID_ADC) && (newbatterie > MIN_VALID_BATTERY_V);

      if (!filter_initialized)
      {
        batterie_filtered = newbatterie;
        filter_initialized = true;
      }
      else
      {
        batterie_filtered = (1.0 - FILTER_ALPHA) * batterie_filtered + FILTER_ALPHA * newbatterie;
      }

      Serial.print(batterie_filtered, 2);
      Serial.print("V (raw ");
      Serial.print(newbatterie, 2);
      Serial.print("V, adc ");
      Serial.print(adc_avg, 1);
      Serial.println(")");

      if (!battery_measurement_valid)
      {
        Serial.println("Battery reading invalid -> no auto power-off");
      }

      if (batterie_filtered < POWER_WARN_MODE)
      {

        for (int i = 0; i < NUM_LEDS; i++)
        {
          //leds[i] = CRGB::Red;
        }
        //FastLED.show();
      }
      else
      {
        for (int i = 0; i < NUM_LEDS; i++)
        {
          //leds[i] = CRGB::Green;
        }
        //FastLED.show();
      }
      if (battery_measurement_valid && batterie_filtered <= POWER_OFF_MODE)
      {
        batterie_low_cont++;
      }
      else
      {
        batterie_low_cont = 0;
      }
      count = 0;
      adc_sum = 0;
    }
  }
  if (millis() > STARTUP_GRACE_MS && battery_measurement_valid && batterie_low_cont >= 3)
  {
    for (int i = 0; i < NUM_LEDS; i++)
    {
      //leds[i] = CRGB::Black;
    }
    //FastLED.show();
    //ledcWrite(0, 0);
    //ledcWrite(1, 0);
    esp_sleep_enable_timer_wakeup(4000000);
    esp_deep_sleep_start();
  }
}