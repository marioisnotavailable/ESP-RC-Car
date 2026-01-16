#include <Arduino.h>
#include <FastLED.h>
void setup()
{
      const int ADC_UB = 39; // Pin für Spannungsmessung
  const float VOLTAGE_LEVEL = 3.3; // Referenzspannung des ADC
  const int R1 = 10000; // Widerstand R1 in Ohm
  const int R2 = 10000; // Widerstand R2 iöön Ohm
  const float POWER_WARN_MODE = 7.9; // Spannung für Warnung
  const float POWER_OFF_MODE = 7.4; // Spannung für Abschaltung
  const int WAITTIME = 10; // Wartezeit zwischen den Messungen in ms
  const int NUM_LEDS = 1; // Anzahl der LEDs

  static float newbatterie = 0;
  static int count = 0;
  static int batterie_low_cont = 0;

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
      SerialBT.print(newbatterie);
      SerialBT.println("V");
      if (newbatterie < POWER_WARN_MODE)
      {

        for (int i = 0; i < NUM_LEDS; i++)
        {
          leds[i] = CRGB::Red;
        }
        FastLED.show();
      }
      else
      {
        for (int i = 0; i < NUM_LEDS; i++)
        {
          leds[i] = CRGB::Green;
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
      leds[i] = CRGB::Black;
    }
    FastLED.show();
    ledcWrite(0, 0);
    ledcWrite(1, 0);
    esp_sleep_enable_timer_wakeup(4000000);
    esp_deep_sleep_start();
  }
}