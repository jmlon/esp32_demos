# MobiFlight on ESP32 / MicroPython — protocol notes

Working implementation lives in `mobiflight01.py`. These are the
non-obvious things we had to learn the hard way. Source-of-truth
references in the cloned repos:

- Firmware: `E:\GIT\tmp\MobiFlight-FirmwareSource`
- Connector (desktop app): `E:\GIT\tmp\MobiFlight-Connector`

## Wire protocol (CmdMessenger)

- Field separator: `,`
- Command separator: `;`
- Escape character: `\` (backslash) — **not** `/` as the firmware
  comments imply. The authoritative value is in
  `MobiFlight/MobiFlightModule.cs:281`:
  `new CmdMessenger(..., ',', ';', '\\', ...)`.
- Line endings: PC → board sends commands as `<id>[,<arg>...];` with no
  trailing newline. Board → PC sends `<id>[,<arg>...];\r\n`. The
  Connector trims `\r\n` before parsing (`CommunicationManager.cs:254`),
  so either order works on the inbound side.
- Each command starts with a **numeric** ID. The protocol descriptions
  using `f.get`/`f.type`/`f.id` (e.g. AI summaries) are wrong — those
  commands don't exist. Real IDs are in
  `MobiFlight-FirmwareSource/src/commandmessenger.h`.

## Discovery sequence the Connector enforces

After `SerialPort.Open()` (which asserts DTR and resets the ESP32),
the Connector waits `Board.Connection.ConnectionDelay` and then runs:

1. `GetInfo` (9) → expects `Info` (10) within `CommandTimeout` = 2500 ms.
2. If `InfoCommand.Ok`, **sends GetInfo a second time** (acknowledged
   workaround in `MobiFlightModule.cs:918-922`). Only the *second*
   response's data is used. If the first call times out, the second
   call is never sent and the board is silently disconnected.
3. After `OnMobiFlightBoardDetected`, `LoadConfig()` is called.
   `LoadConfig` accesses `module.Config.Items`, which triggers a
   `GetConfig` (12). The response (`Info` payload) is parsed into
   `Config.Items`.

The board only becomes "ready" after `OnGetConfig` runs server-side
(`Config.cpp:620`), and analog change events from the board are
expected to be sent only after that point.

## Timing — `ConnectionDelay` must accommodate MicroPython boot

- MicroPython on ESP32 boots in ~3–4 s from a hard reset (DTR pulse).
- Default Mega `ConnectionDelay` = 2000 ms + `CommandTimeout` = 2500 ms
  ⇒ Connector gives up at T+4500 ms. That is *too tight* for
  MicroPython to be ready and respond.
- **Fix**: edit
  `%LOCALAPPDATA%\MobiFlight\MobiFlight Connector\Boards\arduino_mega.board.json`
  and set `"ConnectionDelay": 6000` (under the `Connection` object).
  Without this, the first `GetInfo` round-trip races MicroPython boot
  and usually loses.

## DeviceType enum mismatch (this one is brutal)

There are **two** enums that both call themselves "analog input" and
use different numeric values. The wire format uses the *Connector's*
enum.

| Name in firmware (`commandmessenger.h`) | Value | Name in Connector (`DeviceType.cs`) | Value |
| --- | --- | --- | --- |
| `kTypeAnalogInputDeprecated` | 11 | `AnalogInput` | **11** |
| `kTypeAnalogInput` | 18 | (no entry — undefined enum) | — |

When the Connector serializes an `AnalogInput` (e.g. during
`SetConfig`), it writes type `11`. The firmware accepts both 11 and
18 and parses them as analog inputs, so 18 looks like it should work
— but when the board echoes 18 back, the **Connector's** parser
hits an unknown enum value, the `switch` falls through to default,
and `Items` ends up empty. Symptoms: real-time analog events flow
fine, but the Modules tree has no children, the wizard's Device
dropdown is empty, and "Reload config" does nothing.

**Always send type 11 for analog inputs.** The same caution likely
applies to other "deprecated" firmware types — when in doubt, check
the values in `DeviceType.cs`, not the firmware header.

Config wire format for one analog input:

```
<type>.<pin>.<sensitivity>.<name>:
```

e.g. `11.54.5.Pot1:`. Pin number is a Mega pin number (54 = A0); it
doesn't have to correspond to the ESP32 GPIO we actually read from.

## REPL and stdio on ESP32 MicroPython

- On this build, the REPL lives on **USB-CDC**, not on a `machine.UART`
  we can swap in or out. Attempting `machine.UART(0, 115200)` raises
  `OSError(-259, 'ESP_ERR_INVALID_STATE')` because the IDF UART driver
  was never installed — stdio uses USB-CDC directly. Use
  `sys.stdin`/`sys.stdout` with `select.poll`.
- To stop the REPL from competing for incoming bytes, call
  `os.dupterm(None, 1)` early. The 0x03 (ctrl-C) byte also has to be
  disarmed with `micropython.kbd_intr(-1)`, otherwise any 0x03 in the
  protocol stream raises `KeyboardInterrupt`.
- Once `kbd_intr(-1)` is in effect there is no normal way to break out
  of the script — see the escape-hatch section.

## Escape hatches (since `kbd_intr(-1)` is irreversible from outside)

The script supports two ways back to the REPL:

1. **`/skip_mf.flag` file on the device.** Checked at startup. If
   present, the script prints a message and exits before detaching
   anything. Create from Thonny shell with
   `open("/skip_mf.flag","w").close()`. Delete it when you want to
   run the protocol again.
2. **BOOT button (GPIO0) press while the script is running.** The
   main loop polls it every iteration, re-enables `kbd_intr(3)`, and
   raises a custom `_EscapeToRepl` exception. The uncaught exception
   unwinds out of `main.py` and MicroPython falls into the REPL.
   - The button must be *released* for ~100 ms before a press counts —
     GPIO0 reads low briefly right after boot on some boards.

Pitfalls we hit:

- `sys.exit()` inside `main.py` triggers a **soft reboot** (which
  re-runs `main.py`, re-detaches the REPL, and traps you). Use an
  exception instead.
- `os.dupterm(machine.UART(0, 115200), 1)` to "restore" the REPL
  fails (see above) and the failure path soft-reboots — same trap.
- Holding GPIO0 during RESET does **not** drop into the REPL — it
  activates the bootloader strap. Either the board enters
  serial-download mode (good for `esptool erase_flash`, useless for
  Thonny) or you get `flash read err, 1000` followed by
  `RTCWDT_RTC_RESET`. Don't rely on this.

If you ever do get fully stuck (REPL won't return, `main.py` traps),
the recovery is **Thonny → Tools → Options → Interpreter → Install or
update MicroPython (esptool) → Erase flash**, then re-flash the
firmware.

## Other small but useful facts

- The Connector spams `SetPowerSavingMode` (command 18) on a regular
  cadence. Real firmware does not reply to it (`commandmessenger.cpp:109`),
  so don't either — just `pass`. Sending `5,n/a;` works but is noise.
- `kResetBoard` (24) and a few others are *not* attached as callbacks
  in `attachCommandCallbacks()` — the real firmware returns `5,n/a;`
  via `OnUnknownCommand` for those. Mirror that behavior.
- `OnGetConfig` in firmware (`Config.cpp:605`) sends each byte of the
  EEPROM config separately via `sendArg` — the wire payload is still a
  single CmdMessenger argument because `sendArg` (no `Cmd`) does not
  insert a field separator. The C# side reads it back with one
  `ReadStringArg()` call. So just send the whole config string as one
  arg.
- Boot timestamp in our log can look huge (e.g. 1.4M ms) because
  `time.ticks_ms()` does not reset on soft reboot — only on hard
  reset. Don't read too much into absolute values; the delta between
  receive and reply is the relevant number.

## Workflow checklist for any change

1. Edit `mobiflight01.py` on the host.
2. In Thonny: save copy to MicroPython device as `main.py`.
3. **Close Thonny** (release COM4) before testing — otherwise it owns
   the port and MobiFlight Connector can't see the board.
4. Press the ESP32 RESET button.
5. Open MobiFlight Connector. Module should auto-enumerate with its
   device(s).
6. To go back to editing: press BOOT once, or create `/skip_mf.flag`
   and reset.
