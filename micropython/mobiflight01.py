r"""
MobiFlight protocol implementation for ESP32 (MicroPython).
MicroPython v1.28.0 on 2026-04-06; Generic ESP32 module with ESP32

Reports a potentiometer on GPIO33 to the MobiFlight Connector as a
single analog input named "Pot1".

Deployment (REQUIRED — running from Thonny's "Run" button does NOT
work, because the MicroPython REPL competes for the USB serial port):

  1. In Thonny: File > Save copy... > MicroPython device > name it
     "main.py".  The script will then auto-run on every boot.
  2. Close Thonny so the COM port is released.
  3. Press the ESP32's RESET button.
  4. Open the MobiFlight Connector.

Escape back to the REPL (script disables ctrl-C, so a normal interrupt
from Thonny won't work).  Two reliable ways out:

  A) Create the file "/skip_mf.flag" on the device with Thonny.  Each
     time the board boots, main.py checks for this file; if present, it
     leaves the REPL attached and exits without running the protocol.
     Delete the file when you want to go back to MobiFlight mode.

  B) While the script is running, briefly press the BOOT button (GPIO0).
     The protocol loop stops, ctrl-C is re-enabled, and the script raises
     KeyboardInterrupt so MicroPython drops back to the REPL prompt.

DO NOT hold BOOT while pressing RESET -- on the ESP32 that's a hardware
strap that either enters the serial-download bootloader (bypassing
main.py but also unusable for Thonny) or causes a flash-read fault.

A diagnostic log is written to /mobiflight.log on the ESP32's flash.
To inspect it: reset/power-cycle the board, re-open Thonny, then in
the file pane open /mobiflight.log.  Each line is prefixed with "<"
for inbound (from PC) and ">" for outbound (to PC).

Adjust connection delay:
Open: "C:\Users\jlon\AppData\Local\MobiFlight\MobiFlight Connector\Boards\arduino_mega.board.json"
  "Connection": {
    "ConnectionDelay": 6000,



"""

import os
import sys
import time
import select
import machine

# --------------------------------------------------------------------------
# Escape hatches.
#
#  1) If "/skip_mf.flag" exists, leave everything alone and drop straight
#     to the REPL.  Use this when you want Thonny to edit/upload.  Create
#     the file from Thonny's shell with: open("/skip_mf.flag","w").close()
#
#  2) BOOT button (GPIO0): the main loop polls it every iteration and
#     bails out to the REPL on first press.  Do NOT hold it during RESET
#     -- that's a hardware strap and won't behave as you'd expect.
# --------------------------------------------------------------------------
# Boot marker -- written unconditionally so we can tell from Thonny whether
# main.py executed at all on the last boot, and what state it saw.
def _marker(msg):
    try:
        with open("/mf_boot_marker.txt", "a") as _f:
            _f.write("[{}ms] {}\n".format(time.ticks_ms(), msg))
    except Exception:
        pass

try:
    # truncate on each boot
    open("/mf_boot_marker.txt", "w").close()
except Exception:
    pass
_marker("main.py started")
_marker("GPIO0 (BOOT) initial reading = {}".format(
    machine.Pin(0, machine.Pin.IN, machine.Pin.PULL_UP).value()))

try:
    os.stat("/skip_mf.flag")
    print("/skip_mf.flag present -- skipping MobiFlight, REPL active.")
    raise SystemExit
except OSError:
    pass

BOOT_BTN = machine.Pin(0, machine.Pin.IN, machine.Pin.PULL_UP)

# --------------------------------------------------------------------------
# Detach the REPL's keyboard-interrupt handling so 0x03 in the protocol
# stream doesn't blow us up.  (We do NOT call dupterm here -- on this
# MicroPython build the REPL lives on USB-CDC, not on a machine.UART that
# we could swap in or out.)
# --------------------------------------------------------------------------
try:
    import micropython
    micropython.kbd_intr(-1)
except Exception:
    pass


class _EscapeToRepl(Exception):
    """Raised by the BOOT-button handler to unwind back to the REPL."""


def _restore_repl_and_exit():
    try:
        import micropython
        micropython.kbd_intr(3)            # re-enable ctrl-C
    except Exception:
        pass
    print("\nBOOT pressed -- dropping to REPL.")
    # Raise an exception (NOT sys.exit, which soft-reboots and re-runs
    # main.py, trapping us in a loop).  An uncaught exception in main.py
    # drops MicroPython straight to the REPL prompt.
    raise _EscapeToRepl()

