#include <Arduino.h>
#include <FastLED.h>

#define ADC_UB 39          // Pin fuer Spannungsmessung
#define VOLTAGE_LEVEL 3.3  // Referenzspannung des ADC
#define R1 10000           // Widerstand R1 in Ohm
#define R2 10000           // Widerstand R2 in Ohm
#define POWER_WARN_MODE 7.9 // Spannung fuer Warnung
#define POWER_OFF_MODE 7.4  // Spannung fuer Abschaltung
#define WAITTIME 10        // Wartezeit zwischen den Messungen in ms
#define NUM_LEDS 1         // Anzahl der LEDs

float newbatterie = 0;
int count = 0;
int batterie_low_cont = 0;

void setup()
{
  Serial.begin(115200);

  //FastLED.addLeds<NEOPIXEL, 2>(leds, NUM_LEDS); // Pin für die LEDs

}

void loop()
{

if (millis() % WAITTIME == 0)
  {
    newbatterie += analogRead(ADC_UB);
    count++;
    if (count >= 200)
    {
      newbatterie = newbatterie / 200 * (VOLTAGE_LEVEL * (R2 + R1) / R2);
      Serial.print(newbatterie);
      Serial.println("V");
      if (newbatterie < POWER_WARN_MODE)
      {

        for (int i = 0; i < NUM_LEDS; i++)
        {
          //leds[i] = CRGB::Red;
        }
        FastLED.show();
      }
      else
      {
        for (int i = 0; i < NUM_LEDS; i++)
        {
          //leds[i] = CRGB::Green;
        }
        FastLED.show();
      }
      if (newbatterie <= POWER_OFF_MODE)
      {
        batterie_low_cont++;
      }
      else
      {
        batterie_low_cont = 0;
      }
      count = 0;
      newbatterie = 0;
    }
  }
  if (batterie_low_cont >= 3)
  {
    for (int i = 0; i < NUM_LEDS; i++)
    {
      //leds[i] = CRGB::Black;
    }
    FastLED.show();
    ledcWrite(0, 0);
    ledcWrite(1, 0);
    esp_sleep_enable_timer_wakeup(4000000);
    esp_deep_sleep_start();
  }
}