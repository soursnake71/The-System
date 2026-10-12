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

**MIDI Out (confirmed on hardware):** when our code takes over, channel A
is not set up yet: status `00`, and enabling the transmitter (CRA `04`,
as OS 3.53 does) leaves it at `00`. Setting it up from scratch works and
the computer receives the data: reset Tx/Rx/errors, MR1 `13` (8 bits, no
parity), MR2 `07` (1 stop bit, no handshake), CSRA `EE` (both directions
from the external clock pins at 16x, so those pins carry 500 kHz as on
MAME's VFX), then CRA `05`. Status then reads `0C` (ready, empty).

## Panel input (confirmed on hardware)

With the panel explorer running, every panel button (arrows and LOAD
included) sends codes on DUART channel B, which the explorer printed.
The keyboard sends nothing there, and neither does the data entry slider.

- Keyboard: read by other hardware. The ASR-10 "tunes" its keyboard at
  power-up, and forum reports describe four LC oscillators under the keybed
  that measure key position. That measurement is what fails calibration on
  this unit. Code for it is in the boot ROM.
- Slider: in MAME's Ensoniq drivers the slider, wheels, pedal and battery
  voltage are analog inputs read through the sound chip's ADC port; the
  ASR-10 probably works the same way.

Still to do: which code each button sends.

## Boot ROM (dumped)

Dumped from the user's ASR-10 over MIDI with the explorer OS
(`tools/syx2rom.py`), 1024 of 1024 chunks with no damage. Split into even
and odd bytes it matches MAME's `asr10` set exactly: ROM **1.5b**, hi CRC
`b37cd3b6`, lo CRC `8e437843`. Kept in `local/asr10rom.bin`, not committed.
The 68000 reset vectors at its start are SSP `0x300`, PC `0x00000C`, i.e.
it boots at address 0 and runs from `0xF80000` afterwards.

### Panel protocol (from the ROM)

- Normal display output is interrupt driven: a ring buffer at RAM
  `0x378-0x3B7` is fed to DUART B one byte per interrupt (`0xF89AA4`).
- Handshaked command send (`0xF89C46`): write the byte, wait about 8000
  loop passes, check RxRDY; retry up to 100 times; the reply comes back
  in d1. Our `panel_tx` does the same thing, which is why it worked.
- Start-up (`0xF89BD6`, called from the RAM test, boot and error paths):
  drain 64 bytes from the panel, then send `E7` (up to 15 tries) and `71`
  (up to 10 tries). MAME's EPS panel model replies to exactly these two.
- `66` then plain ASCII (`0xF89C94`): used for the boot-loader messages
  ("FATAL SYSTEM ERROR", "BAD DISK/NOT EPS DISK", "ERROR - REBOOT ?").
  `66` is ASCII "f", which MAME's EPS display model treats as **clear
  screen**: the ASR-10 panel speaks the EPS-family protocol, not the VFX
  one (`D6`/`83` did nothing).
- `0xF89CF6` sends `7F`, waits, `FF`, waits, `FD`, then waits for a reply;
  reply `C9` sets carry (the failure path). MAME's model treats `FD` after
  `FB` as a calibration request answered with `FF` = calibration OK. This
  is the likely source of the keyboard calibration message. Not called
  directly anywhere; reached through a table, to trace.
- `0xF89D46` is a crash handler: shows "ERROR - REBOOT ?" and lets the
  panel buttons dump registers.

## Display commands (guess)

MAME has three Ensoniq display models. All print `0x20`-`0x5F` at the
cursor, so plain uppercase text is safe. Their control codes disagree:

| | SQ-1 (MAME's ASR-10) | VFX / SD-1 |
|---|---|---|
| clear | `0x83` | `0xD6` |
| cursor | `0x87 rc` | `0x80 + position` (0-79) |

Which one the ASR-10 uses is still open. Watching what the ROM sends would
settle it.

**On the ASR-10 (try 2):** sending `D6 83 " THE SYSTEM "` back to back,
polling only TxRDY, left the screen showing `e     loading system`: one
letter got through and the ROM's text stayed. MAME's EPS panel answers
every byte the main board sends; if the ASR-10 panel does the same, the
board has to wait for that reply before sending the next byte. Try 3
waits for a reply (or a few ms) after each byte.

**Try 3 worked:** the user reported the display reading "the system
system". Our text arrived, and neither `D6` nor `83` cleared the screen, so
" THE SYSTEM " overwrote the start of the ROM's "LOADING SYSTEM" and its
last word stayed. **Confirmed on hardware:** the OS file layout and
takeover, DUART channel B at `0xFC4800` as the panel link, and plain
uppercase text printing at the cursor. The ROM prints lowercase on screen,
or the user typed it that way; to check.

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

The table covers the file exactly (2 + 171 = 173 blocks).

**OS 3.53** (from the user's own boot disk) uses the same header and the
same ROM handler addresses. Its load table differs only in the third
segment: `0x9C00`, `0x102` blocks, so low RAM runs to at least `0x2A000`.
The file is 382 blocks. Evidence: 1291 of
1726 short calls into `0xFF8000+` land after a return or branch with this
layout. So do 121 of 170 long calls into `0x8000-0xFFFF`, and 13 of 13
calls into `0xFC6000+`.

The ROM uses the vectors it finds in the header *while it loads the rest
of the file*. First test on the ASR-10: pointing every vector at our code
hung the machine at "LOADING SYSTEM" with the drive selected, almost
certainly because a disk or timer interrupt jumped into code not yet
loaded. So only the vectors whose stock handler is in the OS itself are
safe to take over. In 3.53 those are vectors 4 and 11 (to `0xFC6000`/
`0xFC6014`), traps 10 and 11, and 68302 interrupts 74, 75, 77 and 81. The
longs at `0x0C0`-`0x0FF` (vectors 48-63) are settings data, not vectors.

**Still unknown:** where the ROM jumps after loading. The jump table at
`0xFF801E` is OS services, not a start address. Our OS doesn't need to
know: every vector points at our code, and every unused byte of the
segments is a NOP slide into it (`tools/make_os.py`).

## Floppy format

Checked against OS 1.61 and nine of the user's disks (HD and DD): layout
matches. HD disk, 3200 blocks, FAT at block 5 (19 blocks), data from
block 24. DD disk, 1600 blocks.

Disks written by the ASR-10 can leave FAT chains from deleted files that
no directory entry owns. The free count in block 2 counts those blocks as
free, so it can be higher than the number of zero FAT entries. The disk
tool trusts block 2 and adjusts it by the change in size.

## HFE files (the Gotek's format)

From the user's HFE files: HFE v1, MFM, 80 cylinders, 2 sides, sectors
numbered from 0, gap 3 = 36, index mark at byte 95, first sector ID at
byte 161, sectors 610 bytes apart. HD: 500 kbit/s, interface mode 1, about
50,024 bytes per track. DD: 250 kbit/s, interface mode 0, 25,088. Track
length varies a little between files. `tools/hfe.py` writes this layout;
Greaseweazle's `ensoniq.1600` decoder reads it back unchanged.

## Next questions

1. Does OS 3.53 use the same header and load table? Run `make disk` on your
   image; `make_os.py` checks the table against the file size.
2. Does the first test boot? See README.
3. What does the ROM send to the panel at start-up? That settles the display
   commands.
4. What is at `0xFC2000`? Then the OTTO and ESP addresses, from code that
   plays sounds and loads effects.
