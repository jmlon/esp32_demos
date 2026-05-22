import network
import time
import urequests

ap_ssid = "Mikrotik"
ap_password = "tayfunulu"

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

    else:
        print("Failed:", nic.status())
