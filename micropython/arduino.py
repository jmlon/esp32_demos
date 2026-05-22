import machine
import time

HIGH = 1
LOW = 0

LED_BUILTIN = 2

_pins = {}

def _pin(name):
    if isinstance(name, str) and name == 'LED_BUILTIN':
        num = LED_BUILTIN
    else:
        num = name
    p = _pins.get(num)
    if p is None:
        p = machine.Pin(num, machine.Pin.OUT)
        _pins[num] = p
    return p

def pin_mode(name, mode):
    _pin(name)

def digital_write(name, value):
    _pin(name).value(value)

def digital_read(name):
    return machine.Pin(name, machine.Pin.IN).value()

def delay(ms):
    time.sleep_ms(ms)

def millis():
    return time.ticks_ms()

def start(setup, loop):
    setup()
    while True:
        loop()
