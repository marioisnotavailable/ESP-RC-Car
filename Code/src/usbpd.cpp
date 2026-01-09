#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_STUSB4500.h>

STUSB4500 usb;

// SDA/SCL entsprechend deiner Verdrahtung
static const int I2C_SDA = 13;
static const int I2C_SCL = 14;
static const uint8_t STUSB4500_ADDR = 0x28;
static const uint8_t PORT_STATUS_REG = 0x0E; // Provides POWER_OK pin state

uint8_t readPortStatus()
{
  Wire.beginTransmission(STUSB4500_ADDR);
  Wire.write(PORT_STATUS_REG);
  if (Wire.endTransmission(false) != 0) return 0xFF; // 0xFF indicates bus error

  if (Wire.requestFrom(STUSB4500_ADDR, static_cast<uint8_t>(1)) != 1) return 0xFF;
  return Wire.read();
}

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

  Serial.println("Connected.");

  // --------- PDO1 = 5V ----------
  usb.setVoltage(1, 5.0f);     // Volt
  usb.setCurrent(1, 3.0f);     // Ampere (setz hier was dein Netzteil kann)

  // --------- PDO2 bleibt wie es ist ----------

  // --------- PDO3 = 12V ----------
  usb.setVoltage(3, 12.0f);    // Volt
  usb.setCurrent(3, 3.0f);     // Ampere

  // Änderungen in EEPROM speichern
  usb.write();

  Serial.println("PDOs updated. Please re-plug USB-C power to renegotiate.");
}

void loop()
{
  uint8_t status = readPortStatus();
  if (status == 0xFF) {
    Serial.println("PORT_STATUS read failed");
  } else {
    bool pdo3Active = (status & 0x04) == 0;
    bool pdo2Active = (status & 0x02) == 0;
    bool pdo1Active = (status & 0x01) == 0;

    Serial.printf("PORT_STATUS=0x%02X -> PDO3:%s PDO2:%s PDO1:%s\n",
                  status,
                  pdo3Active ? "active" : "inactive",
                  pdo2Active ? "active" : "inactive",
                  pdo1Active ? "active" : "inactive");
  }

  delay(1000);
}