#include "batterie.h"
#include <Arduino.h>
#include <FastLED.h>

// --- Konfiguration ---
static const int   ADC_UB          = 39;     // ADC-Pin für Spannungsmessung
static const float VOLTAGE_REF     = 3.3f;   // Referenzspannung des ADC
static const int   ADC_MAX         = 4095;   // 12-Bit ADC
static const int   R1              = 10000;  // Spannungsteiler R1 [Ohm]
static const int   R2              = 10000;  // Spannungsteiler R2 [Ohm]
static const float BAT_MAX_V       = 8.4f;   // 2S LiPo voll geladen  → 100 %
static const float BAT_MIN_V       = 7.0f;   // 2S LiPo entladen      →   0 %
static const float POWER_WARN_V    = 7.9f;   // LED-Warnschwelle
static const float POWER_OFF_V     = 7.4f;   // Abschaltschwelle
static const int   SAMPLE_COUNT    = 200;    // Messungen pro Mittelwert
static const int   SAMPLE_DELAY_MS  = 10;     // Pause zwischen zwei Messungen [ms]
static const int   NUM_LEDS         = 1;      // Anzahl NeoPixel-LEDs
static const uint64_t SLEEP_US      = 4000000ULL; // Tiefschlaf-Dauer nach leerer Batterie (4 s)

// --- Globale Variable (via batterie.h nach außen sichtbar) ---
int batterie_percent = -1;  // -1 = noch keine Messung

// --- Interne Variablen ---
static CRGB     leds[NUM_LEDS];
static float    batSum        = 0.0f;
static int      batCount      = 0;
static int      batLowCount   = 0;
static uint32_t lastSampleMs  = 0;

void batterie_init()
{
  FastLED.addLeds<NEOPIXEL, 2>(leds, NUM_LEDS);
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB::Black;
  FastLED.show();
}

void batterie_update()
{
  uint32_t now = millis();
  if (now - lastSampleMs < (uint32_t)SAMPLE_DELAY_MS) return;
  lastSampleMs = now;

  batSum += (float)analogRead(ADC_UB);
  batCount++;

  if (batCount >= SAMPLE_COUNT)
  {
    // Spannung berechnen
    float avgAdc  = batSum / (float)SAMPLE_COUNT;
    float voltage = (avgAdc / (float)ADC_MAX) * VOLTAGE_REF * ((float)(R1 + R2) / (float)R2);

    // Spannung in Prozent umrechnen (begrenzt auf 0–100)
    int pct = (int)(((voltage - BAT_MIN_V) / (BAT_MAX_V - BAT_MIN_V)) * 100.0f);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    batterie_percent = pct;

    // LED-Anzeige: Grün = OK, Rot = Warnung
    CRGB color = (voltage < POWER_WARN_V) ? CRGB::Red : CRGB::Green;
    for (int i = 0; i < NUM_LEDS; i++) leds[i] = color;
    FastLED.show();

    // Abschaltzähler
    if (voltage <= POWER_OFF_V) batLowCount++;
    else                        batLowCount = 0;

    batSum   = 0.0f;
    batCount = 0;
  }

  // Tiefschlaf bei anhaltend leerer Batterie
  if (batLowCount >= 3)
  {
    for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB::Black;
    FastLED.show();
    esp_sleep_enable_timer_wakeup(SLEEP_US);
    esp_deep_sleep_start();
  }
}