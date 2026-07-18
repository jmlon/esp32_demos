import time
from machine import Pin, I2C,ADC
from i2c_lcd import I2cLcd
import sys

# Define the default I2C hardware pins for ESP32
SDA_PIN = Pin(21)
SCL_PIN = Pin(22)

# Potentiometer on pin 33
adc = ADC(Pin(33))
# Attenuation sets the input voltage range (12-bit ADC -> 0..4095 counts).
# The higher the attenuation, the larger the measurable range, but the ADC is
# only reasonably linear in the middle of each range (it floors near 0V and
# clips near full scale). Available settings:
#   ADC.ATTN_0DB    ~0 - 1.1V  (no attenuation, best resolution, small range)
#   ADC.ATTN_2_5DB  ~0 - 1.5V
#   ADC.ATTN_6DB    ~0 - 2.2V
#   ADC.ATTN_11DB   ~0 - 3.3V  (full range; the 11dB name is kept for
#                               compatibility, newer chips may call it ATTN_12DB)
adc.atten(ADC.ATTN_11DB)    # Full 0-3.3V range

# Common default I2C addresses for LCD1602 modules are 0x27 or 0x3F
I2C_ADDR = 0x27
TOTAL_ROWS = 4
TOTAL_COLS = 20

# Initialize hardware I2C bus 0 (GPIO21/22 are its default pins on the ESP32).
# Use SoftI2C(scl=..., sda=...) instead only if you need bit-banged I2C on
# other pins.
i2c = I2C(0, scl=SCL_PIN, sda=SDA_PIN, freq=100000)

try:
    # Initialize the LCD object
    lcd = I2cLcd(i2c, I2C_ADDR, TOTAL_ROWS, TOTAL_COLS)

    # Clear any leftover artifacts on screen
    lcd.clear()

    # Write centered title on the first line (Row 0)
    title = "MicroPython!" + str(sys.implementation[1][0])+"."+str(sys.implementation[1][1])+"."+str(sys.implementation[1][2])
    lcd.move_to((TOTAL_COLS - len(title)) // 2, 0)
    lcd.putstr(title)

    # Write message to the second line (Row 1, Column 0)
    lcd.move_to(0, 1)
    lcd.putstr("Analog input:")

    time.sleep(1)

    # read_uv() applies the ESP32's factory eFuse calibration and returns
    # microvolts, which is far more accurate at the low end than read().
    # It isn't supported on every chip/firmware (it can raise
    # ESP_ERR_INVALID_ARG when the calibration scheme is unavailable), so
    # probe it once and fall back to a plain linear conversion of the raw
    # count if it's not usable.
    try:
        adc.read_uv()
        have_read_uv = True
    except Exception:
        have_read_uv = False

    # Simple loop demonstrating cursor control & text updates
    while True:
        raw = adc.read()
        if have_read_uv:
            millivolts = adc.read_uv() // 1000
        else:
            # Uncalibrated: scale the 12-bit count over the ~3.3V range.
            # Note this inherits the raw ADC's low-end offset and clipping.
            millivolts = raw * 3300 // 4095
        # Print on rows 2 & 3, padded so old (longer) readings are cleared
        lcd.move_to(0, 2)
        lcd.putstr("{:<20}".format("Raw:  {}".format(raw)))
        lcd.move_to(0, 3)
        lcd.putstr("{:<20}".format("mV:   {}".format(millivolts)))
        time.sleep(0.25)


except Exception as e:
    print("Error initializing LCD display:", e)
