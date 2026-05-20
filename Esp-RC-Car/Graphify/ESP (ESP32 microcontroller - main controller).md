---
source_file: "SchaltungSkizze.png"
type: "concept"
community: "Community None"
tags:
  - graphify/concept
  - graphify/EXTRACTED
  - community/Community_None
---

# ESP (ESP32 microcontroller - main controller)

## Connections
- [[Battery info line (annotation 'mb Info ueber Batterie' - possibly battery info to ESP)]] - `shares_data_with` [INFERRED]
- [[Buck converter (steps down voltage, labeled 5V12V on I2C line)]] - `shares_data_with` [EXTRACTED]
- [[ESC (Electronic Speed Controller for motor)]] - `shares_data_with` [EXTRACTED]
- [[I2C bus (5V12V rail annotation, communication line)]] - `shares_data_with` [INFERRED]
- [[PWM signal line to servo]] - `implements` [EXTRACTED]
- [[PWMSPI signal line to ESC]] - `implements` [EXTRACTED]
- [[SchaltungSkizze - Hand-drawn circuit sketch of ESP-RC-Car power and control architecture]] - `references` [EXTRACTED]
- [[Servo (steering servo, driven by PWM)]] - `shares_data_with` [EXTRACTED]

#graphify/concept #graphify/EXTRACTED #community/Community_None