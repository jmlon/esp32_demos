"""Arduino-compatibility shim for MicroPython on ESP32.

Provides Arduino-style helpers (pin_mode, digital_write, digital_read, delay,
millis) and a start(setup, loop) runner so Arduino-style sketches map cleanly
onto MicroPython's `machine` module. This is a helper library, not a runnable
demo on its own -- import it from another script (see blink2.py):

    from arduino import *
"""

import machine
import time

HIGH = 1
LOW = 0

# Pin mode constants (mirror the Arduino names).
INPUT = 0
OUTPUT = 1

# On-board LED GPIO for a standard ESP32 dev board.
# Use 8 for ESP32-C3 or 48 for ESP32-S3.
LED_BUILTIN = 2

# Cache of configured Pin objects, keyed by GPIO number, so repeated calls
# reuse the same object instead of reconfiguring the pin.
_pins = {}

def _pin(name, mode=OUTPUT):
    """Return a cached machine.Pin for `name`, creating it on first use.

    `name` may be the string 'LED_BUILTIN' or a raw GPIO number. `mode` is
    OUTPUT or INPUT; it only takes effect the first time a pin is created.
    """
    if isinstance(name, str) and name == 'LED_BUILTIN':
        num = LED_BUILTIN
    else:
        num = name
    p = _pins.get(num)
    if p is None:
        direction = machine.Pin.IN if mode == INPUT else machine.Pin.OUT
        p = machine.Pin(num, direction)
        _pins[num] = p
    return p

def pin_mode(name, mode):
    """Configure a pin as INPUT or OUTPUT (Arduino's pinMode)."""
    _pin(name, mode)

def digital_write(name, value):
    """Drive an output pin HIGH or LOW (Arduino's digitalWrite)."""
    _pin(name, OUTPUT).value(value)

def digital_read(name):
    """Read the current level of an input pin (Arduino's digitalRead)."""
    return _pin(name, INPUT).value()

def delay(ms):
    """Block for `ms` milliseconds (Arduino's delay)."""
    time.sleep_ms(ms)

def millis():
    """Return milliseconds since boot (Arduino's millis)."""
    return time.ticks_ms()

def start(setup, loop):
    """Run setup() once, then loop() forever -- the Arduino sketch lifecycle."""
    setup()
    while True:
        loop()
