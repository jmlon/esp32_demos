# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

A collection of standalone ESP32 hardware demos. There is **no build system, package manager, or test suite** — each demo is flashed to a physical board and observed. Two independent toolchains coexist:

- **Arduino sketches** (`.ino`, at repo root) — compiled/flashed with the Arduino IDE.
- **MicroPython scripts** (`micropython/`) — copied onto a board already running the MicroPython firmware.

Each subdirectory is a self-contained example; there are no shared modules across Arduino sketches. Do not try to "build the project" as a whole.

## Target boards and per-sketch configuration

Sketches target different ESP32 variants (plain ESP32, ESP32-C3, ESP32-S3), and the **correct pin numbers, board type, and Arduino IDE settings differ per variant**. This is the single most important thing to get right.

- The required Arduino IDE settings (Board, USB CDC On Boot, Upload Mode, USB Mode, Flash size) and required libraries are documented in the **comment header at the top of each `.ino` file** — read it before changing anything. E.g. HID/joystick demos require `USB Mode: USB-OTG (TinyUSB)`; serial-only demos use `Hardware CDC and JTAG`.
- Pinout reference images for each variant live at the repo root (`esp32-c3-devkitm-1-*.jpg`, `esp32-s3_devkitc-1_*.jpg`, `esp32-ideaspark-7789.jpg`).
- Common GPIO gotcha: the built-in LED is GPIO 2 on plain ESP32, GPIO 8 on ESP32-C3, GPIO 48 on ESP32-S3 (see `micropython/blink.py`). Default I2C pins used here are SDA=21, SCL=22.
- External Arduino libraries used: `RotaryEncoder` (Matthias Hertel), `Adafruit GFX` + `Adafruit ST7735 & ST7789`, `Joystick_ESP32S2`.

## MicroPython workflow

Scripts under `micropython/` run on MicroPython v1.28 (tested on ESP-WROOM-32). Copy a script to the board's filesystem and run it at the REPL, or name it `main.py` to autostart on boot. REPL controls (from `micropython/README.md`): `Ctrl-C` stop, `Ctrl-D` soft reboot.

- `arduino.py` is a small shim that emulates the Arduino API (`pin_mode`, `digital_write`, `delay`, `millis`, `start(setup, loop)`) on top of MicroPython's `machine` module — used so Arduino-style sketches can be ported directly.
- `i2c-addresses.py` scans the I2C bus and prints device addresses — run this first when wiring up a new I2C peripheral.
- `display/` holds an HD44780 character-LCD driver over I2C (PCF8574 backpack): `lcd_api.py` (hardware-agnostic HD44780 command layer) → `i2c_lcd.py` (`I2cLcd`, the PCF8574 HAL) → `lcd1602_i2c.py` (the runnable example). All three files must be copied to the board together. LCD1602 modules are typically at I2C address `0x27` or `0x3F`.

## Demo inventory

- `hid_demo/`, `g1000_knob/` — USB HID (keyboard / joystick) on ESP32-S3.
- `rotary_encoder/`, `rotary_encoder_display/` — dual concentric rotary encoders, the latter with an ST7789 display.
- `ideaspark_7789/` — ST7789 TFT graphics test.
