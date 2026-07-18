# Micropython for ESP32 Demos

Ctrl-C : Stop program
Ctrl-D : Soft reboot
Ctrl-F2: Restart backend

main.py: Autostart when the board boots
         EN+BO + Ctrl-c to about during restart

```python
import sys
sys.implementation
# (name='micropython', version=(1, 28, 0, ''), _machine='Generic ESP32 module with ESP32', _mpy=11014, _build='ESP32_GENERIC', _thread='GIL')
sys.version
# '3.4.0; MicroPython v1.28.0 on 2026-04-06'
```

Examples for the ESP32 board
