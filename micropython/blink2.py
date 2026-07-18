"""Blink demo structured like an Arduino sketch (setup() + loop()).

Shows the Arduino lifecycle in plain MicroPython and uses lightsleep() instead
of time.sleep() to cut power draw between toggles. Targets a standard ESP32
(on-board LED on GPIO 2). The arduino.py shim in this folder offers a start()
runner that wraps the setup/loop pattern used manually here.
"""

from machine import Pin, lightsleep

# GPIO 2 = on-board LED on a standard ESP32 (see blink.py for C3/S3 pins).
led = Pin(2, Pin.OUT)

def setup():
    """Run once at start -- mirrors Arduino's setup()."""
    print('starting my program')

def loop():
    """Run repeatedly -- mirrors Arduino's loop()."""
    print('loopy loop')
    led.on()            # on()/off() are shortcuts for value(1)/value(0)
    lightsleep(500)     # low-power sleep for 500 ms
    led.off()
    lightsleep(500)

setup()

while True:
    loop()
