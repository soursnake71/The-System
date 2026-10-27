#!/usr/bin/env python3
"""Pull the game data out of a Starpath Supercharger Dragonstomper dump and write ../data.js.

usage: python3 extract.py <dragonstomper.zip | Dragonstomper.bin | folder with the three parts> [out.js]

The Supercharger loads are 8448 bytes each: 8K of data in 256-byte pages plus a 256-byte header.
Header byte 3 is the page count and bytes $10.. say where each page goes (bits 0-1 bank, bits 2-4
page within the 2K bank). The full-game file is the three loads back to back (load numbers 0, 4, 5).
Everything below was found by disassembling the loads and running them in tools/emu.js; see
../REVERSE_ENGINEERING.md for the addresses and what the code does with them.
"""
import io, json, os, sys, zipfile

LOAD = 8448


def read_loads(path):
    blobs = []
    if os.path.isdir(path):
        for n in sorted(os.listdir(path)):
            if n.lower().endswith('.bin'):
                blobs.append(open(os.path.join(path, n), 'rb').read())
    elif zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as z:
            for n in sorted(z.namelist()):
                if n.lower().endswith('.bin'):
                    blobs.append(z.read(n))
    else:
        blobs.append(open(path, 'rb').read())
    loads = {}
    for b in blobs:
        for o in range(0, len(b) - LOAD + 1, LOAD):
            d = b[o:o + LOAD]
            num = d[0x2005]
            if num == 0 and 0 in loads:
                continue  # the preview load is also number 0; keep the first full one
            loads.setdefault(num, d)
    missing = [n for n in (0, 4, 5) if n not in loads]
    if missing:
        sys.exit('missing Supercharger load(s) %s - need all three parts of Dragonstomper' % missing)
    return loads


def banks_of(load):
    h = load[0x2000:]
    banks = [bytearray(2048) for _ in range(3)]
    for i in range(h[3]):
        b = h[0x10 + i]
        bank, page = b & 3, (b >> 2) & 7
        banks[bank][page * 256:page * 256 + 256] = load[i * 256:i * 256 + 256]
    return banks


class Mem:
    """CPU view of the cartridge RAM for one bank configuration: $F000 bank, $F800 bank."""
    def __init__(self, banks, lo, hi):
        self.b, self.map = banks, (lo, hi)

    def __getitem__(self, a):
        return self.b[self.map[(a >> 11) & 1]][a & 0x7ff]


# Stella's NTSC palette (index = colour byte >> 1)
NTSC = [
    0x000000, 0x4a4a4a, 0x6f6f6f, 0x8e8e8e, 0xaaaaaa, 0xc0c0c0, 0xd6d6d6, 0xececec,
    0x484800, 0x69690f, 0x86861d, 0xa2a22a, 0xbbbb35, 0xd2d240, 0xe8e84a, 0xfcfc54,
    0x7c2c00, 0x904811, 0xa26221, 0xb47a30, 0xc3903d, 0xd2a44a, 0xdfb755, 0xecc860,
    0x901c00, 0xa33915, 0xb55328, 0xc66c3a, 0xd5824a, 0xe39759, 0xf0aa67, 0xfcbc74,
    0x940000, 0xa71a1a, 0xb83232, 0xc84848, 0xd65c5c, 0xe46f6f, 0xf08080, 0xfc9090,
    0x840064, 0x97197a, 0xa8308f, 0xb846a2, 0xc659b3, 0xd46cc3, 0xe07cd2, 0xec8ce0,
    0x500084, 0x68199a, 0x7d30ad, 0x9246c0, 0xa459d0, 0xb56ce0, 0xc57cee, 0xd48cfc,
    0x140090, 0x331aa3, 0x4e32b5, 0x6848c6, 0x7f5cd5, 0x956fe3, 0xa980f0, 0xbc90fc,
    0x000094, 0x181aa7, 0x2d32b8, 0x4248c8, 0x545cd6, 0x656fe4, 0x7580f0, 0x8490fc,
    0x001c88, 0x183b9d, 0x2d57b0, 0x4272c2, 0x548ad2, 0x65a0e1, 0x75b5ef, 0x84c8fc,
    0x003064, 0x185080, 0x2d6d98, 0x4288b0, 0x54a0c5, 0x65b7d9, 0x75cceb, 0x84e0fc,
    0x004030, 0x18624e, 0x2d8169, 0x429e82, 0x54b899, 0x65d1ae, 0x75e7c2, 0x84fcd4,
    0x004400, 0x1a661a, 0x328432, 0x48a048, 0x5cba5c, 0x6fd26f, 0x80e880, 0x90fc90,
    0x143c00, 0x355f18, 0x527e2d, 0x6e9c42, 0x87b754, 0x9ed065, 0xb4e775, 0xc8fc84,
    0x303800, 0x505916, 0x6d762b, 0x88923e, 0xa0ab4f, 0xb7c25f, 0xccd86e, 0xe0ec7c,
    0x482c00, 0x694d14, 0x866a26, 0xa28638, 0xbb9f47, 0xd2b656, 0xe8cc63, 0xfce070,
]
def rgb(c): return '#%06x' % NTSC[(c & 0xff) >> 1]

