#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_STUSB4500.h>

STUSB4500 usb;

// SDA/SCL entsprechend deiner Verdrahtung
static const int I2C_SDA = 13;
static const int I2C_SCL = 14;
static const uint8_t STUSB4500_ADDR = 0x28;

void setup()
{
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println("\nSTUSB4500 PDO programming");

  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  delay(200);

  if (!usb.begin(STUSB4500_ADDR, Wire)) {
    Serial.println("ERROR: Cannot connect to STUSB4500 (0x28).");
    while (1) delay(1000);
  }
5
  Serial.println("Connected.");

  // --------- PDO1 = 5V ----------
  usb.setVoltage(1, 5.0f);     // Volt
  usb.setCurrent(1, 3.0f);     // Ampere (setz hier was dein Netzteil kann)

  // --------- PDO2 bleibt wie es ist ----------

  // --------- PDO3 = 12V ----------
  usb.setVoltage(3, 12.0f);    // Volt
  usb.setCurrent(3, 3.0f);     // Ampere

  usb.setPdoNumber(3);  // Anzahl der PDOs auf 3 setzen

  // Änderungen in EEPROM speichern
  usb.write();

  Serial.println("PDOs updated. Please re-plug USB-C power to renegotiate.");
}

void loop()
{
}
