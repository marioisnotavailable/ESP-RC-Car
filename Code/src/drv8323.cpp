#include "drv8323.h"

// DRV8323 uses 16-bit frames: [15]=R/W, [14:11]=addr, [10:0]=data
// SPI mode per datasheet: mode 1, MSB first. Clock kept conservative at 1 MHz.

DRV8323::DRV8323(uint8_t csPin,
                 uint8_t enPin,
                 uint8_t faultPin,
                 uint8_t sclkPin,
                 uint8_t misoPin,
                 uint8_t mosiPin,
                 SPIClass &spiBus)
    : bus(spiBus),
      cs(csPin),
      en(enPin),
      fault(faultPin),
      sclk(sclkPin),
      miso(misoPin),
      mosi(mosiPin),
      clock(1000000) {}

void DRV8323::begin(uint32_t spiHz)
{
    clock = spiHz;

    pinMode(cs, OUTPUT);
    digitalWrite(cs, HIGH);

    pinMode(en, OUTPUT);
    digitalWrite(en, LOW); // keep in reset while SPI starts

    pinMode(fault, INPUT_PULLUP); // nFAULT is open drain

    bus.begin(sclk, miso, mosi, cs);
    delay(1);

    digitalWrite(en, HIGH); // wake the driver
    delay(2);
}

uint16_t DRV8323::transferFrame(uint16_t frame)
{
    bus.beginTransaction(SPISettings(clock, MSBFIRST, SPI_MODE1));
    digitalWrite(cs, LOW);
    uint16_t resp = bus.transfer16(frame);
    digitalWrite(cs, HIGH);
    bus.endTransaction();
    return resp & 0x07FF; // data portion only
}

uint16_t DRV8323::readRegister(uint8_t reg)
{
    return transferFrame(0x8000 | ((reg & 0x0F) << 11));
}

bool DRV8323::writeRegister(uint8_t reg, uint16_t value)
{
    transferFrame(((reg & 0x0F) << 11) | (value & 0x07FF));
    return true;
}

bool DRV8323::hasFault() const
{
    return digitalRead(fault) == LOW;
}

uint16_t DRV8323::readFault1()
{
    return readRegister(0x0);
}

uint16_t DRV8323::readFault2()
{
    return readRegister(0x1);
}

bool DRV8323::clearFaults()
{
    // Hardware clear: pulse EN low-high. This avoids guessing bitfields.
    digitalWrite(en, LOW);
    delayMicroseconds(10);
    digitalWrite(en, HIGH);
    delay(1);
    return true;
}
