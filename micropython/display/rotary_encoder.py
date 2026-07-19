"""Interrupt-driven rotary encoder driver for MicroPython.

Each RotaryEncoder instance owns its own pins, decoder state and count, so any
number of encoders can run at once — just give each one a different A/B pin
pair. Decoding uses Ben Buxton's state-machine tables, which reject switch
bounce by ignoring invalid state transitions (no debounce delay needed).

    enc = RotaryEncoder(pin_a=15, pin_b=4)
    ...
    print(enc.value())        # current count
    enc.reset()               # back to 0

Notes for the ESP32:
  * Encoders with no VCC line rely on internal pull-ups (enabled here).
  * Avoid strapping / special pins for A and B (e.g. GPIO 2, 6-11, 12).
    Safe input pins include 4, 5, 13, 14, 18, 19, 25, 26, 27, 32, 33.
"""

from machine import Pin

# Direction bits OR'd into the state value when a detent completes.
_DIR_CW = 0x10
_DIR_CCW = 0x20

# --- Half-step table: emits once per detent for encoders that produce 2
# quadrature transitions per click (the common case). ---
_R_START = 0x0
_R_CCW_BEGIN = 0x1
_R_CW_BEGIN = 0x2
_R_START_M = 0x3
_R_CW_BEGIN_M = 0x4
_R_CCW_BEGIN_M = 0x5

_TTABLE_HALF = (
    # _R_START (00)
    (_R_START_M,            _R_CW_BEGIN,     _R_CCW_BEGIN,   _R_START),
    # _R_CCW_BEGIN
    (_R_START_M | _DIR_CCW, _R_START,        _R_CCW_BEGIN,   _R_START),
    # _R_CW_BEGIN
    (_R_START_M | _DIR_CW,  _R_CW_BEGIN,     _R_START,       _R_START),
    # _R_START_M (11)
    (_R_START_M,            _R_CCW_BEGIN_M,  _R_CW_BEGIN_M,  _R_START),
    # _R_CW_BEGIN_M
    (_R_START_M,            _R_START_M,      _R_CW_BEGIN_M,  _R_START | _DIR_CW),
    # _R_CCW_BEGIN_M
    (_R_START_M,            _R_CCW_BEGIN_M,  _R_START_M,     _R_START | _DIR_CCW),
)

# --- Full-step table: emits once per detent for encoders that produce 4
# quadrature transitions per click. Use half_step=False to select it. ---
_F_START = 0x0
_F_CW_FINAL = 0x1
_F_CW_BEGIN = 0x2
_F_CW_NEXT = 0x3
_F_CCW_BEGIN = 0x4
_F_CCW_FINAL = 0x5
_F_CCW_NEXT = 0x6

_TTABLE_FULL = (
    # _F_START
    (_F_START,     _F_CW_BEGIN,  _F_CCW_BEGIN, _F_START),
    # _F_CW_FINAL
    (_F_CW_NEXT,   _F_START,     _F_CW_FINAL,  _F_START | _DIR_CW),
    # _F_CW_BEGIN
    (_F_CW_NEXT,   _F_CW_BEGIN,  _F_START,     _F_START),
    # _F_CW_NEXT
    (_F_CW_NEXT,   _F_CW_BEGIN,  _F_CW_FINAL,  _F_START),
    # _F_CCW_BEGIN
    (_F_CCW_NEXT,  _F_START,     _F_CCW_BEGIN, _F_START),
    # _F_CCW_FINAL
    (_F_CCW_NEXT,  _F_CCW_FINAL, _F_START,     _F_START | _DIR_CCW),
    # _F_CCW_NEXT
    (_F_CCW_NEXT,  _F_CCW_FINAL, _F_CCW_BEGIN, _F_START),
)


class RotaryEncoder:
    """A single incremental rotary encoder read via pin-change interrupts."""

    def __init__(self, pin_a, pin_b, count=0, step=1,
                 min_val=None, max_val=None, wrap=False,
                 reverse=False, half_step=True):
        """
        pin_a, pin_b : GPIO numbers (or machine.Pin objects) for the A/B outputs.
        count        : initial value.
        step         : amount added/subtracted per detent.
        min_val, max_val : optional inclusive bounds for the count.
        wrap         : if True and bounds are set, wrap around instead of clamp.
        reverse      : swap the sense of rotation (equivalent to swapping A/B).
        half_step    : True for 2-transitions-per-detent encoders (default),
                       False for 4-transitions-per-detent encoders.
        """
        self._a = pin_a if isinstance(pin_a, Pin) else Pin(pin_a, Pin.IN, Pin.PULL_UP)
        self._b = pin_b if isinstance(pin_b, Pin) else Pin(pin_b, Pin.IN, Pin.PULL_UP)

        self.count = count
        self._step = -step if reverse else step
        self._min = min_val
        self._max = max_val
        self._wrap = wrap

        self._table = _TTABLE_HALF if half_step else _TTABLE_FULL
        self._state = _R_START

        # Interrupt on both edges of both pins so no transition is missed.
        trig = Pin.IRQ_RISING | Pin.IRQ_FALLING
        self._a.irq(trigger=trig, handler=self._isr)
        self._b.irq(trigger=trig, handler=self._isr)

    def _isr(self, pin):
        pinstate = (self._a.value() << 1) | self._b.value()
        self._state = self._table[self._state & 0x0F][pinstate]
        direction = self._state & 0x30
        if direction == _DIR_CW:
            self._apply(self._step)
        elif direction == _DIR_CCW:
            self._apply(-self._step)

    def _apply(self, delta):
        value = self.count + delta
        lo, hi = self._min, self._max
        if lo is not None and value < lo:
            value = hi if (self._wrap and hi is not None) else lo
        elif hi is not None and value > hi:
            value = lo if (self._wrap and lo is not None) else hi
        self.count = value

    def value(self):
        """Return the current count."""
        return self.count

    def reset(self, value=0):
        """Set the count to a specific value (default 0)."""
        self.count = value

    def deinit(self):
        """Detach the interrupt handlers."""
        self._a.irq(handler=None)
        self._b.irq(handler=None)
