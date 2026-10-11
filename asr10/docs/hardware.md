# ASR-10 hardware notes

Target unit: ASR-10 keyboard (reported as 49 keys, see note below), SCSI
board fitted, RAM fully expanded, boots OS 3.53 from a Gotek running
FlashFloppy.

> The standard ASR-10 keyboard has 61 keys; the ASR-10R is the rack. If your
> unit really has 49 keys it may be a different model or modified, so check
> the label on the back panel.

Confidence levels:
- **from OS 1.61**: read out of the OS 1.61 disk (archive.org,
  `asr-10-os-v-161`, SHA-1 `c4847578…`) by disassembly. Strong, but not yet
  seen on real hardware, and not yet checked against 3.53.
- **MAME**: from MAME's source. Its ASR-10 driver is a skeleton marked
  "not working", so treat as a lead.
- **guess**: unverified.

## Chips

| Part | Chip | Source |
|---|---|---|
| CPU | Motorola MC68302 | MAME; OS 1.61 uses 68302 registers |
| Sound | Ensoniq ES5506 "OTTO" | MAME |
| Effects | Ensoniq ES5510 "ESP" | MAME |
| Floppy controller | NEC uPD72069 | MAME |
| UART | 2681 DUART | MAME; OS 1.61 |
| Display | 2 x 40 vacuum fluorescent | front panel |

## Memory map (from OS 1.61)

| Address | What | Notes |
|---|---|---|
| `0x000000-0x00FFFF` | RAM | vector table at 0, OS loads from `0x000E00` |
| `0xF80000-0xFBFFFF` | boot ROM, 256 KB | the OS calls ~1000 routines here; exception handlers live here |
| `0xFC2000` | unknown 8-bit device, odd bytes | used a lot near disk code; maybe the floppy controller |
| `0xFC4800` | 2681 DUART, odd bytes: register *n* at `0xFC4801 + 2n` | |
| `0xFC6000` | MC68302 internal RAM (`+0x000`) and registers (`+0x800`) | |
| `0xFF8000-0xFFFFFF` | RAM, 32 KB | separate from `0x8000-0xFFFF`: OS loads different data into each |

Most of the operating system (all the screen text, for example) is in the
boot ROM. The OS file on disk adds code the ROM calls through the vector
table, traps and jump tables.

## DUART (from OS 1.61)

| Channel | Use | Evidence |
|---|---|---|
| A | MIDI | sends a buffer through `0xFC4807` and checks for SysEx `F0`/`F7` |
| B | front panel | reads `0xFC4817`; codes `0xC0` and up are handled separately |

The disk OS never writes to channel B, so display output is done by the
ROM. MAME runs the VFX panel link at 62,500 baud, 8N2.

## Display commands (guess)

MAME has three Ensoniq display models. All print `0x20`-`0x5F` at the
cursor, so plain uppercase text is safe. Their control codes disagree:

| | SQ-1 (MAME's ASR-10) | VFX / SD-1 |
|---|---|---|
| clear | `0x83` | `0xD6` |
| cursor | `0x87 rc` | `0x80 + position` (0-79) |

Which one the ASR-10 uses is still open. Watching what the ROM sends would
settle it.

## The OS file (from OS 1.61)

Directory file type 32. The first two blocks are a header that is an image
of RAM `0x000-0x3FF`:

- `0x000-0x15F`: 68000 vectors 0-87. Most point at ROM handlers;
  traps 10 and 11 point into the OS at `0xFF88E8`/`0xFF88E2`.
- `0x160-0x19F`: settings, including the disk label `DISK000` at `0x184`.
- `0x1C0`: load table, entries of (destination, block count), each 32-bit
  big-endian, ended by zero:

| File offset | Loads to | Blocks |
|---|---|---|
| `0x00400` | `0x000E00` | `0x39` |
| `0x07600` | `0xFF8000` | `0x40` |
| `0x0F600` | `0x009C00` | `0x31` |
| `0x15800` | `0xFC6000` | `0x01` |

The table covers the file exactly (2 + 171 = 173 blocks). Evidence: 1291 of
1726 short calls into `0xFF8000+` land after a return or branch with this
layout. So do 121 of 170 long calls into `0x8000-0xFFFF`, and 13 of 13
calls into `0xFC6000+`.

**Still unknown:** where the ROM jumps after loading. The jump table at
`0xFF801E` is OS services, not a start address. Our OS doesn't need to
know: every vector points at our code, and every unused byte of the
segments is a NOP slide into it (`tools/make_os.py`).

## Floppy format

Checked against the OS 1.61 image: `ensoniq_disk.py info` reports no
mismatches. HD disk, 3200 blocks, FAT at block 5 (19 blocks), data from
block 24.

## Next questions

1. Does OS 3.53 use the same header and load table? Run `make disk` on your
   image; `make_os.py` checks the table against the file size.
2. Does the first test boot? See README.
3. What does the ROM send to the panel at start-up? That settles the display
   commands.
4. What is at `0xFC2000`? Then the OTTO and ESP addresses, from code that
   plays sounds and loads effects.
