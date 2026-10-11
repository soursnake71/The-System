# ASR-10 hardware notes

Target unit: ASR-10 keyboard (reported as 49 keys, see note below), SCSI board fitted,
RAM fully expanded (16 MB of sample RAM), boots OS 3.53 from a Gotek running
FlashFloppy.

Each fact has a source and a confidence level. **Confirmed** means checked
against the stock OS or the real machine. Nothing is confirmed yet.

> The standard ASR-10 keyboard has 61 keys; the ASR-10R is the rack. If your
> unit really has 49 keys it may be a different model or modified, so check
> the label on the back panel.

## Chips

| Part | Chip | Source | Confidence |
|---|---|---|---|
| CPU | Motorola MC68302 (68000 core, 3 serial channels, timers, IDMA, chip selects) | MAME `esqasr.cpp` | likely |
| Sound | Ensoniq ES5506 "OTTO" (32 voices) | MAME `esqasr.cpp` | likely |
| Effects | Ensoniq ES5510 "ESP" | MAME `esqasr.cpp` | likely |
| Floppy controller | NEC uPD72069 | MAME `esqasr.cpp` | likely |
| UART | 2681 DUART | MAME `esqasr.cpp` | likely |
| SCSI (option) | probably a WD33C93 like the EPS-16+ | guess | unknown |
| Display | 2 x 40 vacuum fluorescent | MAME, front panel | likely |

These replace what I said before looking at MAME: the sound chip is the
ES5506 (OTTO), not the ES5505 (OTIS), and the floppy controller is NEC, not
Western Digital.

## Memory map

MAME's ASR-10 driver is a skeleton marked "not working". It has:

```
0x000000-0x03ffff   boot ROM (2 x 128 KB EPROMs, v1.5 in MAME's set)
0xfb0000-0xfcffff   OS RAM (MAME comment; its map just puts RAM at 0xf00000-0xffffff)
```

Everything else (OTTO, ESP, DUART, FDC, SCSI, sample RAM, the 68302's own
register block) is unmapped there. The 68302 sets its chip selects and
register base from software, so the real map is whatever the boot ROM
programs. It has to be read from the boot ROM or the OS.

## Front panel

MAME uses its `esq2x40_sq1` display model for the ASR-10. In that model:

- `0x20`-`0x7f` print a character and move the cursor right
- `0x83` clears the display
- `0x87 rc` moves the cursor (row in the high nibble, column in the low)

A 4-bit column can't reach all 40 columns, so the cursor command is
probably incomplete. On other Ensoniq models the panel is a separate
microcontroller on a serial line that also sends button presses, slider and
wheel positions. The ASR-10 is probably the same, but which serial channel
it uses and at what speed is unknown.

## Boot process

From the user's manual and how the machine behaves: the ROM starts up,
looks for a disk, loads the OS file into RAM and runs it. **Unknown:** how
the ROM finds the OS file (by file type? by directory slot?), where it loads
it, whether it checks a header or checksum, and where it jumps.

## Floppy format

See `tools/ensoniq_disk.py`. Run `info` on the stock 3.53 image to check the
layout; it reports any mismatch.

## How to fill in the unknowns

1. `python3 tools/ensoniq_disk.py ls local/asr10-os353.img` to find the OS file.
2. `python3 tools/ensoniq_disk.py extract local/asr10-os353.img <slot> local/os.bin`
3. Look at the first blocks of `os.bin` for a header (load address, length, entry).
4. `make disasm OS=local/os.bin ORG=<load address>` and follow:
   - writes to the 68302 BAR (0x0F2) and chip-select registers → memory map
   - the serial channel setup and the bytes it sends at start-up → panel link
   - accesses to the OTTO and ESP → their addresses
5. Write each finding here and mark it confirmed.
