"""Connect an ESP32 to Wi-Fi (station mode) and make a simple HTTP GET request.

Target: ESP32 running MicroPython (requires the `urequests` module).
Demonstrates: forcing AP mode off, connecting as a station with a timeout, and
fetching a URL.
"""

import network
import time
import urequests

# TODO: replace with your own network credentials.
# Do not commit real passwords to a public repo.
ap_ssid = "YOUR_WIFI_SSID"
ap_password = "YOUR_WIFI_PASSWORD"

# Make sure AP mode is off (it persists across soft resets)
ap = network.WLAN(network.WLAN.IF_AP)
ap.active(False)

# Turn off, then on the Station mode
nic = network.WLAN(network.WLAN.IF_STA)
nic.active(False)
time.sleep(0.1)
nic.active(True)

if nic.isconnected():
    nic.disconnect()

nic.connect(ap_ssid, ap_password)
time.sleep(1)

print("Waiting for connection...")
timeout = 20

while not nic.isconnected() and timeout > 0:
    time.sleep(1)
    timeout -= 1

    if nic.isconnected():
        print("Assigned IP address and mask:",nic.ipconfig("addr4"))
        print()
        print("Making http request:")
        r = urequests.get("http://example.com")
        print("status:", r.status_code)
        print("headers:", r.headers)
        print("body bytes:", len(r.content))
        print(r.text[:200])
        r.close()
        break  # request done -- stop the wait loop

    else:
        print("Failed:", nic.status())
