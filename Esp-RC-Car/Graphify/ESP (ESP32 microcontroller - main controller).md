---
source_file: "Esp-RC-Car/Graphify/ESP (ESP32 microcontroller - main controller).md"
type: "concept"
community: "Power & Control Schematic"
tags:
  - graphify/concept
  - graphify/EXTRACTED
  - community/Power__Control_Schematic
---

# ESP (ESP32 microcontroller - main controller)

## Connections
- [[Battery info line (mb Info ueber Batterie)]] - `shares_data_with` [INFERRED]
- [[Buck converter (5V12V on I2C line)]] - `shares_data_with` [EXTRACTED]
- [[ESC (Electronic Speed Controller for motor)]] - `shares_data_with` [EXTRACTED]
- [[I2C bus (5V12V rail)]] - `shares_data_with` [INFERRED]
- [[PWM signal line to servo]] - `implements` [EXTRACTED]
- [[PWMSPI signal line to ESC]] - `implements` [EXTRACTED]
- [[SchaltungSkizze - Hand-drawn circuit sketch]] - `references` [EXTRACTED]
- [[Servo (steering servo, driven by PWM)]] - `shares_data_with` [EXTRACTED]

#graphify/concept #graphify/EXTRACTED #community/Power__Control_Schematic