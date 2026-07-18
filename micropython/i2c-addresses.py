"""Scan the I2C bus and print the address of every connected device.

Target: ESP32 with the default I2C pins (GPIO21=SDA, GPIO22=SCL). Run this to
confirm a sensor or display is wired correctly before using it -- e.g. an
LCD1602 backpack typically shows up at 0x27 or 0x3F.
"""

import machine

sdaPIN = machine.Pin(21)  # SDA -- default I2C pin on ESP32 (change for C3/S3)
sclPIN = machine.Pin(22)  # SCL -- default I2C pin on ESP32 (change for C3/S3)

# 10 kHz is slow but tolerant of long/breadboard wiring; raise to 100000
# (100 kHz standard) or 400000 (fast mode) for normal use.
#
# Use hardware I2C bus 0 (GPIO21/22 are its default pins on the ESP32).
i2c = machine.I2C(0, sda=sdaPIN, scl=sclPIN, freq=10000)
# Software (bit-banged) fallback -- use this when the hardware I2C pins aren't
# available and you need I2C on arbitrary GPIOs:
# i2c = machine.SoftI2C(sda=sdaPIN, scl=sclPIN, freq=10000)

devices = i2c.scan()
if len(devices) == 0:
    print("No i2c device !")
else:
    print('i2c devices found:', len(devices))
    for device in devices:
        print("At address: ", hex(device))
