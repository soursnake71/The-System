"""Device models for the ASR-10 emulation engine.

Everything here follows what is in docs/hardware.md: confirmed on the
real ASR-10 where marked there, otherwise the best current reading of the
ROM and OS. Unknown registers behave as plain memory and are logged once.
"""


class Panel:
    """The front panel controller, as seen from DUART channel B.

    Display (confirmed on hardware): 66 clear, 60 nn cursor (0-79), 77 c
    steady character, bare characters 20-7E print blinking. Button presses
    are queued with press()/release(). Commands the ROM sends at start-up
    (E7, 71) and the keyboard tuning request (FD) get answers configured in
    `answers`; the real controller's code is unknown, so these are models.
    """

    ROWS, COLS = 2, 40

    def __init__(self, log=print, answers=None):
        self.log = log
        self.cells = [" "] * (self.ROWS * self.COLS)
        self.blink = [False] * (self.ROWS * self.COLS)
        self.cursor = 0
        self.pending = None          # command waiting for its argument
        self.to_board = []           # bytes the panel will send
        self.received = []           # every byte the board sent
        # E7/71: MAME's EPS panel model answers both with 00.
        # FD (keyboard tuning): this unit's panel answers C9 (failed).
        self.answers = {0xE7: 0x00, 0x71: 0x00, 0xFD: 0xC9} if answers is None else answers

    def press(self, button):
        self.to_board.append(0x80 | button)

    def release(self, button):
        self.to_board.append(button)

    def receive(self, b):
        self.received.append(b)
        if self.pending is not None:
            self.to_board.append(b)
            cmd, self.pending = self.pending, None
            if cmd == 0x60:
                self.cursor = b % len(self.cells)
            elif cmd in (0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C):
                self._put(b, blink=cmd >= 0x7A)
            return
        # Like MAME's EPS panel model, answer every byte: from the table if
        # listed, otherwise by echoing it (the ROM's handshake retries a
        # command until something comes back).
        self.to_board.append(self.answers.get(b, b))
        if b == 0x66:
            self.cells = [" "] * len(self.cells)
            self.blink = [False] * len(self.cells)
            self.cursor = 0
        elif b in (0x60, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x74, 0x75, 0x76):
            self.pending = b
        elif 0x20 <= b < 0x7F and b not in (0x66, 0x71, 0x73):
            self._put(b, blink=True)

    def _put(self, b, blink):
        self.cells[self.cursor] = chr(b) if 0x20 <= b < 0x7F else "?"
        self.blink[self.cursor] = blink
        self.cursor = min(self.cursor + 1, len(self.cells) - 1)

    def render(self):
        rows = []
        for r in range(self.ROWS):
            rows.append("|" + "".join(self.cells[r * self.COLS:(r + 1) * self.COLS]) + "|")
        return "\n".join(rows)


class Duart:
    """2681 DUART at 0xFC4800, registers on odd bytes (reg n at 0xFC4801+2n).

    Channel A is MIDI out (bytes collected in .midi), channel B the panel.
    Transmitters are always ready; received bytes are delivered one at a
    time. Interrupts aren't wired yet (the board routes the DUART to a
    68302 interrupt input; which one is still to find).
    """

    def __init__(self, machine, panel, log=print):
        self.m = machine
        self.panel = panel
        self.log = log
        self.midi = bytearray()
        self.regs = {}
        self.seen = set()
        self.mr_ptr = [0, 0]
        self.mr = [[0, 0], [0, 0]]
        self.rx_b = None

    def tick(self, cycles):
        if self.rx_b is None and self.panel.to_board:
            self.rx_b = self.panel.to_board.pop(0)

    def read(self, uc, offset, size, user):
        reg = (offset - 0x801) // 2 if offset >= 0x801 and offset % 2 else None
        if reg is None:
            return 0xFF
        if reg in (0x0, 0x8):           # MR1/MR2
            ch = reg // 8
            v = self.mr[ch][self.mr_ptr[ch]]
            self.mr_ptr[ch] = 1
            return v
        if reg == 0x1:                  # SRA: TxRDY, TxEMT
            return 0x0C
        if reg == 0x9:                  # SRB
            self.tick(0)
            return 0x0C | (1 if self.rx_b is not None else 0)
        if reg == 0xB:                  # RHRB
            b = self.rx_b if self.rx_b is not None else 0
            self.rx_b = None
            return b
        if reg not in self.seen:
            self.seen.add(reg)
            self.log(f"[duart] read reg {reg:#x}")
        return self.regs.get(reg, 0)

    def write(self, uc, offset, size, value, user):
        reg = (offset - 0x801) // 2 if offset >= 0x801 and offset % 2 else None
        if reg is None:
            return
        value &= 0xFF
        if reg in (0x0, 0x8):
            ch = reg // 8
            self.mr[ch][self.mr_ptr[ch]] = value
            self.mr_ptr[ch] = 1
        elif reg in (0x2, 0xA) and (value & 0x70) == 0x10:
            self.mr_ptr[reg // 8] = 0
        elif reg == 0x3:
            self.midi.append(value)
        elif reg == 0xB:
            self.panel.receive(value)
        else:
            if (reg, value) not in self.seen:
                self.seen.add((reg, value))
                self.log(f"[duart] write reg {reg:#x} = {value:#04x}")
            self.regs[reg] = value


class M68302:
    """MC68302 block at 0xFC6000: internal RAM below +0x800, registers above.

    The block is ordinary emulator memory; these hooks see register
    accesses. Registers behave as memory unless modelled; each one the code
    touches is logged once so the model can grow where the ROM needs it.
    """

    BASE = 0xFC6000

    def __init__(self, machine, log=print):
        self.m = machine
        self.log = log
        self.seen = set()

    def tick(self, cycles):
        pass

    def on_read(self, uc, access, addr, size, value, user):
        off = addr - self.BASE
        if off not in self.seen:
            self.seen.add(off)
            self.log(f"[68302] read  +{off:#05x} ({size})")

    def on_write(self, uc, access, addr, size, value, user):
        off = addr - self.BASE
        if (off, value) not in self.seen:
            self.seen.add((off, value))
            self.log(f"[68302] write +{off:#05x} = {value:#x} ({size})")
