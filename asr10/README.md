# ASR-10 OS

A from-scratch operating system for the Ensoniq ASR-10, loaded from floppy
(a Gotek running FlashFloppy) in place of Ensoniq's OS 3.53.

## Why this is safe

The ASR-10's boot ROM loads the OS from disk into RAM each time it powers
up. A new OS is just a file in a disk image on the USB stick. If it crashes,
switch off and boot your stock disk again. No chips are changed.

## Status

| Step | State |
|---|---|
| Cross-compiler and build | done (`make`) |
| Disk image tool | done, checked against OS 1.61 and your own disks |
| HFE conversion (for your Gotek) | done, matches your HFE files; checked with Greaseweazle |
| OS file layout (header, load table, memory map) | found in OS 1.61, see `docs/hardware.md` |
| First boot test: text on the display, a note on MIDI Out | try 1 hung at "LOADING SYSTEM"; try 2 showed a stray "e" (our code ran, panel dropped bytes); try 3 ready |
| Display commands (clear, cursor) | two candidate sets, not settled |
| Buttons, slider, keyboard | — |
| Play a sample through the OTTO | — |
| Floppy and SCSI drivers, effects | — |

## Layout

```
Makefile               build, test, make a disk, disassemble
src/                   the OS (68000 C and assembly)
tools/ensoniq_disk.py  Ensoniq disk image tool
tools/make_os.py       wraps our code in an ASR-10 OS file
tools/hfe.py           converts .img to and from .hfe
tests/                 tests, and a 68000 emulator harness (emulate.py)
docs/hardware.md       hardware notes
local/                 your Ensoniq disk images and ROM dumps (not committed)
```

## Setup

Needs `make`, Python 3 and a 68000 cross-compiler. On Debian or Ubuntu:

```
sudo apt install gcc-m68k-linux-gnu binutils-m68k-linux-gnu
pip install unicorn        # optional, for the emulator test
```

## Build a test disk

Put your stock OS disk (`.hfe` or `.img`) in `local/`, then:

```
make test
make hfe STOCK=local/_AUTOBOOT.HFE
```

This makes `build/THESYSTEM.hfe` (and `build/system.img`): a copy of your
stock disk whose OS file is replaced by ours. It uses the header and load
table from your stock OS file and checks they add up.

## Try it on the ASR-10

1. Keep your stock boot disk on the USB stick as it is. Copy
   `build/THESYSTEM.hfe` next to it.
2. Connect MIDI Out to something that makes sound or shows MIDI (a synth on
   channel 1, or a MIDI monitor on a computer).
3. Power on, pick `THESYSTEM.hfe` on the Gotek, and let it boot.
4. Expected: ` THE SYSTEM ` appears on the display, and middle C plays once
   a second on MIDI Out. The panel buttons won't do anything.
5. Turn the power off, pick the stock image, and boot as usual.

Please report what you see, even if it's nothing. "No text, but the note
plays" or "it shows ' THE SYSTEM ' after the Ensoniq text" both tell us a
lot.

## Ensoniq material

Ensoniq's OS and ROMs are their copyright. Keep your copies in `local/`,
which git ignores. Nothing from them goes in this repository.
