#include <Arduino.h>
#include <SPI.h>

#define PIN_MOSI 6
#define PIN_MISO 5
#define PIN_SCLK 7
#define PIN_CS   15

#define PIN_ENABLE 16
#define PIN_PWM    18
#define PIN_FAULT  39

SPIClass spi = SPIClass(FSPI);

// SPI Transfer
uint16_t drvTransfer(uint16_t data)
{
  digitalWrite(PIN_CS, LOW);
  uint16_t rx = spi.transfer16(data);
  digitalWrite(PIN_CS, HIGH);
  return rx;
}

uint16_t drvRead(uint8_t addr)
{
  uint16_t frame = 0x8000 | (addr << 11);
  return drvTransfer(frame);
}

void drvWrite(uint8_t addr, uint16_t data)
{
  uint16_t frame = (addr << 11) | data;
  drvTransfer(frame);
}

void drvInit()
{
  uint16_t reg = 0;

  // 1 PWM Mode + Clear Fault
  reg |= (1 << 4);
  reg |= (1 << 0);

  drvWrite(0x02, reg);

  drvWrite(0x03, 0x0300);
  drvWrite(0x04, 0x0300);
  drvWrite(0x05, 0x0190);
  drvWrite(0x06, 0x0040);
}

void setup()
{
  Serial.begin(115200);

  pinMode(PIN_CS, OUTPUT);
  pinMode(PIN_ENABLE, OUTPUT);
  pinMode(PIN_PWM, OUTPUT);
  pinMode(PIN_FAULT, INPUT_PULLUP);

  digitalWrite(PIN_CS, HIGH);
  digitalWrite(PIN_ENABLE, LOW);

  spi.begin(PIN_SCLK, PIN_MISO, PIN_MOSI, PIN_CS);

  delay(100);

  digitalWrite(PIN_ENABLE, HIGH);

  drvInit();

  Serial.println("DRV8323 gestartet");

  Serial.print("FAULT1: ");
  Serial.println(drvRead(0x00), HEX);

  Serial.print("FAULT2: ");
  Serial.println(drvRead(0x01), HEX);
}

void loop()
{
  // PWM Test
  ledcAttachPin(PIN_PWM, 0);
  ledcSetup(0, 20000, 8);

  ledcWrite(0, 100);

  if (digitalRead(PIN_FAULT) == LOW)
  {
    Serial.println("DRV FAULT!");
  }

  delay(1000);
}