# --------------------------------------------------------------------------
# CmdMessenger command IDs  (MobiFlight-FirmwareSource/src/commandmessenger.h)
# --------------------------------------------------------------------------
K_STATUS           = 5
K_GET_INFO         = 9
K_INFO             = 10
K_SET_CONFIG       = 11
K_GET_CONFIG       = 12
K_RESET_CONFIG     = 13
K_SAVE_CONFIG      = 14
K_CONFIG_SAVED     = 15
K_ACTIVATE_CONFIG  = 16
K_CONFIG_ACTIVATED = 17
K_SET_POWER_SAVE   = 18
K_SET_NAME         = 19
K_GEN_NEW_SERIAL   = 20
K_TRIGGER          = 23
K_RESET_BOARD      = 24
K_ANALOG_CHANGE    = 28
K_RETRIGGER_DONE   = 34
K_DEBUG            = 0xFF

FIELD_SEP   = ","
CMD_SEP     = ";"
ESCAPE_CHAR = "\\"   # backslash, per CmdMessenger ctor in MobiFlightModule.cs

# --------------------------------------------------------------------------
# Board identity
# --------------------------------------------------------------------------
BOARD_TYPE   = "MobiFlight Mega"
BOARD_NAME   = "ESP32 Pot"
BOARD_SERIAL = "SN-A1B-2C3"   # must match SN-XXX-YYY where chars are hex
FW_VERSION   = "3.0.0"   # core firmware version; Connector enforces a minimum
CORE_VERSION = "3.0.0"   # per get_version.py, core version matches firmware

# Config string: one analog input named "Pot1".
#   format:   <type>.<pin>.<sensitivity>.<name>:
#   type 11 = AnalogInput in the Connector's DeviceType enum
#            (firmware calls this "kTypeAnalogInputDeprecated" but it is
#             what the Connector actually uses on the wire; firmware's
#             new kTypeAnalogInput = 18 is unrecognised by the Connector's
#             parser, which made Items end up empty).
#   pin 54  = A0 on an Arduino Mega (valid analog pin)
ANALOG_NAME   = "Pot1"
CONFIG_STRING = "11.54.5.{}:".format(ANALOG_NAME)

# --------------------------------------------------------------------------
# Hardware
# --------------------------------------------------------------------------
POT_PIN_GPIO     = 33
ANALOG_DEADBAND  = 30
SEND_INTERVAL_MS = 50

adc = machine.ADC(machine.Pin(POT_PIN_GPIO))
adc.width(machine.ADC.WIDTH_12BIT)
adc.atten(machine.ADC.ATTN_11DB)

# --------------------------------------------------------------------------
# Logging to flash -- file is opened LAZILY on the first log() call so
# filesystem I/O doesn't delay our first GetInfo response.
# --------------------------------------------------------------------------
LOG_PATH = "/mobiflight.log"
_log = None


def log(direction, msg):
    global _log
    if _log is None:
        try:
            _log = open(LOG_PATH, "w")
            _log.write("--- boot {} ms ---\n".format(time.ticks_ms()))
        except Exception:
            return
    try:
        _log.write("{:>8} {} {}\n".format(time.ticks_ms(), direction, msg))
        _log.flush()
    except Exception:
        pass

# --------------------------------------------------------------------------
# Serial I/O via sys.stdin / sys.stdout (REPL detached above).  Taking
# UART0 directly fails on ESP32 MicroPython because stdio owns it.
# --------------------------------------------------------------------------
_poll = select.poll()
_poll.register(sys.stdin, select.POLLIN)

_rx_buf = ""
board_ready = False


def _escape(s):
    out = []
    for ch in s:
        if ch in (FIELD_SEP, CMD_SEP, ESCAPE_CHAR, "\0"):
            out.append(ESCAPE_CHAR)
        out.append(ch)
    return "".join(out)


def send_cmd(cmd_id, *args):
    parts = [str(cmd_id)]
    for a in args:
        parts.append(_escape(str(a)))
    line = FIELD_SEP.join(parts) + CMD_SEP + "\r\n"
    sys.stdout.write(line)
    log(">", line.rstrip())


def _split_fields(line):
    fields = []
    cur = []
    i = 0
    while i < len(line):
        ch = line[i]
        if ch == ESCAPE_CHAR and i + 1 < len(line):
            cur.append(line[i + 1])
            i += 2
            continue
        if ch == FIELD_SEP:
            fields.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
        i += 1
    fields.append("".join(cur))
    return fields


