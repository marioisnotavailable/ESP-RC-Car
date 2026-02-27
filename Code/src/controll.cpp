#include <Servo.h>

Servo esc;

//pins für die Ansteuerung des Motors bestimmen

// Werte in Mikrosekunden (typisch RC-ESC)
const int MIN_US = 1000;   // Stop / Minimum
const int ARM_US = 1000;   // Arming-Signal
const int START_US = 1100; // leichter Start
const int MAX_US = 1600;   // vorsichtig, zum Test reicht das

void setup() {
  Serial.begin(9600);
  esc.attach(ESC_PIN);

  // Arming: ESC braucht meistens ein paar Sekunden "min throttle"
  esc.writeMicroseconds(ARM_US);
  delay(3000);

  // kurzer "Startkick"
  esc.writeMicroseconds(START_US);
  delay(1000);
}

void loop() {
  // langsam hochfahren
  for (int us = START_US; us <= MAX_US; us += 5) {
    esc.writeMicroseconds(us);
    delay(50);
  }

  delay(1000);

  // langsam runterfahren
  for (int us = MAX_US; us >= MIN_US; us -= 5) {
    esc.writeMicroseconds(us);
    delay(50);
  }

  delay(2000);
}