# Dragonstomper: reverse engineering notes

Dragonstomper (Starpath, 1982, Steve Landrum - the ROM text says "BY SHL") came on Supercharger
cassette in three loads. This folder rebuilds it in 3D from data pulled out of those loads. The ROM
itself is not included; `tools/extract.py` regenerates `data.js` from your own copy.

## Tools

| File | What it does |
| --- | --- |
| `tools/extract.py` | Reads the loads (zip, folder, or the 25,344-byte three-part `.bin`) and writes `data.js`: font, every string, scenery shapes, monsters and their stats, items, village and cave sprites. |
| `tools/emu.js` | A small cycle-counting 6502 + TIA + RIOT + Supercharger emulator in Node. Renders frames to PNG. Good enough to play the real game and watch memory. |
| `tools/play.js` | Scripts input into the emulator and saves screenshots: `node play.js Dragonstomper.bin "W60 RESET5 W30 U180 S"`. |
| `tools/disasm.py` | Disassembler: `DS_ROMS=dir python3 disasm.py "Dragonstomper (1 of 3).bin" <bank> <start> <end>`. |

## The tape format

Each load is 8,448 bytes: 8 KB of data in 32 pages, then a 256-byte header.

| Header byte | Meaning |
| --- | --- |
| `$00-$01` | start address |
| `$02` | bank configuration byte (also left in `$80` by the BIOS; the game reuses it to restore banking) |
| `$03` | page count |
| `$05` | load number (part 1 = `$00`, part 2 = `$04`, part 3 = `$05`; the preview is also `$00`) |
| `$10+` | where each page goes: bits 0-1 bank, bits 2-4 page within the 2 KB bank |

Part 2 skips bank 0 pages 5-7 and bank 2 page 0. Those survive the next tape load, which is how your
stats and inventory reach the village.

Banking (bits 2-4 of the config byte): config 2 = `$F000` bank 2 / `$F800` bank 0, config 6 = bank 2
/ bank 1, and so on. The game logic runs in config 6, the display kernels in config 2.

Writing to Supercharger RAM is the usual trick: `CMP $F0xx` latches `xx`, and the 5th distinct bus
access after it writes the byte. Dragonstomper does it as `CMP $F000,X / TAX / CMP $F251,X`.
The emulator counts distinct accesses exactly like Stella's `CartAR` so this works.

## Text

The message window is a 48x20 bitmap in bank 2 (`$F100-$F177`) drawn by the 6-sprite kernel at
`$F9CD`. A print routine (part 1 `$FEC6`, bank 1) renders strings into it:

* bytes `< $40` are characters of a 3x5 font (glyph pointers at bank 1 `$F871`)
* `$40-$7F` print a BCD number from zero page (`$60 + byte`: `$AA` STR, `$AC` DEX, `$DC/$DD` gold)
* `$80-$FE` insert another string (a dictionary: `CHURCH`, `YOU`, monster names...)
* `$FF` ends the string

String pointer tables: part 1 `$F89A`/`$F900` (102 strings), part 2 `$FC4E`/`$FCB5` (103),
part 3 `$FC8A`/`$FCE8` (94). Character codes: space, A-Z (code `$11` is `!`), arrows
▲▶◀▼, 0-9.

Vocabulary recovered, among others: monsters DEMON BUG WARRIOR SNAKE SPIDER GHOUL MANIAC SLIME
BEETLE GOLEM MONKEY SCORPION GUARD; items PAPER (the ID) HANDAXE CHEST SHIELD KEY CROSS RING STAFF
CHARM POTION; places TREE HUT LAKE SWAMP GRASS TRAP TEMPLE PIT BRIDGE CASTLE CHURCH; village TRADE
SHOP, MAGIC SHOP, HOSPITAL, LANTERN MEDICINE SAPPHIRE CHAIN ROPE RUBY VITAMINS ELIXIR LONGBOW,
spells BLAST FLASH STUN UNLOCK VISION PROTECT, "ENLIST", "ONE MAN IS INTERESTED", the gate sign
"!ABANDON ALL HOPE YE WHO ENTER HERE! AND IT WARNS !BEWARE OF THE DRAGON!"; cave ROCK TRAP BONES
TORCH POISON DART, "SICK WITH FEVER", "FELL INTO THE PIT", "DRAGON MAGIC CANCELS IT", "TOO FAR
AWAY", "A WARRIOR WAS HIT/KILLED", "YOU HAVE DEFEATED THE DRAGON".

## Graphics

Sprites are stored bottom row first and packed back to back.

* **Countryside scenery** (part 1 kernel, `$F93E`): 13 types; colour `$FD13,y`, pointer
  `$FCF9/$FD06,y`. Heights come from the gap to the next pointer. Pine, oak, church (with its cross),
  temple, hut, lake, swamp, grass, rocks, castle.
