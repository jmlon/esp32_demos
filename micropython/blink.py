"""Minimal LED blink demo for ESP32 -- toggles the on-board LED once per second.

Pick the LED_OUTPUT value matching your board (see the commented options below).
"""

import machine
import time

LED_OUTPUT = 2 # ESP32
#LED_OUTPUT = 8 # ESP32-C3
#LED_OUTPUT = 48 # ESP32-S3

led = machine.Pin(LED_OUTPUT, machine.Pin.OUT)

while True:
    led.value(1)
    time.sleep(1)
    led.value(0)
    time.sleep(1)
