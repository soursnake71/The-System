#!/usr/bin/env python3
"""ASR-10 emulation engine.

Runs the real boot ROM (and the OS it loads) on an emulated 68000, with
the board's devices modelled as far as they are known:

  0x000000-0x07FFFF  RAM
  0xF80000-0xFBFFFF  boot ROM (your dump, local/asr10rom.bin)
  0xFC4800           2681 DUART: channel A = MIDI, channel B = front panel
  0xFC6000           MC68302 internal RAM (+0x000) and registers (+0x800)
  0xFF0000-0xFFFFFF  RAM

Any other address the code touches is mapped on first use and logged, so
running the ROM shows what still needs modelling.

Usage: asr10.py [--rom local/asr10rom.bin] [--steps N] [--trace]
"""

import argparse
import collections
import ctypes
import os
import struct
import sys

from unicorn import (Uc, UcError, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_MEM_UNMAPPED,
                     UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_INTR, UC_PROT_ALL,
                     UC_MEM_FETCH_UNMAPPED)
from unicorn.m68k_const import (UC_M68K_REG_PC, UC_M68K_REG_SR, UC_M68K_REG_A7, UC_M68K_REG_A0,
                                UC_M68K_REG_D0, UC_CPU_M68K_M68000)

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from devices import Duart, Floppy, M68302, Panel  # noqa: E402

ROM_BASE, ROM_SIZE = 0xF80000, 0x40000
PAGE = 0x1000
MIRROR = 0xFF000000   # the 68000 drives 24 address lines; 0xFFxxxxxx = 0x00xxxxxx


