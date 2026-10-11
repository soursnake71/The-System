#!/usr/bin/env python3
"""Run a built OS file on an emulated 68000 and record what it sends out.

Loads the file the way the ASR-10 boot ROM does (header to RAM 0, segments
per the load table at 0x1C0), enters at the given address, and captures
bytes written to the DUART's MIDI and panel transmit registers. The DUART
always reports "ready to send"; nothing else on the board is emulated.

Usage: emulate.py <os-file> [entry-hex] [instructions]
Needs: pip install unicorn
"""

import struct
import sys

from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.m68k_const import UC_M68K_REG_PC, UC_M68K_REG_SR, UC_CPU_M68K_M68000

THRA, THRB, SRA, SRB = 0xFC4807, 0xFC4817, 0xFC4803, 0xFC4813


def run(os_file, entry, count):
    data = open(os_file, "rb").read()
    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    uc.ctl_set_cpu_model(UC_CPU_M68K_M68000)
    uc.mem_map(0x000000, 0x40000)   # low RAM (OS 3.53 loads up to 0x2A000)
    uc.mem_map(0xFC4000, 0x1000)    # DUART page
    uc.mem_map(0xFC6000, 0x1000)    # 68302 internal RAM and registers
    uc.mem_map(0xFF8000, 0x8000)    # high RAM
    uc.mem_write(0, data[:0x400])
    off = 0x400
    for i in range(8):
        dest, blocks = struct.unpack(">II", data[0x1C0 + 8 * i:0x1C8 + 8 * i])
        if not dest:
            break
        uc.mem_write(dest & 0xFFFFFF, data[off:off + blocks * 512])
        off += blocks * 512
    out = {"midi": bytearray(), "panel": bytearray()}

    def on_write(uc, access, addr, size, value, user):
        if addr == THRA:
            out["midi"].append(value & 0xFF)
        elif addr == THRB:
            out["panel"].append(value & 0xFF)

    def on_read(uc, access, addr, size, value, user):
        if addr in (SRA, SRB):
            uc.mem_write(addr, b"\x04")  # TxRDY

    uc.hook_add(UC_HOOK_MEM_WRITE, on_write, begin=0xFC4000, end=0xFC4FFF)
    uc.hook_add(UC_HOOK_MEM_READ, on_read, begin=0xFC4000, end=0xFC4FFF)
    uc.reg_write(UC_M68K_REG_SR, 0x2700)  # we arrive from the ROM or an exception: supervisor
    uc.emu_start(entry, 0xFFFFFFFF, count=count)
    return out, uc.reg_read(UC_M68K_REG_PC)


if __name__ == "__main__":
    entry = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0xE00
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 3_000_000
    out, pc = run(sys.argv[1], entry, count)
    print(f"entered {entry:#x}, stopped at {pc:#x}")
    print("panel:", bytes(out["panel"]))
    print("midi: ", out["midi"].hex(" "))