* **Monsters and items** (part 1, bank 2): 23 entities. Sprite pointer `$F1B4/$F1CB`, height
  `$F1E2`, colour `$F22D`, flags `$F244` (bit 7 = they come in threes, like the monkeys). Entity *n*
  is named by string `$4E + n`.
* **Village** (part 2, bank 2): gate `$F6AB`, palm `$F68E`, magic shop `$F659`, tree `$F681`,
  hospital `$F673`, warrior `$F763`, trade shop `$F667`, door `$F69D`.
* **Cave** (part 3, bank 2): bones `$F718`, skull `$F70C`, torch `$F6E6`, rock `$F6F1`.
  The dragon is not a stored sprite, so the 3D one is hand built in the same style.

## Rules

| Routine | Behaviour |
| --- | --- |
| `$F691` random | 16-bit shift register at `$A2/$A3`: `A=$A2; LSR; LSR; SBC $A2; LSR; ROL $A3; ROR $A2` |
| `$F69F` randBelow(n) | `(rnd >> 1) mod (n + 1)` |
| `$F6AF` contest(a, y) | true when `randBelow(y) >= randBelow(a) >> 1` |
| `$F55E` encounter | monster HP `$F1F9,x`, damage `$F206,x`, attack `$F213,x` |
| `$FC6A` you fight | hit if contest(monster attack, DEX); damage `randBelow($AD) + $D6` (weapon 4); 0 = miss; dies when damage > HP |
| `$FDB7` it fights | hit if contest(DEX, monster attack); damage `randBelow(monster damage)`, taken off STR in BCD |
| `$FD2F` escape | contest(2 × monster attack, DEX) |
| `$F580` loot | `$F220,x`: low nibble = item dropped, bits 4-6 = keep threshold, bit 7 = gold (`$F672`: `randBelow(9)` in the low BCD digit, `randBelow(damage)` in the tens) |
| `$F640` key + chest | 75 gold plus an item from the table at `$F666` |
| `$FD63` church | DONATE 200 = STR to max, DONATE 15 = maybe +1, PRAY = nothing |
| `$F6E0` magic items | CROSS/RING/STAFF/CHARM/POTION each get one of five effects, shuffled at power-up (`$F15F` into `$C2-$C6`): full STR and max +2 (`$F6F3`), DEX ¼ of the way to 49 (`$F70B`), DEX down ¼ (`$F70D`), STR halved (`$F702`), protection (`$D5`). The game only ever says "THE x FEELS WEIRD". |
| `$FCE2` regen | STR +1 every 256 frames up to max |

Starting stats: STR 23, DEX 23, 400 gold, weapon 4.

| Monster | HP | DMG | ATK | Drops |
| --- | --- | --- | --- | --- |
| DEMON | 4 | 3 | 15 | CHARM, gold |
| BUG | 5 | 4 | 16 | KEY |
| WARRIOR | 9 | 6 | 17 | HANDAXE, gold |
| SNAKE | 8 | 4 | 18 | RING |
| SPIDER (x3) | 7 | 3 | 15 | KEY |
| GHOUL | 8 | 4 | 16 | CHEST, gold |
| MANIAC (x3) | 6 | 5 | 17 | PAPER (always), gold |
| SLIME | 6 | 4 | 18 | STAFF |
| BEETLE | 5 | 3 | 13 | KEY, gold |
| GOLEM | 6 | 4 | 3 | CROSS, gold |
| MONKEY (x3) | 4 | 5 | 19 | POTION, gold |
| SCORPION | 8 | 5 | 16 | CHEST, gold |
| GUARD | 112 | 9 | 31 | SHIELD, gold |

## What the 3D version adds or guesses

The text names everything in parts 2 and 3, but I did not trace their prices or exact effects, so
the village prices, what each good/spell does, warrior wages, cave hazard damage, and the dragon's
numbers (HP 60, attack 25, damage 9) are design choices in the spirit of the original. The dragon
fight is real time; everything else keeps the original turn-by-turn menus.

## The realistic rebuild

The voxel version was replaced by a realistic one. The rules module (`js/rules.js`) and the extracted data are unchanged; the real-time layer only decides *whether* a swing or a bite reaches its target (reach, facing, block, dodge). When it does, the hit and the damage are rolled exactly as on the 2600: `contest(monster attack, your DEX)` and `randBelow(weapon) + bonus` for you, `contest(DEX, monster attack)` and `randBelow(monster damage)` against you. Strength regenerates one point every 256 frames, the magic items are shuffled per game, the church, shops and loot tables are the ROM's.

Characters and creatures are sculpted procedurally (signed-distance shapes polygonised with marching cubes and skinned to bone rigs), so there are no character assets to download; the world uses Poly Haven CC0 scans.
