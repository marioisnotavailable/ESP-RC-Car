#pragma once

#include <Arduino.h>
#include <SPI.h>

// Minimal SPI helper for the DRV8323S (gate driver)
class DRV8323 {
public:
    DRV8323(uint8_t csPin,
            uint8_t enPin,
            uint8_t faultPin,
            uint8_t sclkPin,
            uint8_t misoPin,
            uint8_t mosiPin,
            SPIClass &spiBus = SPI);

    // Initialize pins and SPI; keeps driver in reset briefly, then wakes it
    void begin(uint32_t spiHz = 1000000);

    // Raw register access (11-bit data)
    uint16_t readRegister(uint8_t reg);
    bool     writeRegister(uint8_t reg, uint16_t value);

    // Fault helpers
    bool     hasFault() const;
    uint16_t readFault1();
    uint16_t readFault2();

    // Clears latched faults by pulsing EN low-high
    bool clearFaults();

private:
    uint16_t transferFrame(uint16_t frame);

    SPIClass &bus;
    uint8_t   cs;
    uint8_t   en;
    uint8_t   fault;
    uint8_t   sclk;
    uint8_t   miso;
    uint8_t   mosi;
    uint32_t  clock;
};
