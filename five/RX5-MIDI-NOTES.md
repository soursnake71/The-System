# Yamaha RX5 Digital Rhythm Programmer: MIDI, SysEx and data-format reference notes

Compiled 2026-11-26 from public sources. Every fact has a source tag. Anything inferred or
unconfirmed is marked **UNCERTAIN**.

## Sources

| Tag | Source |
|---|---|
| [OM] | Yamaha RX5 Owner's Manual (English/French/German, 81-page scan). Internet Archive: https://archive.org/details/synthmanual-yamaha-rx-5-owners-manual. I read the PDF (`yamaharx-5ownersmanual_text.pdf`) and the OCR text (`https://archive.org/download/synthmanual-yamaha-rx-5-owners-manual/yamaharx-5ownersmanual_djvu.txt`). The same scan is at https://archive.org/details/manuallib-id-2654208 and https://www.manualslib.com/manual/341472/Yamaha-Rx-5.html. Page numbers below are the printed page numbers; "PDF p." is the page index in the archive.org PDF. |
| [OM-CHART] | The MIDI Implementation Chart in [OM], PDF p.76. Header: "Model RX5 MIDI Implementation Chart, Date: 11/6, 1986, Version: 1.0". I read it from a 250 dpi render of the page. |
| [RX5USB] | gligli, *RX5USB*, a DIY programmable waveform cartridge and bank editor (GPL), https://github.com/gligli/rx5usb. The main source is `gui_tool/rx5classes.pas`. The repo also includes dumps of the factory cartridge banks (`banks/rx5romA.rx5bank`, `banks/rx5romB.rx5bank`), which I decoded myself (see section 6). |
| [EM] | gligli's RX5USB announcement thread, https://electro-music.org/forum/topic-56137.html |
| [VSE] | "Yamaha RX-5 ROM format parser/extractor", Vintage Synth Explorer forum, https://forum.vintagesynth.com/viewtopic.php?t=100906 |
| [POLY] | polynominal.com RX5 page, https://polynominal.com/site/studio/gear/drum/yamaha_rx5/yamaha_rx5.html |
| [WIKI] | https://en.wikipedia.org/wiki/Yamaha_RX-5 |
| [MUZ1] | David Mellor, RX5 review, https://www.muzines.co.uk/articles/yamaha-rx5/1956 |
| [MUZ2] | Music Technology, Apr 1987, RX5 review, https://www.muzines.co.uk/articles/yamaha-rx5/1993 |
| [MAME] | MAME source tree, `src/mame/yamaha/`, https://github.com/mamedev/mame. It has only `ymrx15.cpp`, a skeleton RX15 driver. There is no RX5 driver. |

---

## 0. Bottom line on SysEx

**The byte-level format of the RX5's MIDI bulk dump is not publicly documented. I found no
Yamaha data list, service manual, librarian source or reverse-engineering write-up that gives it.**

- The owner's manual has no "MIDI Data Format" section. Its only statements on the subject are:
  - The MIDI Implementation Chart marks System Exclusive as transmitted "o" and recognized "o", with the remark "voice & sequence" [OM-CHART].
  - MIDI Jobs #08–#10 (Device Number, Receive Bulk, Transmit Bulk) are described in operating terms only [OM pp.65–66].
- Searches of manualslib, archive.org, the usa.yamaha.com manual library listings, Gearspace, Vintage Synth Explorer, muzines and GitHub found no RX5 SysEx header, byte count or checksum spec.
- Gearspace pages returned HTTP 403 to the fetch tool, so I could only see their search snippets. One snippet: a user says the RX5 "does not do sysex" and another points out that the chart says it does.
- [POLY] says "there is NO official RX5 editor". It mentions that the YRM-302 RX Editor for MSX (written for the RX11/15/21) "partially works on the RX5".
- I found no parameter-change SysEx for the RX5. A Gearspace snippet quotes an **RX7** parameter-change message (`F0 43 10 03 ...`), but that is a different machine. Do not assume it applies to the RX5.

