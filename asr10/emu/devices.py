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

    def __init__(self, machine, panel, log=print, floppy=None):
        self.m = machine
        self.panel = panel
        self.floppy = floppy
        self.log = log
        self.midi = bytearray()
        self.regs = {}
        self.seen = set()
        self.mr_ptr = [0, 0]
        self.mr = [[0, 0], [0, 0]]
        self.rx_b = None
        # Input pins, from the ROM's disk code: the ROM clears the change
        # bits, then expects IP0 to have changed (IPCR bit 4) if a disk is
        # in: IP0 is the drive's index pulse, one per turn while a disk
        # spins. A change on IP1 (bit 5) is checked by the disk-change code;
        # IP2 low selects high-density settings.
        self.ip = 0x00
        self.ip_changed = 0x0
        self.index_timer = 0
        self.tx_on = [False, False]       # transmitter enabled, per channel
        self.imr = 0
        self.counter_ready = False
        self.counter_period = 0
        self.counter_phase = 0
        if floppy is not None and floppy.image is not None and len(floppy.image) <= 819200:
            self.ip |= 0x04                # double density

    def tick(self, cycles):
        if self.rx_b is None and self.panel.to_board:
            self.rx_b = self.panel.to_board.pop(0)
        if self.counter_period:
            self.counter_phase += cycles
            if self.counter_phase >= self.counter_period:
                self.counter_phase = 0
                self.counter_ready = True
        self._update_irq()
        if self.floppy is not None and self.floppy.image is not None:
            self.index_timer += cycles
            if self.index_timer >= 20000:   # index pulse
                self.index_timer = 0
                self.ip ^= 0x01
                self.ip_changed |= 0x01

    # ISR bits: 0 TxRDYA, 1 RxRDYA, 3 counter ready, 4 TxRDYB, 5 RxRDYB
    def isr(self):
        v = 0
        if self.tx_on[0]:
            v |= 0x01
        if self.counter_ready:
            v |= 0x08
        if self.tx_on[1]:
            v |= 0x10
        if self.rx_b is not None:
            v |= 0x20
        return v

    def _update_irq(self):
        if self.isr() & self.imr:
            self.m.raise_irq("duart", 6, 86)       # IRQ6, vector 86 (OS header)
        else:
            self.m.clear_irq("duart")

    def read(self, uc, offset, size, user):
        v = self._read(offset, size)
        self._update_irq()
        return v

    def write(self, uc, offset, size, value, user):
        self._write(offset, size, value)
        self._update_irq()

    def _command(self, ch, value):
        if value & 0x04:
            self.tx_on[ch] = True
        if value & 0x08:
            self.tx_on[ch] = False
        if (value & 0x70) == 0x10:
            self.mr_ptr[ch] = 0
        elif (value & 0x70) == 0x30:              # reset transmitter
            self.tx_on[ch] = False

    def _read(self, offset, size):
        if offset < 0x800 and self.floppy is not None:
            return self.floppy.read(offset)
        reg = (offset - 0x801) // 2 if offset >= 0x801 and offset % 2 else None
        if reg is None:
            return 0xFF
        if reg in (0x0, 0x8):           # MR1/MR2
            ch = reg // 8
            v = self.mr[ch][self.mr_ptr[ch]]
            self.mr_ptr[ch] = 1
            return v
        if reg == 0x1:                  # SRA: TxRDY, TxEMT when enabled
            return 0x0C if self.tx_on[0] else 0x00
        if reg == 0x9:                  # SRB
            self.tick(0)
            return (0x0C if self.tx_on[1] else 0x00) | (1 if self.rx_b is not None else 0)
        if reg == 0x5:                  # ISR
            return self.isr()
        if reg == 0xE:                  # start counter
            self.counter_phase = 0
            return 0xFF
        if reg == 0xF:                  # stop counter: clears counter ready
            self.counter_ready = False
            return 0xFF
        if reg == 0xB:                  # RHRB
            b = self.rx_b if self.rx_b is not None else 0
            self.rx_b = None
            return b
        if reg == 0x4:                  # IPCR: changes (bits 4-7), pins 0-3
            v = (self.ip_changed << 4) | (self.ip & 0x0F)
            self.ip_changed = 0
            return v
        if reg == 0xD:                  # IP: input pins
            return self.ip
        if reg not in self.seen:
            self.seen.add(reg)
            self.log(f"[duart] read reg {reg:#x}")
        return self.regs.get(reg, 0)

    def _write(self, offset, size, value):
        if offset < 0x800 and self.floppy is not None:
            self.floppy.write(offset, value & 0xFF)
            return
        reg = (offset - 0x801) // 2 if offset >= 0x801 and offset % 2 else None
        if reg is None:
            return
        value &= 0xFF
        if reg in (0x0, 0x8):
            ch = reg // 8
            self.mr[ch][self.mr_ptr[ch]] = value
            self.mr_ptr[ch] = 1
        elif reg in (0x2, 0xA):
            self._command(reg // 8, value)
        elif reg == 0x5:
            self.imr = value
        elif reg in (0x6, 0x7):
            self.regs[reg] = value
            # counter/timer preset; one counter-ready per 2 x preset clocks
            # of a 3.6864 MHz X1 clock, in CPU instructions (~4 MHz worth)
            preset = (self.regs.get(6, 0) << 8) | self.regs.get(7, 0)
            self.counter_period = max(1, preset * 2)
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

    The block is ordinary emulator memory (registers keep what was written);
    these hooks see register accesses. Modelled: timers 1 and 2 and the
    interrupt controller (IPR +0x814, IMR +0x816, ISR +0x818), delivering
    internal interrupts at level 4 with vector (GIMR bits 7-5) | source.
    Everything else is logged once.
    """

    BASE = 0xFC6000
    GIMR, IPR, IMR, ISR = 0x812, 0x814, 0x816, 0x818
    # (TMR, TRR, TCN, TER, source bit) per timer
    TIMERS = ((0x840, 0x842, 0x846, 0x849, 9), (0x850, 0x852, 0x856, 0x859, 6))
    MODELLED = {GIMR, IPR, IMR, ISR}

    def __init__(self, machine, log=print):
        self.m = machine
        self.log = log
        self.seen = set()
        self.count = [0, 0]
        self.cp_commands = set()

    def r16(self, off):
        return self.m.r16(self.BASE + off)

    def w16(self, off, v):
        self.m.uc.mem_write(self.BASE + off, (v & 0xFFFF).to_bytes(2, "big"))

    def tick(self, cycles):
        for i, (tmr, trr, tcn, ter, bit) in enumerate(self.TIMERS):
            mode = self.r16(tmr)
            if not mode & 1:                    # RST: timer enabled
                continue
            prescale = (mode >> 8) + 1
            self.count[i] += cycles * 4 // prescale   # ~4 clocks per instruction
            ref = self.r16(trr) or 0x10000
            if self.count[i] >= ref:
                self.count[i] %= ref
                if mode & 0x10:                 # ORI: interrupt on reference
                    self.m.uc.mem_write(self.BASE + ter, bytes([self.m.uc.mem_read(self.BASE + ter, 1)[0] | 2]))
                    self.w16(self.IPR, self.r16(self.IPR) | (1 << bit))
            self.w16(tcn, self.count[i])
        self.update_irq()

    def update_irq(self):
        pending = self.r16(self.IPR) & self.r16(self.IMR) & ~self.r16(self.ISR)
        if pending:
            bit = pending.bit_length() - 1
            base = self.r16(self.GIMR) & 0xE0
            self.m.raise_irq("68302", 4, base | bit, ack=lambda: self.ack(bit))
        else:
            self.m.clear_irq("68302")

    def ack(self, bit):
        self.w16(self.IPR, self.r16(self.IPR) & ~(1 << bit))
        self.w16(self.ISR, self.r16(self.ISR) | (1 << bit))

    def on_read(self, uc, access, addr, size, value, user):
        off = (addr & 0xFFFFFF) - self.BASE
        if off in (0x828, 0x829):
            # Port B pin 3 is a periodic clock from elsewhere on the board
            # (OS start-up fails with ERROR 009 unless it toggles; likely the
            # sound chip's sample clock). Toggle it on every read.
            a = self.BASE + 0x829
            uc.mem_write(a, bytes([uc.mem_read(a, 1)[0] ^ 0x08]))
        if off not in self.seen and off not in self.MODELLED:
            self.seen.add(off)
            self.log(f"[68302] read  +{off:#05x} ({size})")

    def on_write(self, uc, access, addr, size, value, user):
        off = (addr & 0xFFFFFF) - self.BASE
        if off == self.ISR:
            # writing 1 clears in-service bits; done after the store lands
            self.m.after_write.append(lambda v=value: self.w16(self.ISR, self.isr_before & ~v))
            self.isr_before = self.r16(self.ISR)
            return
        if off == 0x860:
            # command register: bit 0 (FLG) reads back clear once the
            # communications processor has taken the command
            self.m.after_write.append(lambda a=addr: self.m.uc.mem_write(
                a, bytes([self.m.uc.mem_read(a, 1)[0] & 0xFE])))
            if value not in self.cp_commands:
                self.cp_commands.add(value)
                self.log(f"[68302] CP command {value:#04x}")
            return
        if off in (0x849, 0x859):
            ter_before = self.m.uc.mem_read(addr, 1)[0]
            self.m.after_write.append(lambda a=addr, v=value, b=ter_before:
                                      self.m.uc.mem_write(a, bytes([b & ~v & 0xFF])))
            return
        if (off, value) not in self.seen and off not in self.MODELLED:
            self.seen.add((off, value))
            self.log(f"[68302] write +{off:#05x} = {value:#x} ({size})")


class Floppy:
    """NEC uPD72069 floppy controller (uPD765 command set) at 0xFC4000:
    main status register at +1, data register at +3.

    Commands go in through the data register while MSR says ready (bit 7)
    and direction is host-to-controller (bit 6 = 0); results come back the
    same way with bit 6 = 1. Sector data comes from an Ensoniq .img in
    the usual order (cylinder, head, sector numbered from 0).
    """

    CMD_LEN = {0x03: 3, 0x04: 2, 0x07: 2, 0x08: 1, 0x0F: 3, 0x06: 9, 0x05: 9,
               0x0A: 2, 0x0D: 6, 0x10: 1, 0x13: 4, 0x12: 2, 0x14: 1, 0x0E: 1}

    def __init__(self, machine, log=print, image=None, sectors=20):
        self.m = machine
        self.log = log
        self.image = image
        self.sectors = sectors
        self.cmd = []
        self.result = []
        self.data = bytearray()      # sector bytes waiting to be read (PIO)
        self.cyl = [0, 0, 0, 0]
        self.int_pending = []        # (st0, cyl) for SENSE INTERRUPT STATUS
        self.commands = []           # log of every command, for tests

    def msr(self):
        if self.data:
            return 0xF0              # RQM, DIO (to host), non-DMA execution, busy
        if self.result:
            return 0xD0              # RQM, DIO, busy
        return 0x80 | (0x10 if self.cmd else 0)

    def read(self, off):
        if off == 1:
            return self.msr()
        if off == 3:
            if self.data:
                return self.data.pop(0)
            if self.result:
                return self.result.pop(0)
            return 0x80
        return 0xFF

    def write(self, off, value):
        if off != 3:
            self.log(f"[fdc] write +{off:#x} = {value:#04x}")
            return
        self.cmd.append(value)
        op = self.cmd[0] & 0x1F
        need = self.CMD_LEN.get(op)
        if need is None:
            self.log(f"[fdc] unknown command {self.cmd[0]:#04x}")
            self.result = [0x80]
            self.cmd = []
            return
        if len(self.cmd) == need:
            cmd, self.cmd = self.cmd, []
            self.commands.append(bytes(cmd))
            self.execute(cmd)

    def execute(self, cmd):
        op = cmd[0] & 0x1F
        self.log(f"[fdc] {bytes(cmd).hex(' ')}")
        drive = cmd[1] & 3 if len(cmd) > 1 else 0
        if op == 0x03:                                   # SPECIFY
            pass
        elif op == 0x04:                                 # SENSE DRIVE STATUS
            st3 = 0x20 | drive | (0x10 if self.cyl[drive] == 0 else 0)
            if self.image is not None:
                st3 |= 0x08                              # two-sided
            self.result = [st3]
        elif op == 0x07:                                 # RECALIBRATE
            self.cyl[drive] = 0
            self.int_pending.append((0x20 | drive, 0))
        elif op == 0x0F:                                 # SEEK
            self.cyl[drive] = cmd[2]
            self.int_pending.append((0x20 | drive, cmd[2]))
        elif op == 0x08:                                 # SENSE INTERRUPT STATUS
            if self.int_pending:
                st0, c = self.int_pending.pop(0)
                self.result = [st0, c]
            else:
                self.result = [0x80]
        elif op == 0x0A:                                 # READ ID
            head = (cmd[1] >> 2) & 1
            if self.image is None:
                self.result = [0x40 | drive, 0x01, 0x00, self.cyl[drive], head, 0, 2]
            else:
                self.result = [drive | (head << 2), 0, 0, self.cyl[drive], head, 0, 2]
        elif op == 0x06:                                 # READ DATA
            c, h, r, n, eot = cmd[2], cmd[3], cmd[4], cmd[5], cmd[6]
            head = (cmd[1] >> 2) & 1
            if self.image is None:
                self.result = [0x40 | drive | (head << 2), 0x01, 0, c, h, r, n]
                return
            out = bytearray()
            sec = r
            while sec <= eot:
                block = (c * 2 + head) * self.sectors + sec
                out += self.image[block * 512:(block + 1) * 512]
                sec += 1
            self.data = out
            self.result = [drive | (head << 2), 0x80, 0, c, h, eot, n]
        elif op == 0x10:                                 # VERSION
            self.result = [0x90]
        else:
            self.log(f"[fdc] command {cmd[0]:#04x} not modelled")
            self.result = [0x40 | drive, 0x01, 0, 0, 0, 0, 0]
