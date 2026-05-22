# from arduino import * # Arduino package not present
from machine import Pin, lightsleep
import time


led = Pin(2, Pin.OUT)

def setup():
  print('starting my program')

def loop():
  print('loopy loop')
  #led.value(1)
  led.on()
  lightsleep(500)
  #led.value(0)
  led.off()
  lightsleep(500)

setup()

while True:
    loop()
