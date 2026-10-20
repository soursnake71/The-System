# E-mu SP-1200 Hardware Architecture (seen by a firmware developer)

Research date: 2026-10-20. Main primary source: the E-mu SP-1200 Service Manual (Rev 1, 1987; schematic set SK103 Rev D, dated 870717). A scan is on the Internet Archive ([archive.org item](https://archive.org/details/emu-sp-1200-service-manual-1987); [PDF](https://archive.org/download/emu-sp-1200-service-manual-1987/Emu-SP-1200-Service-Manual-1987.pdf)). Page numbers below are the manual's printed page numbers. The archive.org OCR text layer is almost empty (only watermarks), so I read the pages as rendered images.

Second main source: the GitHub repo [Lytrix/EMU-SP1200](https://github.com/Lytrix/EMU-SP1200), last commit 2026-05-14. It holds a KiCad redraw, a BOM, PROM/PAL dumps, the SP-1200 boot EPROM image, SP-12 ROM images, HxC disk images including an OS disk, and a signal-name table transcribed from the manual.

The I/O port map below comes from my own disassembly of the boot EPROM image (using the `z80dis` Python package), checked against schematic sheet 4. It is labelled as inference wherever the manual does not state it.

---

## Q1. CPU, clock, boards, ROM and RAM (sizes and chip types)

### Takeaway
The CPU is confirmed as a Zilog Z-80A at 3.3 MHz. ROM is a single 8 KB 2764 "Mainboot" EPROM, and the CPU has 64 KB of DRAM ("Program Memory", two 4464 64K×4 chips). The boot EPROM only loads the operating system from floppy into that DRAM at power-up. The OS, its variables, the stack and the sequencer data all live in the 64 KB DRAM. The CPU has Z-80 CTC and Z-80 SIO/2 peripherals and a custom TTL "microcontroller" that plays the voices.

### Cited Findings
- "The SP1200 uses a Z-80A processor running at 3.3 mHz. Its memory consists of 8K bytes of EPROM memory and 64K of dynamic RAM. A PAL is used to decode the CPUs memory." — [SP-1200 Service Manual p.43](https://archive.org/details/emu-sp-1200-service-manual-1987)
- "The instructions that boot the machine are contained in EPROM. The main operating instructions are loaded into RAM from the floppy disk on power up. This RAM called the Program Memory also contains the program variables, CPU stack and sequencer memory." — [Service Manual p.39](https://archive.org/details/emu-sp-1200-service-manual-1987)
- The CPU + front panel block diagram (Fig. 3, p.42) shows: Z-80 CPU; "Dynamic Ram 64Kx8"; "EPROM 8Kx8"; "ADC 8 inputs"; CTC; SIO; a display on A0/A1; an LED latch; a button row latch, decoder and bus driver; and control logic ("decoders, gates, PALs, clocks, power ok and reset"). — [Service Manual p.42](https://archive.org/details/emu-sp-1200-service-manual-1987)
- The parts list gives these main-board ICs. Digital: Z-80A CPU (IM343), Z-80A SIO/2 (IM344), Z-80A Counter/Timer (IM346). Memory: 2764 Mainboot EPROM (IP362), 14 × 4464 64K×4 RAM, 6 × 2114 SRAM 4K 300 ns, 74S288 µC PROM, 74S288 Log PROM, and four PALs ("Z-80 RAM PAL", "Chip Select PAL", "Sound Mem. PAL", "DRAM PAL"). Also a 20 MHz crystal. — [Service Manual pp.86–89](https://archive.org/details/emu-sp-1200-service-manual-1987)
- Schematic sheet 1 is titled "SP1200 MAIN – Clocks and CPU" (doc SK103, Rev D, 870717, page 1 of 18). The set has 18 main-board sheets plus front-panel and "touch mod" sheets, some carried over from the SP-12. — [Service Manual schematics section](https://archive.org/details/emu-sp-1200-service-manual-1987)
- The manual's signal list names the clocks: +C20M.D (20 MHz), +C2MHZ.D (2 MHz, to sheet 2 = CTC/SIO), +C3MHZ.D (3 MHz), −C6MHZ (6 MHz), +PHI.D (Z-80 clock). Z-80 control lines: −NMI, −INT (from sheet 2), −WAIT (from the sound memory logic, sheet 13), −RFSH (sheet 3 DRAM). — [Lytrix SP1200_Signal_Definitions.md](https://github.com/Lytrix/EMU-SP1200/blob/main/Plan/SP1200_Signal_Definitions.md), transcribed from the manual's "Signal Names" section
- The boot EPROM dump in the Lytrix repo is `BIN files/ip362b-emu-2588.bin`: 8,192 bytes, SHA-1 a538c7a14948f308739a1e5658bcec7cad052095. Its strings include "SP-1200  REV B", "Loading Software", "Insert Diskette", "Disk Read Error", "1 = Cassette / 2 = MidiDisk". The file name matches E-mu part IP362 (the 2764 Mainboot EPROM). — [Lytrix/EMU-SP1200 BIN files](https://github.com/Lytrix/EMU-SP1200)
- Forum users confirm the boot flow: the OS lives on the disk, the machine loads the disk at start-up, and "Format/Copy Software" writes OS plus memory so that a disk is bootable. — [HxC forum p=9811](https://hxc2001.com/floppy/forum/viewtopic.php?p=9811); [HxC forum p=7503](https://hxc2001.com/floppy/forum/viewtopic.php?p=7503); [torlus.com p=22342](https://torlus.com/floppy/forum/viewtopic.php?p=22342)
- Holding Setup at power-on runs the built-in diagnostics, according to moderators on the SP-1200 forum. — [Tapatalk sp1200 forum](https://www.tapatalk.com/groups/sp1200/black-sp-1200-does-not-boot-on-the-software-t1828.html)

### Inferences (from my disassembly of the boot ROM; not stated in the manual)
- Reset path: `JR 0069h` → `LD SP,6200h`. The code initialises the LCD, switches all LEDs on, fills all 64 KB with FFh (a write loop from 0000h, so writes "under" the ROM reach DRAM), then turns the LEDs off. That looks like a lamp test plus memory clear. It then selects `IM 2`.
- Once loading has succeeded, the loader jumps to `0002h`, which holds `OUT (58h),A`. Execution then falls through to `0004h`. The Rossum "Main FW Rev 1.04" image (see Q4) begins with four NOPs at 0000h and `JP 0079h` at 0004h. So writing to port 58h most likely switches the boot EPROM out and exposes RAM at 0000h ("shadow ROM" scheme), and the OS image is loaded to address 0000h. This is consistent with the manual's "Z-80 RAM PAL" and with −CSPB feeding the RAM decode (see Lytrix's IC130 note). It needs schematic confirmation (sheet 3, Program Memory).
- Z80 clock: 20 MHz / 6 = 3.33 MHz, which matches the manual's "3.3 MHz". Assumed from the 20 MHz crystal plus the 3/6 MHz clock names.
- 14 × 4464 = 2 for the Z-80's 64 KB (64K×8) + 12 for the 256K-word sound memory (8 "MSB" chips + 4 "LSB" chips, per Fig. 6). The arithmetic fits, but the split is my inference.
- **Board count:** the parts list has "Main Board", "Front Panel" and "Chassis" sections, and there are separate front-panel schematics. So there is effectively one main logic board plus a front-panel board, with the floppy drive and power transformer in the chassis.

### Gaps
- The memory map of the 64 KB Program Memory (where the OS, sequencer data, variables and stack sit) is not in the theory-of-operation text. Sheet 3 ("Program Memory") and the IC130 PAL16R4 equations would settle it, and the PAL is not dumped (Lytrix lists it as missing).
- The exact Z80 interrupt vector (I register) setup is unconfirmed. The boot ROM uses IM 2. The Rossum image contains `LD I,A`.

---

## Q2. Sample memory, playback/DAC circuitry, filters, and how the CPU drives voices

### Takeaway
Sound memory is 256K × 12-bit words in 4464 DRAMs, in four 2.5-second banks that a sound cannot cross. It sits behind a non-programmable, microcoded TTL "microcontroller" (µC). The Z-80 never streams samples. It writes per-channel start address, length, pitch increment, loop data, bank and level into the µC's register file through five I/O ports, then sets a request latch. The µC time-multiplexes 8 channels through one 12-bit DAC (7541). An 8-bit multiplying DAC (7523, fed via a log PROM) sets each channel's level. A 4051 demultiplexes the DAC output into eight LF398/cap sample-and-holds and the per-channel filters. Pitch shifting is done by drop/duplicate sample addressing, not by changing the rate. Sampling reuses the main DAC as a successive-approximation ADC under CPU control, with the CPU held in WAIT.

### Cited Findings
- "The sound memory is composed entirely of 4464 Dynamic RAMs which are four bits wide by 64K for a total sound memory size of 256K words. The sound memory is divided up into 4 banks of 2.5 seconds each. Sounds cannot cross over into another bank." — [Service Manual p.41](https://archive.org/details/emu-sp-1200-service-manual-1987)
- "The DAC is a standard 12 bit linear device. It is used both for playing and sampling sounds." "For signal level control a standard 8 bit multiplying DAC is used as an attenuator… The level value is stored in a register in the microcontroller." — [Service Manual p.41](https://archive.org/details/emu-sp-1200-service-manual-1987)
- The parts list gives the 7541 12-bit MDAC (main DAC), 7523 MDAC (level DAC), ADC0809 (slider ADC), LF398 S/H, 4051 analog mux, 2 × SSM2044 VCF, 74HCT670 4×4 register files (×4), and 74LS283 adders (×6). — [Service Manual pp.86–87](https://archive.org/details/emu-sp-1200-service-manual-1987)
- Memory layout: to stay compatible with the SP-12 (which used byte-wide static RAM), 12-bit data is split into a byte plus a nibble. "Sound memory address 0 switches the source of the low order nibble every other word. LSB RAMs A and B are alternately switched by a 1 of 2 data selector (IC 90)… The CPU can read or write memory through the bi-directional data latches IC70 and IC71." — [Service Manual p.47](https://archive.org/details/emu-sp-1200-service-manual-1987)
- Sound memory address signals SMA0–SMA18 (19 bits; SMA16–18 come from the µC bank register), with −RAS3/4/5 and −SMACAS. The sound data bus is SDB4–11 plus SDB0–3 split into A/B halves. — [Lytrix signal table (from manual)](https://github.com/Lytrix/EMU-SP1200/blob/main/Plan/SP1200_Signal_Definitions.md)
- The µC: "is in control of playing the sounds. The CPU tells it where the sound is, the length, pitch, and loop points if any, then the µC generates all the signals to get the sound out of memory and to the DAC and sample/hold… It is not programmable so we call it a microcontroller… has a bank select register for addressing memory beyond the 16 bits of the address latch output." — [Service Manual p.51](https://archive.org/details/emu-sp-1200-service-manual-1987)
- The Z-80 talks to the µC in five places: (1) the register file input latch (address, size, pitch, loop data); (2) the register file RAM address latch; (3) the sound level register; (4) the bank select register (selects the 64K block); (5) the request latch, which the µC reads before copying the latched data into register-file RAM. "The CPU writes to the µController as 5 separate I/O ports." — [Service Manual p.51](https://archive.org/details/emu-sp-1200-service-manual-1987)
- "To play a sound the CPU writes the Start Address, the Sound Length and the Pitch of the sound into the Input Registers… also writes the Bank Number and the Sound Level… The Start and Size numbers get stored in the 2114 memory chips. Their locations in the 2114s correspond to the output channel number." — [Service Manual pp.51–53](https://archive.org/details/emu-sp-1200-service-manual-1987)
- Pitch: on each channel's cycle, the pitch number is loaded into the Increment Latch and added to the previous value. The carry out picks "play same sample twice" (0) or "increment" (1). With A0 forced to 1 and the carry always 1, the address advances by 2, which is an octave up. Intermediate 7-bit values (neither 0000000 nor 1111111) give semitone steps, and "the CPU only sends values to the µController pitch register which create semitone tuning intervals." — [Service Manual p.53](https://archive.org/details/emu-sp-1200-service-manual-1987)
- Fig. 9 (µC block diagram) shows: input register 74HCT374; increment latch 74HCT377; RAM address latch 74HCT174 (6 bits) with HCT157 mux; 2114 register-file memories; LS283 adders; HCT374 sum registers; HCT377 output registers to sound memory; a microcode PROM driven by a µController counter and current-channel counter; bank select register; sound level register → Log PROM → level DAC; and the analog demux. — [Service Manual p.52](https://archive.org/details/emu-sp-1200-service-manual-1987)
- "The µC de-multiplexes the signal using a 4051 and sample/holds it using caps and bi-FET op amps." Two channels (7, 8) are direct outs; "The rest of the channels are filtered." — [Service Manual pp.41, 51](https://archive.org/details/emu-sp-1200-service-manual-1987)
- Sampling: an anti-alias filter feeds the S/H, then a comparator and a SAR that drives the 12-bit main DAC. The low 4 bits are written only every other sample, with a latch holding the intermediate nibble. "During the conversion process the CPU is in a wait state." The PAL makes the CPU read the data through IC70/71 while RAM is written, "so that it can update the display VU meter." — [Service Manual pp.48–51](https://archive.org/details/emu-sp-1200-service-manual-1987)
- The PROM/PAL names and status in the voice and sample path: Control Sequencer IC67 74S288 (not yet dumped), Register Files/Log IC88 74S288 (dumped), Sequencer Memory IC62 PAL12H6 (dumped), Program Memory IC130 PAL16R4 (not dumped). — [Lytrix README](https://github.com/Lytrix/EMU-SP1200)
- Filter layout per Wikipedia: two dynamic SSM2044 filters on channels 1–2, fixed lowpass filters on the others, channels 7–8 unfiltered, and no reconstruction filter. — [Wikipedia: E-mu SP-1200](https://en.wikipedia.org/wiki/E-mu_SP-1200)
- On the dynamic-filter channels, the SSM2044 is swept by a simple envelope that the Z-80 generates (a forum post describes a ~5 ms sloping attack then decay). — [dxarmy/Tapatalk sp1200 forum "Adjusting the 1200's filters"](https://www.tapatalk.com/groups/sp1200/adjusting-the-1200-s-filters-t561.html) (forum source; not verified against the schematic)
- Signal list entries: GAIN1/GAIN2 "Env/Gain Control Bit" go to sheets 6 and 16, and "Dyn. Filt Out Ch. 0/1" are channels 0/1, which matches two SSM2044 channels. — [Lytrix signal table](https://github.com/Lytrix/EMU-SP1200/blob/main/Plan/SP1200_Signal_Definitions.md)
- Spec: 26.04 kHz, 12-bit linear, 8 voices, 10 s total, 2.5 s max per sound. — [Wikipedia: E-mu SP-1200](https://en.wikipedia.org/wiki/E-mu_SP-1200)

### Inferences
- 256K words / 26,040 Hz ≈ 10.07 s and 64K words ≈ 2.52 s, which matches "10 seconds" and "4 banks of 2.5 s". The bank number (SMA16–17) is therefore the high part of a sound address, and firmware must keep each sound inside one 64K-word bank.
- 20 MHz / 768 = 26,041.7 Hz, which suggests the sample rate is derived from the 20 MHz crystal through the µC state counter (UCNT0–2 × 8 channels). This is unverified; the manual text does not give the divider.
- For firmware: per-voice parameters live in µC register-file RAM, indexed by channel through the 6-bit register-file address latch. The likely write sequence is: write the reg-file address → write lo/hi data bytes → set the request latch. The µC then copies the data on its next cycle. The decay envelope and SSM2044 sweep appear to be CPU-generated, by updating level and filter values at the interrupt rate.
- The Lytrix README describes the same pitch scheme in hobbyist terms: 0000000 skips (+1 octave), 1111111 doubles (−1 octave). — [Lytrix README](https://github.com/Lytrix/EMU-SP1200)

### Gaps
- The exact register-file layout (which reg-file addresses hold start, size, pitch, loop for each channel) and the microcode PROM (IC67) contents are undocumented. IC67 is not dumped, and the manual describes it only functionally.
- How the SSM2044 cutoff CV is generated (a DAC/S&H channel, or the level DAC) is not described in the theory text I read. Schematic sheets 15–17 would show it.

---

## Q3. Peripherals: display, keypad, sliders, floppy, MIDI, SMPTE/sync, timers and interrupts

### Takeaway
- **Display:** a 16×2 character LCD on the data bus at I/O addresses A0h–A2h. The init bytes 38h/0Ch/01h and busy-flag polling suggest an HD44780-compatible controller (inference).
- **Buttons:** a scanned matrix (row latch out, column read).
- **Sliders:** 8 sliders plus the piezo dynamics circuit on an ADC0809.
- **Timers:** a Z-80 CTC interrupts every 1.2 ms for housekeeping and also does sequencer timing, with two cascaded counters.
- **Serial:** a Z-80 SIO/2 handles MIDI, SMPTE, sync, cassette and the floppy. There is **no dedicated FDC chip**: the floppy is driven by the SIO plus discrete sync-separator logic, sharing SIO channel B with SMPTE through a 74LS157 selector.

### Cited Findings
- "The CTC… is used primarily for interrupt driven timing functions… an interrupt generated by the CTC every 1.2 milliseconds that reminds the CPU to read the front panel buttons and sliders… The second most often used CTC function is running the sequencer… ZC2 (Zero Count channel 2) is connected to CT1 (Clock Trigger channel 1)… to cascade the two counters to provide very high resolution (.1 BPM) sequencer timing. The CTC is also used to generate a clock for the bit rate on the SMPTE/SYNC input… the CTC provides a transmit clock to the SIO… for outputting SMPTE and click signals." — [Service Manual p.43](https://archive.org/details/emu-sp-1200-service-manual-1987)
- "The SIO… sends and receives all the signals for MIDI, SMPTE, Sync and the disk drive… Channel A is dedicated to MIDI and the floppy disk drive, with conventional MIDI input, output and thru circuitry. Channel B is dedicated to the SMPTE/SYNC interface. The remaining pins on the SIO used for implementing part of the floppy disk drive interface. The floppy disk drive actually uses both halves of the SIO. When the disk drive is in use, the TLC, RXC, ROS, and the RLC signals going to the lower half of the SIO are switched (by IC126) from the SMPTE decoders to the floppy disk circuits… no other SP1200 functions are accessible during disk operations." — [Service Manual p.43](https://archive.org/details/emu-sp-1200-service-manual-1987)
- Fig. 4 "SMPTE/Disk Select" shows the SIO, a 1-of-2 selector on TLC/RXC/ROS/RLC, a SMPTE sync separator (one-shot), and a "Disk Drive Sync Separator" (one-shot) feeding the floppy. Schematic sheet 4 shows IC126 74LS157, IC127 74LS221 one-shot, the −WD write-data output to CN7-22, and a 20 MHz clock divided by IC128 74HCT393 in the write path. — [Service Manual p.44 and schematic sheet 4](https://archive.org/details/emu-sp-1200-service-manual-1987)
- Floppy interface signals on CN7: −INDEX, −TK00 and −WP go into sheet 2 (CTC/SIO); −SELECT, −SIDE, −STEP and −WTGT come from sheet 2 (SIO spare pins); −WD comes from sheet 4; −SELDSK is "SMPTE/Disk Select". — [Lytrix signal table](https://github.com/Lytrix/EMU-SP1200/blob/main/Plan/SP1200_Signal_Definitions.md)
- SMPTE: bi-phase decoder with an LM311 input (≈250 mV–5 V), an edge detector and an IC94 one-shot whose width SIO pin 25 (DTRB) switches between the 25 and 30 fps windows. — [Service Manual p.45](https://archive.org/details/emu-sp-1200-service-manual-1987)
- "The LEDs are mapped into the CPU's I/O space as a write only port… The CPU writes a 0 to the latch to turn the LED on and a 1 to turn it off." "The display is connected directly to the data bus and is mapped as an I/O port. After the display is initialized, the CPU sends ASCII characters." — [Service Manual p.45](https://archive.org/details/emu-sp-1200-service-manual-1987)
- Buttons: the CPU writes the button latch, which drives a decoder that selects a row. Columns are pulled up, and the CPU reads them via a bus driver. — [Service Manual pp.39, 47](https://archive.org/details/emu-sp-1200-service-manual-1987)
- ADC: "single chip… 8 analog inputs… mapped into two of the Z-80As I/O ports. The first port written to select the 1 of 8 sliders and start the conversion. The second port is used to read back the digital value… Slider 8 is multiplexed with the dynamic button circuit on the front panel using a 4053 analog mux." — [Service Manual p.43](https://archive.org/details/emu-sp-1200-service-manual-1987); the part is an ADC0809 per [parts list p.86](https://archive.org/details/emu-sp-1200-service-manual-1987)
- Front panel parts: "LCD 16 x 2 lines" (LP304), 15 red LEDs, 10 large black switches, 1 small red, 28 small white, 8 × 10K linear sliders, a piezo crystal assembly, and an "LCD Volt. Supply". — [Service Manual pp.89–90](https://archive.org/details/emu-sp-1200-service-manual-1987)
- Chip-select decode (schematic sheet 4): IC62 PAL12H6 takes A5–A7, IORQ, RD, WR, C6MHZ, UCK and UCNT0–2 as inputs. IC41 74HCT138 (A2–A4 select) outputs, Q0→Q7: −CSWLVL, −CSWLVH, −CSWPBL, −CSWLD0, −CSWLD1, −CSWAD, −CSWLO, −CSWHI. IC61 74HCT138 (A3–A5 select, enabled by A7/IORQ/M1 logic) outputs: Q0 −CSSM (sound memory), Q1 −CSCTC, Q2 −CSSIO, Q3 −CSPB, and −CSRPB via IC96 OR gate. Other decoded strobes are +SHSTB, +CSWADC, +CSRADC, +CSDISP and +LATCH. — [Service Manual schematic sheet 4 "Sequencer Memory"](https://archive.org/details/emu-sp-1200-service-manual-1987)
- Gotek/HxC floppy emulators are used in SP-1200s with mixed results. Loading works; formatting and writing sometimes fail ("Read/Verify Error"), and a classic HxC SD is recommended over a Gotek. — [HxC forum "SP1200 with Gotek HxC"](https://hxc2001.com/floppy/forum/viewtopic.php?p=16507); [torlus.com thread](https://torlus.com/floppy/forum/viewtopic.php?p=16535)

### Inferences: tentative I/O port map
Derived from the boot EPROM disassembly and checked against the sheet-4 decoders. The port numbers are my own reconstruction; verify them before relying on them.

| Port(s) | Device / function (inferred) | Evidence |
|---|---|---|
| 40h–47h (40h, 41h used) | Sound memory data access (−CSSM); A0 selects byte/nibble half | IC61 Q0; heavy IN/OUT 40h/41h in disk-load loops; Sound Mem PAL has A0.D input (Fig. 7) |
| 48h–4Bh | Z-80 CTC channels 0–3 | IC61 Q1; boot writes 03h (reset), 07h + 2Bh (timer mode + time constant) to 49h/4Ah |
| 50h–53h | Z-80 SIO/2. 50h/51h appear to be channel A data/control (MIDI); 52h/53h channel B data/control (SMPTE/cassette/disk read data) | IC61 Q2; boot OTIRs 18h,04h,CCh,03h,C1h,05h,68h to 51h (channel reset; WR4 = ×64 clock, async; Rx/Tx 8-bit enable). ×64 with the 2 MHz clock to sheet 2 gives 31,250 baud = MIDI. Disk/tape loops poll 53h and read 52h. |
| 58h | Read: button column driver (−CSRPB); bits 5/6 also tested at boot. Write: likely the boot-ROM-disable strobe (−CSPB) | IC61 Q3; `OUT (58h),A` at 0002h immediately before the OS entry at 0004h |
| 80h–83h / 84h–87h | µC sound level register lo / hi (−CSWLVL/−CSWLVH) | IC41 Q0/Q1; boot zeroes 80h–87h |
| 88h | Button row-select latch (−CSWPBL) | IC41 Q2; boot writes 7Ch |
| 8Ch / 90h | LED latches 0 / 1 (write 0 = LED on) | IC41 Q3/Q4; boot writes 00h then FFh around the RAM fill (lamp test) |
| 94h | µC register-file RAM address latch (−CSWAD) | IC41 Q5; written with channel/register index |
| 98h / 9Ch | µC input register lo / hi byte (−CSWLO/−CSWHI) | IC41 Q6/Q7; the RST 08h routine writes L→98h, H→9Ch then reads a sound-memory byte |
| A0h / A1h / A2h | LCD: A0h instruction write, A1h data write, A2h status read (busy = bit 7) | +CSDISP; display on A0/A1 (Fig. 3); init sequence 38h, 0Ch, 01h (HD44780 function-set/display-on/clear); `IN F,(A2h)` busy loop |
| C0h | ADC0809: write = select channel and start, read = result | +CSWADC/+CSRADC; `OUT (C0h)` … delay … `IN (C0h)` |
| 60h–63h | Used by the Rossum 1.04 OS image (not by the boot ROM). Function unknown; possibly the bank-select or request latch | Port-usage count of `sp1200fw.bin` |

- Interrupts: the boot ROM selects IM 2. The CTC and SIO are Z80-family parts with daisy-chain vectoring (SIOIEO "SIO Int. Ena. Out" appears in the signal list), so firmware will use IM 2 vector tables. The CTC's 1.2 ms tick is the main scheduler.
- The floppy is a raw drive driven by SIO channel B in synchronous mode plus a hardware sync separator. Disk format and encoding are therefore E-mu-specific, not IBM PC MFM. This would explain why floppy emulators need special images (HFE files) and why Gotek writes are unreliable. Inference, partly supported by the forum reports.

### Gaps
- I found no source naming the floppy drive model, or the exact disk format and encoding (bits/sector, sync bytes). The disk-read routine in the boot ROM (ports 52h/53h) is the best place to recover it, along with the HFE images in the Lytrix repo.
- The LCD module part number and controller are not named in the manual. HD44780 compatibility is inferred from the command bytes.
- The SIO channel A/B port ordering is inferred, not confirmed from schematic sheet 2.

---

## Q4. Documentation, ROM dumps, disassemblies, emulators and memory maps

### Takeaway
The full SP-1200 service manual (theory of operation, signal names, 18-sheet schematic, parts list, diagnostics) is publicly available as a scan on the Internet Archive. It carries an "Emulator Archive / personal use of SP1200 owners only" watermark, so its copyright status is not clean even though archive.org lists it.

Dumps exist for:
- the SP-1200 boot EPROM (Rev B),
- the original OS on HxC/HFE floppy images,
- the Rossum reissue OS binary ("Main FW Rev 1.04", a Z80 image),
- SP-12 Turbo ROMs and an SP-12 debug EPROM,
- two of the six logic PROMs/PALs.

There is **no MAME driver** for the SP-12 or SP-1200 as of MAME master on 2026-10-20, and I found no public disassembly or documented memory map.

### Cited Findings
- Internet Archive hosts "E-mu Systems SP-1200 Service Manual" (106-page PDF, 51.7 MB). The PDF's sections are Basic Operation, Interface Specification, Functional Test, Diagnostics and Trims, Theory of Operation, Signal Names, Schematics, Wiring Diagrams, Parts List, E.C.O.s. Pages are watermarked "For the personal use of SP1200 Owners only © E-mu Systems… Contact The Emulator Archive". — [archive.org item](https://archive.org/details/emu-sp-1200-service-manual-1987) (duplicate item: [emu-sp-1200-service-manual-1987_202010](https://archive.org/details/emu-sp-1200-service-manual-1987_202010))
- ManualsLib also lists an SP1200 service manual. — [ManualsLib](https://www.manualslib.com/manual/827730/E-Mu-Sp1200.html)
- An SP-12 service manual (by Steve Davies, v1.1) is available as a PDF. — [vintagesynthparts.com](https://www.vintagesynthparts.com/wp-content/uploads/2016/08/EMU_SP12_Service_Manual.pdf)
- Lytrix/EMU-SP1200 contains a KiCad partial redraw (`sp1200.kicad_sch`), BOM spreadsheets, and board photos. Its `BIN files/` folder has:
  - `ip362b-emu-2588.bin` (SP-1200 Rev B boot EPROM, 8 KB)
  - `sp1200fw.bin` (24,912 bytes, string "Main FW Rev 1.04")
  - `SP1200_SP12LOGRI_IC88_82S123.bin` (32-byte log PROM)
  - IC62 PAL12H6 dump (.bin + .csv)
  - `Emu SP12 Turbo EPROM Firmware (v2/SP12A26.Bin` (16 KB) and `SP12B26.Bin` (8 KB), plus an SP12 Rev 2.6 software PDF
  - `Emu SP12 debug EPROM 27c256.Bin` (32 KB; strings "(c) 1987 E-MU SYSTEMS INC.", "Testing Memory", "Testing TurboMem", "Button Test")
  - `sp1200_hxc/` HFE images including `SP1200_OS12.hfe` and `SP1200_OS12_SND.hfe`

  The service-manual PDFs in that repo are Git LFS objects and could not be fetched anonymously. — [Lytrix/EMU-SP1200](https://github.com/Lytrix/EMU-SP1200)
- Lytrix's stated goal is to dump the remaining PROMs/PALs. IC67 (74S288 control sequencer/microcode) is still unavailable as of the README. Lytrix also says "Unlike the SP-12, the SP-1200 loads its OS from a floppy disk into RAM and does not use system ROMs." — [Lytrix README](https://github.com/Lytrix/EMU-SP1200); [IC130 plan](https://github.com/Lytrix/EMU-SP1200/blob/main/Plan/IC130_SRAM_Conversion_Strategy.md)
- In MAME master (commit dated 2026-10-20), `src/mame/emusys/` contains only `emax.cpp`, `emu2.cpp` (Emulator II), `emu3.cpp` and `emu68k.cpp`. There is no sp12/sp1200 or Drumulator file anywhere in the tree. — [mamedev/mame src/mame/emusys](https://github.com/mamedev/mame/tree/master/src/mame/emusys) (checked by blobless clone and `git ls-tree`)
- MAME's Emulator II driver is a "Skeleton driver" built on Z80, Z80 CTC, Z80 DMA, Z80 PIO, Z80 SIO, PIT8253 and ADC0808 device models. Those device models (z80, z80ctc, z80sio, adc0808, hd44780) are the building blocks an SP-1200 driver would need. — [MAME emu2.cpp](https://github.com/mamedev/mame/blob/master/src/mame/emusys/emu2.cpp)
- Rossum Electro-Music's 2021–2025 SP-1200 reissue is described as using "the same Z-80 processor and memory map as the original". Its main firmware v1.04 (May 2024) is installed by copying `sp1200fw.bin` to an SD card. — [Rossum SP-1200 product page](https://www.rossum-electro.com/products/sp-1200); [Rossum documentation](https://www.rossum-electro.com/pages/documentation). These are quoted from search-result summaries; direct page fetches returned HTTP 429.
- A forum thread titled "Hacking the SP's Operating System… Is it possible and who's up for the challenge?" exists on the dxarmy/Tapatalk SP-1200 forum. Its content could not be fetched (HTTP 403). — [Tapatalk p=12441](https://www.tapatalk.com/groups/sp1200/viewtopic.php?p=12441)
- Audio-path modelling resources: D. Yeh et al., "Physical and Behavioral Circuit Modeling of the SP-12 Sampler" (ICMC 2007) — [CCRMA slides](https://ccrma.stanford.edu/~dtyeh/sp12/yeh2007icmcsp12slides.pdf); and the Python "pitcher" emulation of the SP-12/SP-1200 signal chain — [mwcm/pitcher](https://github.com/mwcm/pitcher)

### Inferences
- Because the OS is RAM-resident and loaded from disk, a custom OS can be booted on unmodified original hardware by writing a bootable disk image. No EPROM swap is needed, provided the image matches the format the boot ROM loader expects.
- The Rossum `sp1200fw.bin` begins `00 00 00 00 C3 79 00` and shares the boot ROM's RST 08h sound-memory copy routine and port set (40h/41h, 48h–4Bh, 50h–53h, 58h, 80h–9Ch, A0h/A1h, C0h). That makes it a good, compact reference for OS entry conventions. It is Rossum's copyrighted code; use it for interoperability study only.
- The `SP1200_OS12*.hfe` images presumably carry E-mu OS v1.2. Decoding the HFE flux back to the boot loader's on-disk format would give the original E-mu OS binary for disassembly.

### Gaps
- I found no public SP-1200 disassembly or documented RAM memory map.
- I found no emulator (MAME or other) that runs the original firmware. "SP-1200 emulations" in plugins are audio models only.
- The SP-1200 OS version history (1.1–1.5?) and whether every version is disk-loaded were not researched in depth here (see `os_storage_and_boot.md`).

---

## Q5. Relationship to the SP-12, Drumulator and Emulator II

### Takeaway
E-mu's own manual says the SP-1200 is "very much like the Drumulator and the SP-12". All three share the same architecture: a Z-80 host plus a custom, non-programmable microcoded sound "microcontroller" that time-multiplexes voices through one DAC into sample-and-holds. The SP-1200's memory organisation (byte + nibble per 12-bit word) is a carry-over from the SP-12. The main differences are 12-bit format, disk drive and user sampling (vs. the Drumulator), RAM-loaded OS (vs. the SP-12's ROM OS), and 26.04 kHz with 10 s of DRAM (vs. the SP-12's 27.5 kHz and 192 kB).

### Cited Findings
- "The SP1200 is very much like the Drumulator and the SP-12 in that it uses multiplexed audio controlled by a custom designed micro-controller and uses a Z-80 CPU. There are some major differences like the 12 bit format, disk drive and user sampling. Knowledge of the Drumulator and or SP-12 will be helpful." — [Service Manual p.39](https://archive.org/details/emu-sp-1200-service-manual-1987)
- "To make the SP1200s design compatible with the SP-12, the memory was set up in the same way… On the SP-12, the memory was Static RAM… Keep in mind that this is a carry over from the SP-12." — [Service Manual p.47](https://archive.org/details/emu-sp-1200-service-manual-1987)
- The pitch-shift adder scheme "as implemented on the Drumulator": with no pitch shifting, the carry into the upper adders is always 1. — [Service Manual p.51](https://archive.org/details/emu-sp-1200-service-manual-1987)
- SP-12 specs: 27.5 kHz sampling, 192 kB wavetable RAM (5 s total, 2.5 s per sound), 24 ROM sounds plus 8 user sounds, 32 tuning steps, 8 outputs with the VCF on outputs 7–8 (SP-12 numbering). — [Yeh et al. ICMC 2007 slides](https://ccrma.stanford.edu/~dtyeh/sp12/yeh2007icmcsp12slides.pdf)
- SP-12 parts that are absent or unnecessary in the SP-1200, per Lytrix: IC69 PAL12L6 "Sound Memory Intfc (Only in SP-12)" and IC148 PAL16R8 "ROM Memory (Only in SP-12)". Note, however, that the SP-1200 manual itself refers to "the PAL (IC69)" in the sampling path, so this labelling is inconsistent. — [Lytrix README](https://github.com/Lytrix/EMU-SP1200); [Service Manual pp.48–51](https://archive.org/details/emu-sp-1200-service-manual-1987)
- The SP-12 Turbo firmware is in EPROM (SP12A26 16 KB + SP12B26 8 KB, "(c) 1985 E-MU SYSTEMS INC.", strings for cassette/disk load). — [Lytrix BIN files](https://github.com/Lytrix/EMU-SP1200)
- E-mu used Z80s in many designs up to the SP-1200. — [Wikipedia: E-mu SP-1200](https://en.wikipedia.org/wiki/E-mu_SP-1200)
- The Emulator II is also Z80-based with Z80 CTC/SIO/PIO/DMA, an 8253 and an ADC0808 (per its MAME skeleton). — [MAME emu2.cpp](https://github.com/mamedev/mame/blob/master/src/mame/emusys/emu2.cpp)

### Inferences
- SP-12 documentation (the Steve Davies SP-12 service manual) and the SP-12 ROMs are useful cross-references for the µC register protocol and the CTC/SIO usage. The SP-12 ROM firmware may share large parts of its code with the SP-1200 OS, since the SP-12 Turbo ROMs and the Rossum SP-1200 image share user-interface strings such as "Song Full", "1 - Accelerando", "Not Enuf Memory"/"NotEnough Memory". Code-level similarity is unverified.
- The Emulator II shares the Z80 + CTC + SIO + ADC08xx peripheral family but has a different (DMA-based) sound engine. It is a sibling in parts choice rather than in voice architecture. This is inference from the MAME device list; I did not compare schematics.

### Gaps
- I did not obtain Drumulator schematics to compare the µC directly.
- I did not verify whether SP-12 and SP-1200 I/O port assignments are identical.
