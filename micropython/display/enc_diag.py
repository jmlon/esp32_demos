import time
from machine import Pin

# Same pins as the main sketch
ROT1_A = 15   # CLK
ROT1_B = 4    # DT  (avoid GPIO2: strapping pin, tied to onboard LED/pulldown)

a = Pin(ROT1_A, Pin.IN, Pin.PULL_UP)
b = Pin(ROT1_B, Pin.IN, Pin.PULL_UP)

print("Turn the encoder slowly. Watching A/B (Ctrl-C to stop).")
print("Resting state now: A=%d B=%d" % (a.value(), b.value()))

last = (a.value(), b.value())
while True:
    cur = (a.value(), b.value())
    if cur != last:
        print("A=%d B=%d" % cur)
        last = cur
    time.sleep_ms(1)