# Character codes of the 4-pixel font (codes $2C+ are unused garbage)
CHARS = ' ABCDEFGHIJKLMNOP!RSTUVWXYZ' + '▲▶◀▼' + '0123456789' + 'KZ?'

MONSTERS = ['DEMON', 'BUG', 'WARRIOR', 'SNAKE', 'SPIDER', 'GHOUL', 'MANIAC', 'SLIME', 'BEETLE',
            'GOLEM', 'MONKEY', 'SCORPION', 'GUARD']
ITEMS = ['PAPER', 'HANDAXE', 'CHEST', 'SHIELD', 'KEY', 'CROSS', 'RING', 'STAFF', 'CHARM', 'POTION']
# the 13 countryside scenery shapes the part 1 kernel draws, in table order
SCENERY = ['PINE', 'RUIN', 'CHURCH', 'TEMPLE', 'BUSH', 'OAK', 'SWAMP', 'GRASS', 'SKULLROCK',
           'HUT', 'BOULDER', 'BRAMBLE', 'CASTLE']


def sprite(mem, ptr, h):
    """Atari sprites are stored bottom row first; return rows top-down with blank rows trimmed."""
    rows = [mem[ptr + h - 1 - i] for i in range(h)]
    while rows and rows[0] == 0:
        rows.pop(0)
    while rows and rows[-1] == 0:
        rows.pop()
    return rows


def font(banks):
    m = banks[1]
    out = {}
    for c in range(0x2c):
        p = m[0x71 + c]
        rows = [m[p + 4 - i] >> 4 for i in range(5)]
        out[CHARS[c]] = rows
    return out


def strings(banks, lo, hi, n):
    mem = Mem(banks, 2, 1)
    ptr = lambda i: mem[lo + i] | mem[hi + i] << 8

    def dec(i, depth=0):
        p, s = ptr(i), ''
        for k in range(32):
            v = mem[p + k]
            if v == 0xff:
                break
            if v < 0x40:
                s += CHARS[v] if v < len(CHARS) else '?'
            elif v >= 0x80:
                s += dec(v & 0x7f, depth + 1) if depth < 4 else ''
                if depth:
                    break
            else:
                s += '#'  # a BCD number from zero page
                if depth:
                    break
        return s
    return [dec(i) for i in range(n)]