class Machine:
    def __init__(self, rom, log=print, disk=None):
        self.log = log
        self.uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        self.uc.ctl_set_cpu_model(UC_CPU_M68K_M68000)
        self.auto_mapped = collections.OrderedDict()
        self.irq = collections.OrderedDict()   # source -> (level, vector, ack)
        self.after_write = []                  # fix-ups after a hooked store

        self.buffers = []
        self.map_ram(0x000000, 0x80000)
        self.map_ram(0xFF0000, 0x10000)
        self.map_ram(ROM_BASE, ROM_SIZE)
        self.uc.mem_write(ROM_BASE, rom)

        self.panel = Panel(log=log)
        self.floppy = Floppy(self, log=log, image=disk)
        self.duart = Duart(self, self.panel, log=log, floppy=self.floppy)
        self.m68302 = M68302(self, log=log)
        for base in (0xFC4000, 0xFC4000 | MIRROR):
            self.uc.mmio_map(base, PAGE, self.duart.read, None, self.duart.write, None)
        # The 68302 block holds code (the ROM runs from its internal RAM),
        # so it is ordinary memory; hooks model the register half. Unicorn
        # runs a read hook before the read, so values set there are seen.
        self.map_ram(0xFC6000, PAGE)
        for base in (0xFC6800, 0xFC6800 | MIRROR):
            self.uc.hook_add(UC_HOOK_MEM_READ, self.m68302.on_read, begin=base, end=base + 0x7FF)
            self.uc.hook_add(UC_HOOK_MEM_WRITE, self.m68302.on_write, begin=base, end=base + 0x7FF)
        # SCSI option (WD33C93, at 0xFC5000): not modelled yet, so present an
        # empty bus (reads FF) and the ROM decides no SCSI board is fitted.
        for base in (0xFC5000, 0xFC5000 | MIRROR):
            self.uc.mmio_map(base, PAGE, lambda uc, off, size, ud: (1 << (8 * size)) - 1, None,
                             lambda uc, off, size, value, ud: None, None)
        self.uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._unmapped)
        self.uc.hook_add(UC_HOOK_INTR, self._exception)
        self.exceptions = collections.Counter()
        self.movep_count = 0
        self.interrupts = collections.Counter()
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self._flush_after_write)

        # Reset: the ROM is at 0 until the chip selects move it; its code
        # at 0x0C is position independent up to the jump into 0xF8xxxx, so
        # start there directly. SR first: in supervisor mode A7 is the SSP.
        ssp, pc = struct.unpack(">II", rom[:8])
        self.uc.reg_write(UC_M68K_REG_SR, 0x2700)
        self.uc.reg_write(UC_M68K_REG_A7, ssp)
        self.uc.reg_write(UC_M68K_REG_PC, ROM_BASE + pc)

    # memory

    def map_ram(self, base, size):
        """Maps size bytes at base and at its 0xFFxxxxxx mirror, sharing one buffer."""
        buf = (ctypes.c_ubyte * size)()
        self.buffers.append(buf)
        for b in (base, base | MIRROR):
            self.uc.mem_map_ptr(b, size, UC_PROT_ALL, ctypes.addressof(buf))

    def _unmapped(self, uc, access, addr, size, value, user):
        """A data access to an unknown device: map a page there (and its
        mirror) as plain memory and log it. Instruction fetches from
        unknown memory are left to fail, so a crash stops the run."""
        if access == UC_MEM_FETCH_UNMAPPED:
            return False
        base = addr & 0xFFFFFF & ~(PAGE - 1)
        if base in self.auto_mapped:
            return False
        self.auto_mapped[base] = uc.reg_read(UC_M68K_REG_PC)
        self.log(f"[map] {addr & 0xFFFFFF:#08x} first touched at pc "
                 f"{uc.reg_read(UC_M68K_REG_PC) & 0xFFFFFF:#08x}")
        self.map_ram(base, PAGE)
        return True

    def r16(self, a):
        return struct.unpack(">H", self.uc.mem_read(a, 2))[0]

    def r32(self, a):
        return struct.unpack(">I", self.uc.mem_read(a, 4))[0]

    def _flush_after_write(self, uc, access, addr, size, value, user):
        pass

    def _apply_after_write(self):
        while self.after_write:
            self.after_write.pop(0)()

    # exceptions: Unicorn reports them and leaves the 68000 stack frame,
    # the vector fetch and RTE to us.

    def _push_frame(self, sr, pc):
        if not sr & 0x2000:
            # switching to supervisor mode swaps A7 to the SSP
            self.uc.reg_write(UC_M68K_REG_SR, sr | 0x2000)
        sp = self.uc.reg_read(UC_M68K_REG_A7) - 6
        self.uc.mem_write(sp, struct.pack(">HI", sr, pc & 0xFFFFFFFF))
        self.uc.reg_write(UC_M68K_REG_A7, sp)

    def _movep(self, uc, pc):
        """Unicorn doesn't implement MOVEP (0000 ddd 1mm 001 aaa, d16):
        moves a word or long to/from every other byte. Emulated here."""
        op = self.r16(pc)
        if op & 0xF138 != 0x0108:
            return False
        dreg = UC_M68K_REG_D0 + ((op >> 9) & 7)
        areg = UC_M68K_REG_A0 + (op & 7)
        mode = (op >> 6) & 7                          # 4 w->reg, 5 l->reg, 6 reg->w, 7 reg->l
        disp = struct.unpack(">h", uc.mem_read(pc + 2, 2))[0]
        addr = (uc.reg_read(areg) + disp) & 0xFFFFFFFF
        n = 4 if mode in (5, 7) else 2
        if mode in (6, 7):
            v = uc.reg_read(dreg)
            for i in range(n):
                uc.mem_write(addr + 2 * i, bytes([(v >> (8 * (n - 1 - i))) & 0xFF]))
        else:
            v = 0
            for i in range(n):
                v = (v << 8) | uc.mem_read(addr + 2 * i, 1)[0]
            if n == 2:
                v = (uc.reg_read(dreg) & 0xFFFF0000) | v
            uc.reg_write(dreg, v)
        uc.reg_write(UC_M68K_REG_PC, pc + 4)
        self.movep_count += 1
        return True

    def _exception(self, uc, intno, user):
        pc = uc.reg_read(UC_M68K_REG_PC)
        sr = uc.reg_read(UC_M68K_REG_SR)
        if intno in (4, 10, 11) and self._movep(uc, pc):
            return
        if intno == 0x100:                              # RTE
            sp = uc.reg_read(UC_M68K_REG_A7)
            new_sr, new_pc = struct.unpack(">HI", uc.mem_read(sp, 6))
            uc.reg_write(UC_M68K_REG_A7, sp + 6)
            uc.reg_write(UC_M68K_REG_SR, new_sr)         # may swap A7 back to USP
            uc.reg_write(UC_M68K_REG_PC, new_pc)
            return
        self.exceptions[intno] += 1
        stacked = pc + 2 if 32 <= intno <= 47 else pc   # TRAP returns past itself
        self._push_frame(sr, stacked)
        uc.reg_write(UC_M68K_REG_SR, (sr | 0x2000) & ~0x8000)
        uc.reg_write(UC_M68K_REG_PC, self.r32(intno * 4))

    # interrupts

    def raise_irq(self, source, level, vector, ack=None):
        self.irq[source] = (level, vector, ack)

    def clear_irq(self, source):
        self.irq.pop(source, None)

    def _take_interrupt(self):
        if not self.irq:
            return
        source, (level, vector, ack) = max(self.irq.items(), key=lambda kv: kv[1][0])
        sr = self.uc.reg_read(UC_M68K_REG_SR)
        if level <= (sr >> 8) & 7 and level != 7:
            return
        pc = self.uc.reg_read(UC_M68K_REG_PC)
        self._push_frame(sr, pc)
        self.uc.reg_write(UC_M68K_REG_SR, (sr & 0x00FF) | 0x2000 | (level << 8))
        self.interrupts[vector] += 1
        if ack:
            ack()
            self.irq.pop(source, None)
        self.uc.reg_write(UC_M68K_REG_PC, self.r32(vector * 4))

    # running

    def run(self, steps, slice_=500):
        done = 0
        while done < steps:
            pc = self.uc.reg_read(UC_M68K_REG_PC)
            try:
                self.uc.emu_start(pc, 0xFFFFFFFF, count=slice_)
            except UcError as err:
                self.log(f"[cpu] stopped at {self.uc.reg_read(UC_M68K_REG_PC):#08x}: {err}")
                return False
            done += slice_
            self._apply_after_write()
            self.m68302.tick(slice_)
            self.duart.tick(slice_)
            self._take_interrupt()
        return True


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--rom", default=os.path.join(HERE, "..", "local", "asr10rom.bin"))
    p.add_argument("--steps", type=int, default=5_000_000)
    p.add_argument("--disk", help="Ensoniq .img (or .hfe) in the floppy drive")
    a = p.parse_args(argv)
    disk = None
    if a.disk:
        data = open(a.disk, "rb").read()
        if data[:8] == b"HXCPICFE":
            sys.path.insert(0, os.path.join(HERE, "..", "tools"))
            import hfe
            data = hfe.hfe_to_img(data)
        disk = data
    m = Machine(open(a.rom, "rb").read(), disk=disk)
    m.run(a.steps)
    print("pc", hex(m.uc.reg_read(UC_M68K_REG_PC) & 0xFFFFFF), "exceptions", dict(m.exceptions),
          "interrupts", dict(m.interrupts))
    print("panel received:", bytes(m.panel.received[:200]).hex(" "))
    print("display:")
    print(m.panel.render())
    return 0


if __name__ == "__main__":
    sys.exit(main())