def poll_command():
    global _rx_buf
    if not _poll.poll(0):
        return None
    ch = sys.stdin.read(1)
    if not ch:
        return None
    # log every byte for diagnosis (the file flush each line is fine for now)
    log("b", "{!r}".format(ch))
    if ch == CMD_SEP:
        line = _rx_buf
        _rx_buf = ""
        log("<", line)
        return _split_fields(line)
    elif ch in ("\r", "\n", "\0"):
        return None
    else:
        _rx_buf += ch
        if len(_rx_buf) > 1024:
            log("<", "(overflow, dropping buffer)")
            _rx_buf = ""
    return None


# --------------------------------------------------------------------------
# Command handlers
# --------------------------------------------------------------------------
def on_get_info():
    send_cmd(K_INFO, BOARD_TYPE, BOARD_NAME, BOARD_SERIAL, FW_VERSION, CORE_VERSION)


def on_get_config():
    global board_ready
    send_cmd(K_INFO, CONFIG_STRING)
    board_ready = True


def on_set_config(args):
    cfg = args[1] if len(args) > 1 else ""
    send_cmd(K_STATUS, len(cfg))


def on_reset_config():
    send_cmd(K_STATUS, "OK")


def on_save_config():
    send_cmd(K_CONFIG_SAVED, "OK")


def on_activate_config():
    send_cmd(K_CONFIG_ACTIVATED, "OK")


def on_set_name(args):
    name = args[1] if len(args) > 1 else BOARD_NAME
    send_cmd(K_STATUS, name)


def on_trigger():
    last_value[0] = -10000          # force a resend on next loop tick
    send_cmd(K_RETRIGGER_DONE)


def on_gen_new_serial():
    send_cmd(K_INFO, BOARD_SERIAL)


def on_unknown():
    send_cmd(K_STATUS, "n/a")


def dispatch(fields):
    if not fields or not fields[0]:
        return
    try:
        cmd_id = int(fields[0])
    except ValueError:
        log("!", "non-integer cmd id: {!r}".format(fields[0]))
        return

    if cmd_id == K_GET_INFO:           on_get_info()
    elif cmd_id == K_GET_CONFIG:       on_get_config()
    elif cmd_id == K_SET_CONFIG:       on_set_config(fields)
    elif cmd_id == K_RESET_CONFIG:     on_reset_config()
    elif cmd_id == K_SAVE_CONFIG:      on_save_config()
    elif cmd_id == K_ACTIVATE_CONFIG:  on_activate_config()
    elif cmd_id == K_SET_NAME:         on_set_name(fields)
    elif cmd_id == K_GEN_NEW_SERIAL:   on_gen_new_serial()
    elif cmd_id == K_TRIGGER:          on_trigger()
    elif cmd_id == K_SET_POWER_SAVE:   pass   # real firmware sends no reply
    else:                              on_unknown()


# --------------------------------------------------------------------------
# Main loop
# --------------------------------------------------------------------------
last_value   = [-10000]
last_sent_ms = [0]


def main():
    _marker("entered main()")
    # Require the BOOT button to be released for ~100ms before we treat
    # a low reading as a genuine press.  Some ESP32 boards pull GPIO0 low
    # briefly right after boot.
    boot_released_since = None
    iterations = 0
    try:
        while True:
            now_ms = time.ticks_ms()
            btn = BOOT_BTN.value()
            if btn == 1:
                if boot_released_since is None:
                    boot_released_since = now_ms
                    _marker("BOOT released, arming escape")
            else:
                if boot_released_since is not None and time.ticks_diff(now_ms, boot_released_since) > 100:
                    _marker("BOOT pressed -- escaping")
                    _restore_repl_and_exit()

            for _ in range(256):
                fields = poll_command()
                if fields is not None:
                    dispatch(fields)
                else:
                    break

            if board_ready:
                raw = adc.read()
                if (abs(raw - last_value[0]) >= ANALOG_DEADBAND and
                        time.ticks_diff(now_ms, last_sent_ms[0]) >= SEND_INTERVAL_MS):
                    send_cmd(K_ANALOG_CHANGE, ANALOG_NAME, raw)
                    last_value[0]   = raw
                    last_sent_ms[0] = now_ms

            iterations += 1
            if iterations == 1:
                _marker("first main loop iteration complete")
            time.sleep_ms(5)
    except _EscapeToRepl:
        _marker("exiting via _EscapeToRepl")
        raise
    except Exception as e:
        _marker("main loop crashed: {!r}".format(e))
        raise


main()