**To get the real format:** capture a dump with MIDI Job #10 into a SysEx recorder (MIDI-OX, SysEx Librarian, or any raw MIDI logger). Also capture the request message the RX5 sends when you answer YES in Job #09. Then compare dumps byte by byte, for example before and after editing one voice parameter.

### 0.1 What can be said about the bulk dump (from [OM])

| Item | Fact | Source |
|---|---|---|
| Messages used | System Exclusive, both directions; content "voice & sequence" | [OM-CHART] |
| Device number | MIDI Job #08 "DEVICE NUMBER": OFF or 01–16. It "MUST be turned on prior to receiving or transmitting bulk data" and "MUST be set to match that of the external MIDI device". The manual calls it "the System Exclusive Data channel". | [OM p.65] |
| Dump request | MIDI Job #09 RECEIVE BULK: "Press +1/YES to send a Bulk Dump Request to the external device". So the RX5 can **send** a dump request. Job #10 also says the RX5 transmits when it receives a Bulk Dump request from another device (another RX5 using Job #09). So it **recognizes** a request too. The bytes are not documented. | [OM pp.65–66] |
| Contents | "Bulk data includes all Sequence and Voice data" (patterns, songs, chains, and voice edit data). Receiving it erases all data in the RX5. Memory Protect must be OFF to receive. | [OM pp.65–66] |
| Granularity | Only one all-data dump. The manual offers no single-pattern or single-voice MIDI dump; single-pattern save exists only for cartridge and cassette. | [OM pp.46–55, 65–66] |
| Duration | "the factory-preset Pattern and Song data in your RX5 takes about 10 seconds to transmit." At 3125 bytes/s, 10 s is about 31 KB. **UNCERTAIN:** the real size depends on how full memory is. | [OM p.66] |
| Checksum | There is one. The error message "checksum error!" appears when "During a Receive Bulk operation (MIDI JOB #09), there was a break in transmission". The algorithm is not documented. | [OM p.68] |
| Compatible devices | "another RX5 ... or a Yamaha QX1 or QX5 Digital Sequence Recorder" | [OM pp.65–66] |
| MIDI buffer | "MIDI BUFFER FULL" error. The manual says each byte is held "of the order of .0003 seconds for one Byte". | [OM p.68] |

**UNCERTAIN (educated guess only, not verified):** 1986 Yamaha bulk dumps normally start
`F0 43 0n ...` (n = device number − 1), and requests normally start `F0 43 2n ...`.
Yamaha checksums of that era are usually the two's complement of the 7-bit sum of the data bytes.
None of this is confirmed for the RX5.

---

## 1. MIDI implementation (channel messages)

Transcribed from [OM-CHART], PDF p.76:

| Function | Transmitted | Recognized | Remarks |
|---|---|---|---|
| Basic channel, default | 1–16 | 1–16 | memorized |
| Basic channel, changed | 1–16 | 1–16 | |
| Mode, default | 3 | 3 | |
| Mode messages | x | x | |
| Mode altered | ********** | x | |
| Note number | 0–127 | 0–127 / 36–96 | *1 |
| Note number, true voice | ********** | x | |
| Velocity, Note ON | o 9nH, v=1–127 | o v=1–127 | |
| Velocity, Note OFF | x 8nH, v=64 | x | |
| After touch, key's / ch's | x / x | x / x | |
| Pitch bender | x | x | |
| Control change | x | x | |
| Program change | x | o | "Note# asgn: pitch" |
| System exclusive | o | o | "voice & sequence" |
| System common: Song position | x | o | |
| System common: Song select | o 0–19 | o 0–19 | |
| System common: Tune | x | x | |
| System real time: Clock | o | o | |
| System real time: Commands | o | o | |
| Aux: Local ON/OFF | x | x | |
| Aux: All Notes OFF | x | x | |
| Aux: Active Sense | o | o | |
| Aux: Reset | x | x | |

*1 "When Note number assign switch is set to VOICE, each voice sounds by each note (Note # range: 0–127). When Note number assign switch is set to PITCH, each voice sounds by each channel (Note # range: 36–96)."

