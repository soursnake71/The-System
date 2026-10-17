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
| First boot: our code runs and writes text on the display | **works** (try 3) |
| Display: clear, cursor, steady text | **works**: `66` clear, `60 nn` cursor (0-79), `77 c` steady character |
| Panel buttons | **works**: `80+n` pressed, `n` released |
| Panel explorer: shows the code of each button press | **works** for buttons; keyboard and slider use other hardware |
| MIDI Out from our OS | **works** (port set up from scratch) |
| ROM dump over MIDI SysEx (`tools/syx2rom.py`) | **done**: ROM 1.5b, verified against MAME's checksums |
| Keyboard calibration message on the stock OS | **gone** with `make stock-patched` (one byte; confirmed: boots and loads banks); cause of the failure still to find |
| Emulation engine (`emu/asr10.py`) | runs the real ROM: boots, loads a disk, runs **our OS** end to end; Ensoniq's OS 3.53 starts but crashes (timing) |
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
tools/patch_os.py      checked one-byte patches to your own copy of Ensoniq's OS
tools/syx2rom.py       rebuilds the boot ROM from the explorer's SysEx dump
tools/rom-recorder.html  records that dump in Chrome (Web MIDI) and shows progress
tests/                 tests, and a 68000 emulator harness (emulate.py)
emu/                   ASR-10 emulation engine: runs the real ROM (needs local/asr10rom.bin)
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

## Run it in the emulator

```
pip install unicorn
python3 emu/asr10.py --disk build/system.img --steps 30000000
```

Boots the ROM from `local/asr10rom.bin`, loads the disk, and prints the
display. `--disk` takes `.img` or `.hfe`.

## Ensoniq material

Ensoniq's OS and ROMs are their copyright. Keep your copies in `local/`,
which git ignores. Nothing from them goes in this repository.
