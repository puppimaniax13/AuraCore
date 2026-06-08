# AuraCore

![Project Header](placeholder.jpg)

A full-stack hardware and software solution to replace static OEM rear reflectors with addressable, reactive LED modules. This project features a modular PCB designed for the VB chassis but adaptable to any 12V automotive lighting application.

## 🚀 Technical Overview
- **Mechanical:** Custom 3D-printed housings (Fusion 360) designed for OEM fitment.
- **Electrical:** Dual-layer custom PCB (KiCad) with 12V->5V buck regulation and signal filtering.
- **Software:** C++ Firmware (Arduino) utilizing **FastLED** for sequential animations and interrupt-driven brake logic.

## 🛠️ Hardware Stack
- **MCU:** ESP-32-S3-WROOM
- **LEDs:** WS2812B Addressable Strips
- **Design Tools:** KiCad 9.0, Autodesk Fusion 360, Adobe Lightroom (Documentation)
- **3D Printer:** Bambu Labs P1S, PLA & PETG

## 📂 Repository Structure
- `/Firmware/AuraCore/AuraCore.ino`: Core firmware (LED logic, API routes)
- `/Firmware/AuraCore/data/index.html`: Web UI — edit this to change the app
- `/Hardware`: KiCad project files & PCB Schematics
- `/Media`: High-res project photography

## 🛠️ Development Setup

### One-time installs
- **Arduino ESP32 LittleFS Uploader** — required to push `data/index.html` to the device.
  Install: download the latest `.jar` from the [arduino-esp32fs-plugin releases](https://github.com/lorol/arduino-esp32fs-plugin/releases), place it in `<Arduino>/tools/ESP32FS/tool/`, restart Arduino IDE. It will appear under **Tools → ESP32 LittleFS Data Upload**.
- **Partition scheme**: in Arduino IDE board settings, set **Partition Scheme** to `Default 4MB with SPIFFS` (or any scheme that includes a SPIFFS/data partition).

### Changing the UI vs. changing firmware logic

| What you're changing | What to do |
|---|---|
| UI (colors, layout, new controls) | Edit `data/index.html` → **Tools → ESP32 LittleFS Data Upload** |
| LED logic, new signals, new routes | Edit `AuraCore.ino` → normal compile + flash |
| Both | Flash firmware first, then upload LittleFS |

### OTA updates (no USB required)
1. Compile firmware → find `AuraCore.bin` in your Arduino build output folder
2. Connect phone to `AURA_CORE` WiFi → open the app
3. Tuner → **SELECT FIRMWARE .BIN** → pick the file → watch progress bar
4. Device reboots into new firmware; all NVS settings (colors, modes) are preserved

## 🖨️ 3D Printing
- When using the provided *.3mf* files please try not to adjust much.
- The filament used for the lens is as follows
  - Overture Transparent PETG
  - Bambu Translucent Grey PETG
  - Overture Transparent Red PETG

## ⚖️ License
Code/Firmware: This project is licensed under the **GNU GPLv3**. You are free to use and modify it, but any derivative works must also be open-sourced under the same license.

Hardware & Design (PCB/3D Models): Licensed under CC BY-NC-SA 4.0. Personal use is encouraged; commercial use or resale of these designs is strictly prohibited without prior authorization.

*Commercial use or resale of these designs is strictly prohibited.*