def part1(banks):
    k = Mem(banks, 2, 0)      # kernel bank layout (config 2): $F000 bank 2, $F800 bank 0
    t = Mem(banks, 2, 1)      # game-logic layout (config 6): $F000 bank 2, $F800 bank 1
    # scenery: colour $FD13,y  pointer $FCF9/$FD06,y. Shapes are packed back to back.
    ptrs = [k[0xfcf9 + i] | k[0xfd06 + i] << 8 for i in range(13)]
    starts = sorted(set(ptrs))
    scen = {}
    for i, name in enumerate(SCENERY):
        p = ptrs[i]
        nxt = [s for s in starts if s > p]
        h = (nxt[0] - p) if nxt else 13
        rows = sprite(k, p, h)
        while rows and rows[0] == 0:
            rows.pop(0)
        scen[name] = {'rows': rows, 'color': rgb(k[0xfd13 + i])}
    # entities: sprite lo/hi $F1B4/$F1CB, height $F1E2, colour $F22D, group flag $F244
    # monster HP $F1F9, damage $F206, attack $F213, loot $F220 (all indexed by monster number)
    ents = []
    for i in range(23):
        p = t[0xf1b4 + i] | t[0xf1cb + i] << 8
        h = t[0xf1e2 + i]
        rows = sprite(k, p, h)
        while rows and rows[0] == 0:
            rows.pop(0)
        while rows and rows[-1] == 0:
            rows.pop()
        e = {'rows': rows, 'color': rgb(t[0xf22d + i]), 'flags': t[0xf244 + i]}
        if i < 13:
            loot = t[0xf220 + i]
            e.update(name=MONSTERS[i], hp=t[0xf1f9 + i], dmg=t[0xf206 + i], atk=t[0xf213 + i],
                     drop=(ITEMS[loot & 15] if (loot & 15) < 10 else None),
                     dropKeep=(loot & 0x70), gold=bool(loot & 0x80), group=bool(t[0xf244 + i] & 0x80))
        else:
            e.update(name=ITEMS[i - 13])
        ents.append(e)
    chest = [t[0xf666 + i] for i in range(11)]
    return {
        'scenery': scen,
        'monsters': [e for e in ents if 'hp' in e],
        'items': {e['name']: {'rows': e['rows'], 'color': e['color']} for e in ents if 'hp' not in e},
        'chestLoot': [ITEMS[c] if c < 10 else None for c in chest[:8]],
        'strings': strings(banks, 0xf89a, 0xf900, 0x66),
    }


def part2(banks):
    m = Mem(banks, 2, 0)
    def at(off, h, col):
        return {'rows': sprite(m, 0xf000 + off, h), 'color': rgb(col)}
    return {
        'sprites': {
            'GATE': at(0x6ab, 12, 0x14), 'PALM': at(0x68e, 13, 0xc8), 'MAGICSHOP': at(0x659, 13, 0x74),
            'TREE': at(0x681, 13, 0xb6), 'HOSPITAL': at(0x673, 13, 0x0e), 'WARRIOR': at(0x763, 15, 0x38),
            'TRADESHOP': at(0x667, 10, 0x48), 'DOOR': at(0x69d, 12, 0x00),
        },
        'strings': strings(banks, 0xfc4e, 0xfcb5, 0x67),
    }


def part3(banks):
    m = Mem(banks, 2, 0)
    def at(off, h, col):
        return {'rows': sprite(m, 0xf000 + off, h), 'color': rgb(col)}
    return {
        'sprites': {
            'BONES': at(0x718, 12, 0x0e), 'SKULL': at(0x70c, 9, 0x0e), 'TORCH': at(0x6e6, 7, 0xdc),
            'ROCK': at(0x6f1, 13, 0x08), 'FLAME': at(0x700, 12, 0x36),
        },
        'strings': strings(banks, 0xfc8a, 0xfce8, 0x5e),
    }


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    loads = read_loads(sys.argv[1])
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'data.js')
    data = {
        'font': font(banks_of(loads[0])),
        'part1': part1(banks_of(loads[0])),
        'part2': part2(banks_of(loads[4])),
        'part3': part3(banks_of(loads[5])),
        'start': {'str': 23, 'dex': 23, 'gold': 400, 'weapon': 4},
    }
    js = ('// Generated by tools/extract.py from the Starpath Supercharger Dragonstomper loads. Do not edit.\n'
          '// Sprite rows are top-down bytes (bit 7 = leftmost pixel); colours are NTSC palette entries.\n'
          'export const ROM = ' + json.dumps(data, separators=(',', ':')) + ';\n')
    open(out, 'w').write(js)
    print('wrote', os.path.normpath(out), len(js), 'bytes')


if __name__ == '__main__':
    main()
