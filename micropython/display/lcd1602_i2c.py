"""Demo: drive a 16x2 HD44780 character LCD over I2C from an ESP32.

Wiring: LCD I2C backpack (PCF8574) on SDA=GPIO21, SCL=GPIO22.

Driver stack (all three files must be on the board together):
    lcd1602_i2c.py  -- this runnable demo
    i2c_lcd.py      -- I2cLcd: the PCF8574 I2C transport (HAL)
    lcd_api.py      -- LcdApi: hardware-agnostic HD44780 command layer

If nothing appears, run i2c-addresses.py to confirm the module's address
(commonly 0x27 or 0x3F) and set I2C_ADDR accordingly.
"""

import time
from machine import Pin, I2C
from i2c_lcd import I2cLcd

# Define the default I2C hardware pins for ESP32
SDA_PIN = Pin(21)
SCL_PIN = Pin(22)

# Common default I2C addresses for LCD1602 modules are 0x27 or 0x3F
I2C_ADDR = 0x27
TOTAL_ROWS = 2
TOTAL_COLS = 16

# Initialize hardware I2C bus 0 (GPIO21/22 are its default pins on the ESP32).
# Use SoftI2C(scl=..., sda=...) instead only if you need bit-banged I2C on
# other pins.
i2c = I2C(0, scl=SCL_PIN, sda=SDA_PIN, freq=100000)

try:
    # Initialize the LCD object
    lcd = I2cLcd(i2c, I2C_ADDR, TOTAL_ROWS, TOTAL_COLS)

    # Clear any leftover artifacts on screen
    lcd.clear()

    # Write message to the first line (Row 0, Column 0)
    lcd.move_to(0, 0)
    lcd.putstr("MicroPython!")

    # Write message to the second line (Row 1, Column 0)
    lcd.move_to(0, 1)
    lcd.putstr("ESP32 LCD1602")

    time.sleep(3)

    # Simple loop demonstrating cursor control & text updates
    while True:
        lcd.clear()
        lcd.putstr("System Status:")
        lcd.move_to(0, 1)
        lcd.putstr("Running...")
        time.sleep(2)

        lcd.clear()
        lcd.putstr("System Status:")
        lcd.move_to(0, 1)
        lcd.putstr("Standby")
        time.sleep(2)

except Exception as e:
    print("Error initializing LCD display:", e)
