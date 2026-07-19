import time
from machine import Pin, I2C
from i2c_lcd import I2cLcd
from rotary_encoder import RotaryEncoder

# Define the default I2C hardware pins for ESP32
SDA_PIN = Pin(21)
SCL_PIN = Pin(22)

# Common default I2C addresses for LCD2004 modules are 0x27 or 0x3F
I2C_ADDR = 0x27
TOTAL_ROWS = 4
TOTAL_COLS = 20

# Initialize hardware I2C bus 0 (GPIO21/22 are its default pins on the ESP32).
# Use SoftI2C(scl=..., sda=...) instead only if you need bit-banged I2C on
# other pins.
i2c = I2C(0, scl=SCL_PIN, sda=SDA_PIN, freq=100000)

# --- Rotary encoders ---
# Each RotaryEncoder owns its own pins and count. Add more by instantiating
# another one with a different A/B pin pair, e.g.:
#   enc2 = RotaryEncoder(pin_a=18, pin_b=19)
# Avoid GPIO2 for A/B: it is a strapping pin tied to the onboard LED/pulldown.
enc1 = RotaryEncoder(pin_a=5, pin_b=4)                      # A=CLK, B=DT
enc2 = RotaryEncoder(pin_a=18, pin_b=19, reverse=True)     # A=CLK, B=DT

# Independent push-button (active-low): wire one leg to GPIO23, the other to GND.
# A press resets both encoder counters.
RESET_BTN = 23
reset_btn = Pin(RESET_BTN, Pin.IN, Pin.PULL_UP)
_btn_prev = reset_btn.value()
_btn_t = time.ticks_ms()


def reset_pressed(debounce_ms=50):
    """Return True once per debounced press (falling edge), else False."""
    global _btn_prev, _btn_t
    val = reset_btn.value()
    if val != _btn_prev and time.ticks_diff(time.ticks_ms(), _btn_t) > debounce_ms:
        _btn_t = time.ticks_ms()
        _btn_prev = val
        return val == 0        # active-low: 0 = just pressed
    return False


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
    lcd.putstr("Rotary encoder:")

    time.sleep(1)

    # Simple loop demonstrating cursor control & text updates
    while True:
        # The independent push-button resets BOTH counters
        if reset_pressed():
            enc1.reset()
            enc2.reset()

        lcd.move_to(0, 2)
        lcd.putstr(f"Enc1: {enc1.value():4d}")
        lcd.move_to(0, 3)
        lcd.putstr(f"Enc2: {enc2.value():4d}")
        time.sleep_ms(50)


except Exception as e:
    print("Error initializing LCD display:", e)
