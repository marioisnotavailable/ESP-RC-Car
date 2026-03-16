#include <Arduino.h>
#include <FastLED.h>
#include <driver/gpio.h>

#define ADC_UB 2 // Pin für Spannungsmessung - GPIO2
#define VOLTAGE_LEVEL 3.3 // Referenzspannung des ADC
#define R1 1800000 // Widerstand R1 in Ohm
#define R2 1000000 // Widerstand R2 in Ohm
#define POWER_WARN_MODE 7.9 // Spannung für Warnung
#define POWER_OFF_MODE 7.4 // Spannung für Abschaltung
#define WAITTIME 10 // Wartezeit zwischen den Messungen in ms
#define NUM_LEDS 1 // Anzahl der LEDs
float newbatterie = 0;
int count = 0;
int batterie_low_cont = 0;

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("Setup started");
  
  // GPIO2 als reiner ADC-Input (kein Pull-Up/Down)
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (1ULL << 2);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);
  
  // ADC-Auflösung auf 12 Bit setzen
  analogReadResolution(12);

  //FastLED.addLeds<NEOPIXEL, 2>(leds, NUM_LEDS); // Pin für die LEDs

}

void loop()
{
  static unsigned long lastRead = 0;
  
  if (millis() - lastRead >= WAITTIME)
  {
    lastRead = millis();
    newbatterie += analogRead(ADC_UB);
    count++;
    if (count >= 200)
    {
      // ADC-Wert zu Spannung: (ADC/4095)*3.3 * ((R1+R2)/R2)
      float adc_avg = newbatterie / 200.0;
      float voltage = adc_avg * (VOLTAGE_LEVEL / 4095.0) * ((R1 + R2) / R2);
      
      Serial.print("ADC RAW: ");
      Serial.print(adc_avg);
      Serial.print(" | Spannung: ");
      Serial.print(voltage);
      Serial.println("V");
      
      if (voltage <= POWER_OFF_MODE)
      {
        batterie_low_cont++;
      }
      else
      {
        batterie_low_cont = 0;
      }
      
      // Deep Sleep nur wenn 3x niedrig
      if (batterie_low_cont >= 3)
      {
        Serial.println("BATTERIE ZU NIEDRIG - Deep Sleep aktiviert");
        //esp_sleep_enable_timer_wakeup(4000000);
        //esp_deep_sleep_start();
      }
      
      count = 0;
      newbatterie = 0;
    }
  }
}