Notes:
- The chart marks Note OFF transmit as "x", but the column also shows "8nH, v=64". MIDI Job #07 says "NOTE OFF signals as well as NOTE ON signals are sent" with a Gate Time of 0.01–1 s in 0.01 s steps. In Damp mode the RX5 also uses received Note OFF. So in practice the RX5 does send 8nH v=64 Note Offs [OM pp.6, 64–65]. **UNCERTAIN** why the chart says "x".
- Program change is recognized and tied to "Note# asgn: pitch" (Tunable Note mode). The manual does not explain the mapping. **UNCERTAIN:** it probably selects the voice on that channel.
- Song Select 0–19 maps to the 20 songs [OM-CHART].

### 1.1 MIDI Mode jobs [OM pp.61–66]

| Job | Function | Range / notes |
|---|---|---|
| #01 Channel Message | Turns note transmit/receive ON or OFF | ON/OFF |
| #02 Receive Channel | Note Number mode: one channel, 01–16. Tunable Note mode: each of 16 channels switched on or off independently. | |
| #03 Transmit Channel | Per-voice MIDI transmit channel | 1–16, per voice (all 64) |
| #04 Note Number Assign | "VOICE": each voice gets a note number, C-2 to G8 (0–127). More than one voice can share a note. "PITCH" (Tunable Note): one voice per receive channel, playable chromatically over "five-octave range (C1 – C6)" (notes 36–96). | |
| #05 Initialize Note | Restores the default note map for all 64 voices (table in §5) | |
| #06 E.G. Velocity | How much velocity affects the EG. Higher velocity gives a faster attack and slower D1R/D2R. | OFF, 01–04 |
| #07 Gate Time | Transmitted note length | 0.01–1 s in 0.01 s steps (factory default not legible in the OCR) |
| #08 Device Number | SysEx device number | OFF, 01–16 |
| #09 Receive Bulk | Waits for a bulk dump, or sends a Bulk Dump Request | |
| #10 Transmit Bulk | Sends the bulk dump | |

Received velocity v=1–127 scales level, and with Job #06 also the EG [OM-CHART, OM p.65].

---

## 2. Parameter change messages

**None documented.** The chart shows no control change, no pitch bend and no aftertouch.
No SysEx parameter-change message is documented anywhere I could find [OM-CHART].

---

## 3. Sequencer: resolution, capacities, limits

