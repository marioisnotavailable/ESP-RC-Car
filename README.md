# ESP-RC-Car

A self-engineered, open-source drift car powered by an ESP32-S3 microcontroller, designed for a school project. This project integrates robust battery management features, a high-performance mini brushless motor, and a custom phone app controller.

---

## Features

- **ESP32-S3 MCU:** Efficient, reliable microcontroller for all car logic and communication
- **USB Power Delivery & Battery Management:**  
  - Includes Battery Management System (BMS), charging IC, and balancing ICs for battery protection and health
- **Mini1410 2500kv Motor:**  
  - Driven by DRV8323SRTAR motor driver
- **Custom Controller App:**  
  - Written in Flutter, communicates with the car via WebSocket
  - Source code and Android/iOS builds in [`App/final_build`](./App/final_build)
- **PCB Design:**  
  - Schematics and PCB layout created in Altium, available in the repo
- **Firmware:**  
  - PlatformIO based, located in the repo

---

## Hardware Requirements

- ESP32-S3 microcontroller
- Mini1410 2500kv brushless motor
- DRV8323SRTAR motor driver
- USB PD hardware (for charging)
- Battery pack with BMS, charging, and balancing ICs
- Chassis (3D print file provided; other car body prints must be purchased separately)
- Additional electronics (as defined in PCB files)

---

## Getting Started

### Clone the Repo

```bash
git clone https://github.com/marioisnotavailable/ESP-RC-Car.git
```

### Firmware

- Code for the ESP32-S3 is organized for PlatformIO.  
- Configure and build per PlatformIO requirements.

### Controller App

- Find source and builds under [`App/final_build`](./App/final_build)
- Flutter-based; can build for Android or iOS as needed

---

## PCB & Schematics

- All Altium files (schematics and board) are included for public use.

---

## 3D Prints

- Chassis STL provided in this repo
- All other 3D printable parts must be purchased

---

## License

Open source; no specific license chosen at this time.

---

## Media

**[Demo Image Placeholder]**  
*Insert image or GIF of assembled car here*  
**[Demo Video Placeholder]**  
*Insert video or link to demonstration here*

---

## Contributing

Pull requests welcome! If you have ideas for improvements or new features, feel free to submit.

---

## Acknowledgments

This project is open to the community—no personal or institutional credits required.

---

```
**Please update this README with build instructions, usage notes, or troubleshooting tips as the project evolves!**
```