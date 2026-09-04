# PicoBricks DOOM

A highly customized, fully playable port of **DOOM** explicitly engineered for the [PicoBricks](https://picobricks.com/) hardware ecosystem (Raspberry Pi RP2040). 

Based on the incredible [Smallest-Doom](https://github.com/carlk3/Smallest-Doom) by James Brown, this fork rips out the standard SPI interface and keyboard controls and physically maps DOOM directly to the PicoBricks modules.

## 🚀 Hardware Features

- **I2C 128x64 OLED (GP4 / GP5):** Bypasses the original SPI grayscale dithering and blasts a pure 1-bit frame buffer directly over a 400kHz I2C bus to the PicoBricks OLED display.
- **Potentiometer Steering (GP26 / ADC0):** Steer DOOM Guy smoothly left and right using the rotary potentiometer. Engineered with a massive 50% physical deadzone to guarantee perfect straight walking when resting.
- **Multi-Click Action Button (GP10):** All combat actions are mapped to a custom 250ms multi-click state machine on a single button:
  - **1 Tap:** Fire Weapon
  - **2 Taps:** Open Doors / Use
  - **3 Taps:** Switch Weapon
  - **Hold:** Brake (Stop walking)
- **Haptic Relay Damage Feedback (GP9):** The physical onboard Relay is mapped to DOOM Guy's health pool. Whenever you take damage, the relay physically clacks for 100ms!
- **Auto-Walk Momentum:** DOOM Guy perpetually glides forward at exactly 50% speed. We achieve this by pulsing the physics engine at a 33ms 50% duty cycle, utilizing DOOM's heavy momentum physics to create a smooth, menacing walk instead of an erratic sprint.

## 🛠 Building & Flashing

This project builds using the standard Raspberry Pi Pico SDK.

```bash
mkdir build && cd build
cmake ..
make -j4
```
*Drag and drop the generated `doom_complete_usb.uf2` file onto your Pico while holding the BOOTSEL button, or flash via the Raspberry Pi Debug Probe.*

## 📸 Boot Screen

On boot, the game will halt execution and render a custom 2x scaled splash screen directly to the OLED outlining the controls. Press the action button to drop straight into Hell.

---
*Forked and maintained by Natanael Fejes*