| Item | Value | Source |
|---|---|---|
| Internal resolution | "Clocks shown on the LCD are actually 96th notes. These are the smallest note-increments that can be recorded". Diagram: "BAR TOTAL: 96 CLOCKS" for a 4/4 bar. That is **96 clocks per whole note = 24 clocks per quarter note**, the same as MIDI clock. Quantize OFF records "in increments of 1/96th notes". | [OM pp.13, 17] |
| Quantize values | 1/2, 1/4, 1/6, 1/8, 1/12, 1/16, 1/24, 1/32, 1/48, OFF (1/96) | [OM p.13] |
| Conflicting claim | [POLY] says "48 PPQ". This contradicts the manual's 96 clocks per 4/4 bar. Treat it as **UNCERTAIN / probably wrong**. | [POLY] |
| Patterns | 100 (00–99). Factory patterns occupy 00–54 and are restored by holding ACCENT 1 at power-on. | [OM p.9] |
| Pattern length | 01–99 bars ("MAX. LENGTH: 99 MEASURES"). Append fails if the combined pattern is longer than 100 bars. | [OM pp.10, 70, 69] |
| Time signature | Numerator 01–99; denominator 1/2, 1/4, 1/6, 1/8, 1/12, 1/16, 1/24, 1/32 (not 1/48) | [OM p.10] |
| Per-note data (Pattern Edit) | Pitch (offset ±60 semitones in 10-cent units, actual range +24/−36 semitones), Level (±31, actual 0–31), Attack (±63, actual 01–99), Decay (±63, actual 01–99), Reverse ON/OFF. Also voice and accent. | [OM pp.17–19] |
| Songs | 20 (00–19), each up to 999 Parts. A Part is a pattern, repeat, tempo change, volume change or search mark. Factory songs: 00–02. | [OM p.20, p.70] |
| Chains | 3 (0–2). The overview says "each containing up to 90 Steps", but the spec page says "3 CHAINS (MAX. LENGTH: 999 STEPS)". Conflict, **UNCERTAIN**: 90 is the more specific figure in the body text. | [OM p.30, p.70] |
| Key data sets | 3 stored drum sets (Key Assign jobs #01–#04 data) | [OM p.45, p.70] |
| Tempo | 40–250 quarter notes per minute | [OM p.14] |
| Swing | Only at quantize 1/8 or 1/16. Five settings from 54% to 71%; 67% gives a triplet shuffle. | [OM p.14] |
| Polyphony | Up to 12 sounds at once, one per output channel (key pair). Two keys of the same pair cut each other. | [OM p.6, p.11] |
| Memory size in notes | **Not stated.** The only figure is "remaining memory" as a percentage (Utility Job #01 for patterns, #02 for songs). The error "memory full !!" covers pattern, song and chain memory, and "too large" covers a single pattern holding "the maximum amount of data". There is no note count anywhere. | [OM pp.58, 68–69] |

---

## 4. Voices and parameters

### 4.1 Voice memory

| Memory | Count | Notes | Source |
|---|---|---|---|
| Internal (ROM) | 24 voices | "12 BIT" | [OM p.70] |
| RAM ("Copied", Cp01–Cp12) | 12 | Copies of internal or cartridge voices, made with Key Assign Job #09 | [OM pp.8, 45, 70] |
| Waveform Data Cartridge | 28 (on the supplied cartridge) | | [OM p.7, p.70] |
| Total addressable | 64 voices | "voice data for all 64 voices" | [OM p.47] |

### 4.2 Edit Voice parameters ("primary data", per voice) [OM pp.34–38]

| Job | Parameter | Range |
|---|---|---|
| #02 | Pitch | +24 / −36 semitones, shown as +2400/−3600 cents. Steps of 1 semitone, or 10 cents while holding "0". |
| #03-1 | Attack Rate (AR) | 01–99 |
| #03-2 | Decay 1 Rate (D1R) | 01–99 |
| #03-3 | Decay 1 Level (D1L) | 01–60 (OCR reads "01—60"; **UNCERTAIN** whether the top is 60 or 63) |
| #03-4 | Decay 2 Rate (D2R) | 01–99 |
| #03-5 | Release Rate (RR) | 01–99 |
| #03-6 | Gate Time (GT) | 0.1–6.5 s, shown as 0100–6500 ms. It sets total envelope length before release; maximum 6.5 s. |
| #04-1 | Bend Rate | 0–60 (0 = no bend) |
| #04-2 | Bend Range | ±60 semitones (negative bends down). It cannot take the pitch outside +24/−36. The manual says bend moves in fixed "1/100th cent" steps (sic) with Rate setting the step time. |
| #05 | Voice Level | 0–31 (0 = off) |
| #06 | Loop | ON/OFF. The loop extends long envelopes by repeating the tail. OFF gives the raw sample cut-off. |
| — | Reverse | Panel key or per-note in Pattern Edit. It plays the voice with its original (unedited) EG, and does not reverse the bend. [OM p.6] |

### 4.3 Key Assign parameters ("additional data", per instrument key) [OM pp.40–45]

| Job | Parameter | Range |
|---|---|---|
| #01 | Voice assign (Int / Cp / Crt voices available at that key pair) | |
| #02-1 | Key pitch offset | ±60 semitones (10-cent units), clamped to +24/−36 |
| #02-2 | Key level offset | ±31, clamped to 0–31 |
| #02-3 | Key attack offset | ±63, clamped to 01–99 |
| #02-4 | Key decay offset (applies to D1R and D2R equally, not D1L) | ±63, clamped to 01–99 |
| #03/#04 | Multi Voice / Multi Step: one voice spread over keys A–L with stepped pitch, level, attack or decay | |
| #05 | Accent 1 and Accent 2 levels, per voice | ±31, added to voice level and clamped to 0–31 |
| #06 | Output channel assign (Copied and Cartridge voices only) | 1–12 |
| #07/#08 | Save / load key data | 3 sets |

Note: the manual says the accent range is "the same as the voice level range (refer to EDIT VOICE JOB #04)". That is a typo for Job #05.

### 4.4 Output channels, key pairs and stereo positions [OM p.5]

The 24 keys come in 12 pairs: A+M, B+N, ... L+X. Each pair is one of the 12 output channels.
Stereo position scale: 15 = LEFT, 8 = CENTER, 1 = RIGHT.

| Output ch | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Keys | A/M | B/N | C/O | D/P | E/Q | F/R | G/S | H/T | I/U | J/V | K/W | L/X |
| Stereo pos. | 8 | 9 | 9 | 12 | 10 | 6 | 4 | 13 | 3 | 11 | 5 | 8 |

### 4.5 Factory internal voices (24) and their key pairs [OM pp.5, 8]

| Out ch | Keys A–L voice | Keys M–X voice |
|---|---|---|
| 1 | BD 1 (Bass Drum 1) | BD 2 (Bass Drum 2) |
| 2 | SD 1 (Snare Drum 1) | SD 2 (Snare Drum 2) |
| 3 | RIM 1 (Rimshot 1) | RIM 2 (Rimshot 2) |
| 4 | Tom 1 | E.Tom 1 (Electric Tom 1) |
| 5 | Tom 2 | E.Tom 2 |
| 6 | Tom 3 | E.Tom 3 |
| 7 | Tom 4 | E.Tom 4 |
| 8 | HH clos (Hi-Hat closed) | HH open |
| 9 | Cup (Ride cup) | Edge (Ride edge) |
| 10 | Crash | China (Chinese cymbal) |
| 11 | Claps | Tambrn (Tambourine) |
| 12 | Cowbel (Cowbell) | Shaker |

**UNCERTAIN, row assignment:** the drawing on p.5 prints the "BASS DRUM 2 ... SHAKER" labels
above the A–L row and "BASS DRUM 1 ... COWBELL" below the M–X row. That reading puts BD 2 on key A.
The table above follows the LCD examples instead: they show "keyA :Int-BD 1" throughout the manual,
and the factory RAM copies are Cp1 = BD 1 ... Cp8 = HH clos, Cp9 = Cup, Cp10 = Crash, Cp11 = Claps,
Cp12 = Cowbe [OM pp.8, 35, 40]. The voice pairing per output channel is certain either way.

### 4.6 Supplied Waveform Data Cartridge (WRC-01 "RX5 ROM") voices (28) [OM p.7]

| # | Name | Description | # | Name | Description |
|---|---|---|---|---|---|
| 1 | SD3 | Snare Drum | 15 | Timpn | Timpani |
| 2 | BD3 | Bass Drum | 16 | GlsCsh | Glass Crash effect |
| 3 | CgaHMT | Conga (High Muted) | 17 | Gun | Gunshot effect |
| 4 | CgaHOP | Conga (High Open) | 18 | FMprc1 | Synth-percussion (high) |
| 5 | CgaLO | Conga (Low) | 19 | FMprc2 | Synth-percussion (medium) |
| 6 | BgoHI | Bongo (High) | 20 | FMprc3 | Synth-percussion (low) |
| 7 | BgoLO | Bongo (Low) | 21 | EbassH | Electric Bass Gtr (high) |
| 8 | TimblH | Timbale (High) | 22 | EbassL | Electric Bass Gtr (low) |
| 9 | TimblL | Timbale (Low) | 23 | DXorch | Synth-orchestra |
| 10 | AgoHI | Agogo (High) | 24 | DXmrmb | Synth-marimba |
| 11 | AgoLO | Agogo (Low) | 25 | DXclav | Synth-clavinet |
| 12 | Cuica | Cuica | 26 | Hey | Vocal sound |
| 13 | Cstnt | Castanet | 27 | Wao | Vocal sound |
| 14 | Whstl | Whistle | 28 | Ooo | Vocal sound |

Other ROM cartridges: WRC-02 Jazz & Fusion (24 PCM), WRC-03 Heavy Rock (15), WRC-04 Effect (19) [POLY, WIKI].
The cartridges are also usable on the PTX8 [POLY].

---

## 5. Default MIDI note numbers (Job #05 Initialize Note) [OM p.65, PDF p.68]

Yamaha octave naming, where C3 = 60. An asterisk marks voices that match RX11 note assignments
(16 voices, so an RX5 and an RX11 can drive each other).

| Voice | Note | | Voice | Note |
|---|---|---|---|---|
| *BD 2 | 44 (G#1) | | *BD 1 | 45 (A1) |
| *SD 2 | 49 (C#2) | | *SD 1 | 52 (E2) |
| RIM 2 | 46 (A#1) | | *RIM 1 | 51 (D#2) |
| E.TOM 1 | 43 (G1) | | *TOM 1 | 53 (F2) |
| E.TOM 2 | 42 (F#1) | | *TOM 2 | 50 (D2) |
| E.TOM 3 | 41 (F1) | | *TOM 3 | 48 (C2) |
| E.TOM 4 | 40 (E1) | | *TOM 4 | 47 (B1) |
| *HH OPEN | 59 (B2) | | *HH CLOSED | 57 (A2) |
| RIDE (EDGE) | 63 (D#3) | | *RIDE (CUP) | 62 (D3) |
| CHINESE | 61 (C#3) | | *CRASH | 60 (printed "D3", a misprint for C3) |
| TAMBOURINE | 58 (A#2) | | *CLAPS | 54 (F#2) |
| *SHAKER | 56 (G#2) | | *COWBELL | 55 (G2) |

- Copied voices Cp01–Cp12 default to notes 24–35.
- Cartridge voices 1–28 default to notes 65–92, in the §4.6 order. So SD3 = 65 ... Ooo = 92. (The ordering inside the range is inferred from the list order. **UNCERTAIN**.)
- Notes are assigned per **voice**, not per key. Moving a voice to another key keeps its note.

---

## 6. Sample format and Waveform Data Cartridge format

The owner's manual says only "INTERNAL: 12 BIT, 24 VOICES; RAM: 12 BIT, 12 VOICES; WAVEFORM DATA
CARTRIDGE: 28 VOICES" [OM p.70]. [MUZ2] calls the internal voices "all 12-bit samples", compared
with the 8-bit RX11 and RX15. [WIKI] quotes Yamaha Japan: "normalized 12-bit".
The cartridge layout below comes from reverse engineering ([RX5USB], [EM], [VSE]). Yamaha did not
document it.

### 6.1 Audio

| Item | Value | Source |
|---|---|---|
| Sample rate | 25 kHz nominal ("sample rate is 25Khz"). The tool uses `CRX5BaseSampleRate=25000` at pitch 0. | [EM], [RX5USB], [POLY] |
| Bit depth | Per voice, either **8-bit or 12-bit signed PCM** | [EM], [RX5USB], [VSE] |
| Companding | **UNCERTAIN.** [RX5USB] treats both formats as plain linear two's-complement (8-bit: `s16 = s8*256`). The cartridges it builds play correctly, which suggests linear. [VSE] guessed "compressed", without evidence. [POLY] says the DAC is a "16-bit Yamaha YM3014, described as 'floating point'". The YM3014 is a floating-point (mantissa/exponent) serial DAC, so the 12-bit data may be scaled ("normalized") at playback. | |
| 12-bit packing | 2 samples in 3 bytes. [RX5USB]: `s1 = (B2<<8) \| ((B1 & 0x0F)<<4)`, `s2 = (B3<<8) \| ((B1>>4)<<4)`, giving 16-bit left-justified values. So B2 and B3 are the high 8 bits of samples 1 and 2, and B1's low and high nibbles are their low 4 bits. RX5USB also writes one leading 0x00 byte ("12bit mode has a 1 byte latency"). [VSE] gives a different nibble order (B1H B1L B2H / B2L B3H B3L). **UNCERTAIN**, but [RX5USB] is the one tested on hardware. | [RX5USB], [VSE] |
| Hardware | The supplied cartridge holds two 1 Mbit mask ROMs, each an independent 128 KB sound bank. RX5USB replaces them with one 4 Mbit SST39SF040 flash (4 banks, with a "side" switch choosing a pair). [VSE] names the ROMs M5M23C100-512P and -513P. | [EM], [VSE] |
| Internal sample ROM | [POLY]: 24 PCM voices in two ROMs (ICs 45/46), "about 1 megabyte". CPU: Hitachi HD63B09. MIDI: YM3802. **UNCERTAIN** (hobbyist page). | [POLY] |

### 6.2 Bank layout (one 128 KB = 131072-byte bank) [RX5USB `rx5classes.pas`]

| Offset | Size | Content |
|---|---|---|
| 0x0000 | 4 | `00 00 00 00` |
| 0x0004 | 1 | Bank ID. Factory bank A = 0x10, bank B = 0x11. RX5USB generates one as `(cs & 0xFF) xor (cs >> 8)` of the header checksum. The RX5 uses it to detect a "Different Crt !!" at power-on (**UNCERTAIN** inference). |
| 0x0005 | 1 | Number of voices in this bank (max 30 per RX5USB; factory: 16 + 12 = 28) |
| 0x0006 | 32 × n | Voice entries (below) |
| … | | zero padding |
| 0x03FE | 2 | Checksum, **big-endian**: the 16-bit sum (mod 65536) of bytes 0x0000–0x03FD. I verified this on both factory banks: bank A stores 0x7792 and bank B stores 0x59F7, both equal to the computed sums. [VSE] also says the whole ROM is checked (16-bit byte sum, big-endian, in the last two bytes). |
| 0x0400– | | PCM data. Each voice starts on a 256-byte boundary. |

The "checksum error!" message at power-on is triggered by "a faulty Waveform Data Cartridge" [OM p.68].
That is consistent with this checksum.

### 6.3 32-byte voice entry [RX5USB]; values verified against the factory banks

| Byte | Field | Notes |
|---|---|---|
| 0 | Pitch "octave" | Pitch = `byte0*120 + byte1 − 360`, in 1/20-tone (10-cent) units. Factory voices are all 0x02, 0x78, so pitch = 0, which plays at 25 kHz. Sample rate = 25000 × 2^(pitch/120). The span 0..600 units (−36..+24 semitones) matches the manual's pitch range. (Mapping inferred. **UNCERTAIN**.) |
| 1 | Pitch "note" (0–120) | see above |
| 2 | Format | 0x00 = 8-bit, 0x01 = 12-bit |
| 3 | Flags and address bit 16 | bit 6 (0x40) = loop **disabled** (factory Hey, Wao and Ooo have it set). bit 0 = bit 16 of the start address. |
| 4 | Start address bits 15–8 | Start is 256-byte aligned; low byte implied 0 |
| 5–7 | Loop start, 17-bit absolute address (byte5 bit0, byte6, byte7) | |
| 8–10 | Loop end, 17-bit absolute | Equal to the sample end in all factory voices |
| 11–13 | Sample end, 17-bit absolute | length = end − start (in bytes) |
| 14 | Attack Rate | factory 99 (0x63); vocals 45/70 |
| 15 | Decay 1 Rate (per RX5USB) | e.g. 24, 39, 55 … |
| 16 | "Release Rate" (per RX5USB) | 60 (0x3C) in every factory voice. **UNCERTAIN**: this could be D1L, whose manual maximum is 60. |
| 17 | Decay 2 Rate (per RX5USB) | |
| 18 | "Decay 1 Level" (per RX5USB) | e.g. 59, 51, 1. **UNCERTAIN**, see byte 16. |
| 19 | Gate Time code | 0x5C (92) in every factory voice. RX5USB comment: "index in an array". The coding is unknown (manual range 0.1–6.5 s). |
| 20 | Bend Rate ("supposition") | 0 in factory voices |
| 21 | Bend Range ("supposition") | 0 in factory voices |
| 22 | Unknown | 13–39 in factory voices, 99 for the vocals |
| 23 | Voice Level | 0–31 (factory 22–27) |
| 24 | "Channel" | 0–11. **UNCERTAIN**: does not consistently match the manual's default output pairs. |
| 25 | 0x00 | |
| 26–31 | Name, 6 ASCII characters, space padded | e.g. "SD 3  ", "CgaHMT" |

Factory bank A (ID 0x10) holds, in order: SD 3, BD 3, CgaHMT, CgaHOP, CgaLO, BgoHI, BgoLO, TimblH, TimblL,
AgoHI, AgoLO, Cuica, Cstnt, Whstl, Timpn, GlsCsh. That is 16 voices; 12-bit ones are BD 3, CgaHOP, CgaLO,
BgoLO, TimblL, Cuica and Timpn.
Bank B (ID 0x11) holds: Gun, FMprc1, FMprc2, FMprc3, EbassH, EbassL, DXorch, DXmrmb, DXclav, Hey, Wao, Ooo.
That is 12 voices; 12-bit ones are FMprc3, EbassH, EbassL and DXmrmb.
These are my own decodes of `banks/rx5romA.rx5bank` and `banks/rx5romB.rx5bank` in [RX5USB].

The RX5 does not sample. RAM voices are edited copies of ROM voices (parameters only), not new sample data [OM p.45].

### 6.4 RAM4 data cartridge (sequence and voice-parameter storage)

- The rear-panel "DATA CARTRIDGE" slot takes a Yamaha RAM4. It must be formatted on the RX5 (Cartridge Job #30). The error "Format conflict!" means the cartridge was formatted by another device [OM pp.46–51, 69].
- Jobs: Save #01 (all sequence and voice), #02 (sequence), #03 (single pattern, to any destination 00–99 on the cartridge), #04 (voice). Load #11–#14, Verify #21–#24.
- It holds "the sequence data for up to 100 Patterns, 20 Songs and 3 Chains and/or the voice data for all 64 voices" [OM p.47].
- [POLY] says memory is "doubled" with a RAM4 fitted. That probably means a second full set of storage. **UNCERTAIN.**
- The RAM4 data layout is **not documented** anywhere I found.

---

## 7. Cassette interface

| Item | Fact | Source |
|---|---|---|
| Jobs | Save #01 seq+voice, #02 sequence, #03 single pattern, #04 voice, #05 set-ups. Load #11–#15. Verify jobs are numbered from #21; the scan's job table and contents page are only partly legible, so the exact verify numbers are **UNCERTAIN**. | [OM pp.52–55] |
| Set-up data | The 3 key-data sets, accent levels of all 64 voices, MIDI transmit channels of all 64 voices, MIDI receive channel, MIDI notes of the voices | [OM p.53] |
| Levels | Output "rated to match with a microphone input". Input "matches a normal data recorder output". No noise reduction. | [OM p.52] |
| Errors | "Sum error" = cassette checksum error. "type error" = wrong job for the data type. "data error" = verify mismatch. | [OM p.69] |
| The same jack carries Tape Sync | Tape Sync record and play through the CASSETTE terminal. The sync tone carries tempo and stop, and can drive "any MIDI Clock-type device". | [OM pp.56–57] |
| Modulation, baud rate, block format | **Not documented.** I found no public analysis. | — |

---

## 8. Sync

Sync sources: Internal, MIDI clock, Tape, and External (gate-type clock or trigger via EXT CLOCK IN/OUT) [OM p.56].
The manual gives no pulses-per-quarter figure for EXT CLOCK. **UNCERTAIN.**
A footswitch can drive start/stop [OM p.3].

---

## 9. Gaps and next steps

1. **Bulk dump bytes (header, size, packing, checksum, request message)** are not public. Capture them from hardware: Job #10 for the dump, Job #09 for the request.
2. Gate-time code, bytes 15–18 and 22 of the cartridge voice entry, and the companding question need checking against hardware.
3. The RAM4 and cassette data formats are undocumented.
4. No MAME RX5 driver exists, so there is no emulator source to mine. MAME has only a skeleton RX15 driver (`ymrx15.cpp`) [MAME].
