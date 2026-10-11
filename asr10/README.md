# ASR-10 OS

A from-scratch operating system for the Ensoniq ASR-10, loaded from floppy
(a Gotek running FlashFloppy) in place of Ensoniq's OS 3.53.

## Why this is safe

The ASR-10's boot ROM loads the OS from disk into RAM each time it powers
up. A new OS is just a file in a disk image on the USB stick. If it crashes,
switch off and boot the stock 3.53 image again. No chips are changed.

## Status

| Step | State |
|---|---|
| Cross-compiler and build | done (`make`) |
| Disk image tool: inspect, extract, replace the OS file | done, tested on synthetic images |
| Check disk format against the real 3.53 image | needs your image |
| Find the load address and memory map in OS 3.53 | needs your image |
| Panel driver: text on the display | written, but can't send anything until the panel's serial port is known |
| First boot on the ASR-10 | — |
| Buttons, slider, keyboard | — |
| Play a sample through the OTTO | — |
| Floppy and SCSI drivers, MIDI, effects | — |

`docs/hardware.md` lists what is known about the hardware, where each fact
comes from and how sure it is.

## Layout

```
Makefile               build, test, make a disk, disassemble
src/                   the OS (68000 C and assembly)
tools/ensoniq_disk.py  Ensoniq disk image tool
tests/                 tests for the disk tool
docs/hardware.md       hardware notes
local/                 your Ensoniq disk images and ROM dumps (not committed)
```

## Setup

Needs `make`, Python 3 and a 68000 cross-compiler. On Debian or Ubuntu:

```
sudo apt install gcc-m68k-linux-gnu binutils-m68k-linux-gnu
```

## Use

```
make                    # build/system.bin
make test               # disk tool tests

# with your stock disk at local/asr10-os353.img
python3 tools/ensoniq_disk.py info local/asr10-os353.img
python3 tools/ensoniq_disk.py ls   local/asr10-os353.img
make disk STOCK=local/asr10-os353.img OS_ENTRY=<slot>   # build/system.img
```

Copy `build/system.img` to the Gotek's USB stick next to the stock image
and pick it on the Gotek. **Don't do this yet:** `LOAD_ADDR` in the Makefile
is a placeholder, so this image won't boot until that address is found in
OS 3.53.

## Ensoniq material

Ensoniq's OS and ROMs are their copyright. Keep your copies in `local/`,
which git ignores. Nothing from them goes in this repository.
