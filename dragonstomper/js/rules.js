// Game rules ported from the Dragonstomper 6502 code (addresses are part 1 unless noted).
import { ROM } from '../data.js';

// ---- random numbers: the 16-bit shift register at $A2/$A3 (routine $F691) ----
let lo = 0x5a, hi = 0xc3;
export function seed(n) { lo = (n & 0xff) || 0x5a; hi = ((n >> 8) & 0xff) || 0xc3; }
export function rnd() {
  let a = lo;
  a >>= 1; const c1 = a & 1; a >>= 1;               // LSR, LSR (carry = bit 1 of $A2)
  let r = a - lo - (1 - c1); a = r & 0xff;          // SBC $A2
  const c = a & 1; a >>= 1;                          // LSR
  const c2 = (hi >> 7) & 1; hi = ((hi << 1) | c) & 0xff; // ROL $A3
  lo = (lo >> 1) | (c2 << 7);                        // ROR $A2
  return lo;
}
// $F69F: a number from 0 to n inclusive
export const randBelow = (n) => (rnd() >> 1) % (n + 1);
// $F6AF: the defender's roll against half the attacker's. True when y "wins".
export const contest = (a, y) => randBelow(y) >= (randBelow(a) >> 1);
export const pick = (arr) => arr[randBelow(arr.length - 1)];

export const MONSTERS = Object.fromEntries(ROM.part1.monsters.map(m => [m.name, m]));
export const ITEM_NAMES = ['PAPER', 'HANDAXE', 'CHEST', 'SHIELD', 'KEY', 'CROSS', 'RING', 'STAFF', 'CHARM', 'POTION'];
export const MAGIC = ['CROSS', 'RING', 'STAFF', 'CHARM', 'POTION'];

// Village goods (part 2 strings). Prices and uses are ours; the original's aren't in the text.
export const GOODS = {
  LANTERN: { price: 60, info: 'LIGHTS THE CAVE' },
  MEDICINE: { price: 40, info: 'CURES FEVER' },
  SAPPHIRE: { price: 150, info: 'WARDS DRAGON MAGIC' },
  CHAIN: { price: 120, info: 'ARMOUR -1 DAMAGE' },
  ROPE: { price: 30, info: 'CLIMB OUT OF PITS' },
  RUBY: { price: 180, info: 'HALVES DRAGON FIRE' },
  VITAMINS: { price: 50, info: 'MAX STR +3' },
  ELIXIR: { price: 90, info: 'RESTORES ALL STR' },
  LONGBOW: { price: 140, info: 'SHOOT FROM AFAR' },
};
export const SPELLS = {
  BLAST: { price: 80, info: 'HURLS FIRE 6-18' },
  FLASH: { price: 40, info: 'BLINDS FOR 2 TURNS' },
  STUN: { price: 60, info: 'FREEZES THE FOE' },
  UNLOCK: { price: 30, info: 'OPENS CHAINED DOORS' },
  VISION: { price: 30, info: 'SHOWS HIDDEN TRAPS' },
  PROTECT: { price: 70, info: 'BLOCKS 3 BLOWS' },
};
export const SELL_PRICE = { PAPER: 20, HANDAXE: 40, CHEST: 30, SHIELD: 45, KEY: 15, CROSS: 25, RING: 60, STAFF: 35, CHARM: 30, POTION: 20 };

export function newPlayer() {
  const s = ROM.start;
  const p = {
    part: 1, str: s.str, maxStr: s.str, dex: s.dex, gold: s.gold, weapon: s.weapon, bonus: 0,
    inv: Object.fromEntries(ITEM_NAMES.map(n => [n, 0])),
    goods: Object.fromEntries(Object.keys(GOODS).map(n => [n, 0])),
    spells: Object.fromEntries(Object.keys(SPELLS).map(n => [n, 0])),
    warriors: 0, protect: 0, fever: 0, stuck: 0, kills: 0, seed: (Math.random() * 65535) | 0,
    // $C2-$C6: at power-up the five magic items get the five effects in a random order ($F15F)
    magic: {},
  };
  const fx = [0, 1, 2, 3, 4];
  for (let i = fx.length - 1; i > 0; i--) { const j = (Math.random() * (i + 1)) | 0; [fx[i], fx[j]] = [fx[j], fx[i]]; }
  MAGIC.forEach((n, i) => p.magic[n] = fx[i]);
  return p;
}

// $F6E0: the magic item effects. Every one of them prints "THE <ITEM> FEELS WEIRD".
export function useMagic(p, name) {
  p.inv[name]--;
  switch (p.magic[name]) {
    case 0: p.protect = 3; break;                                         // $D5: shielded for a while
    case 1: p.dex = (((49 + p.dex) >> 1) + p.dex) >> 1; break;           // $F70B: DEX a quarter of the way to 49
    case 2: p.dex = Math.max(1, ((p.dex >> 1) + p.dex) >> 1); break;     // $F70D: DEX down a quarter
    case 3: p.str = p.str >> 1; break;                                    // $F702: poison, STR halved
    case 4: p.str = p.maxStr; if (p.maxStr + 2 <= 50) p.maxStr += 2; break; // $F6F3: STR to max, max +2
  }
  return ['THE ' + name, 'FEELS WEIRD'];
}

export function spawnMonster(name) {
  const m = MONSTERS[name];
  return { ...m, hpLeft: m.hp, blind: 0, asleep: false };
}

// $FC6A: you swing at the monster
export function playerAttack(p, m) {
  if (!contest(m.atk, p.dex)) return { hit: false, msg: ['YOU MISSED'] };
  const dmg = randBelow(p.weapon) + p.bonus;
  if (dmg === 0) return { hit: false, msg: ['YOU MISSED'] };
  if (dmg > m.hpLeft) { m.hpLeft = -1; return { hit: true, dmg, killed: true, msg: ['YOU KILLED', 'THE ' + m.name] }; }
  m.hpLeft -= dmg;
  return { hit: true, dmg, msg: ['YOU JUST HIT', 'THE ' + m.name, 'FOR ' + pad(dmg) + ' UNITS'] };
}

// $FDB7: the monster swings at you
export function monsterAttack(p, m) {
  if (m.blind > 0) { m.blind--; return { hit: false, msg: ['THE ' + m.name, 'CANNOT SEE YOU'] }; }
  if (!contest(p.dex, m.atk)) return { hit: false, msg: ['THE ' + m.name, 'MISSED'] };
  let dmg = randBelow(m.dmg);
  if (dmg === 0) return { hit: false, msg: ['THE ' + m.name, 'MISSED'] };
  if (p.protect > 0) { p.protect--; return { hit: false, msg: ['YOU WERE HIT', 'NO EFFECT'] }; }
  if (p.inv.SHIELD > 0) dmg = Math.max(1, dmg - 1);
  if (p.goods.CHAIN > 0) dmg = Math.max(1, dmg - 1);
  p.str -= dmg;
  return { hit: true, dmg, dead: p.str < 0, msg: ['YOU WERE HIT', 'YOU TAKE ' + pad(dmg), 'UNITS DAMAGE'] };
}

// $FD2F: trying to walk away
export const tryEscape = (p, m) => contest(m.atk * 2, p.dex);

// $F580 / $F672: what a dead monster leaves
export function loot(m) {
  const out = { gold: 0, item: null };
  if (m.gold) out.gold = randBelow(9) + 10 * randBelow(m.dmg);
  if (m.drop && (rnd() >> 1) >= m.dropKeep) out.item = m.drop;
  return out;
}

// $F640: a KEY opens a CHEST: 75 gold and maybe something inside
export function openChest(p) {
  p.inv.KEY--; p.inv.CHEST--;
  const item = ROM.part1.chestLoot[rnd() & 7];
  p.gold += 75;
  if (item) p.inv[item]++;
  return item;
}

// $FD63: the church. 0 pray, 1 donate 15 (maybe +1 STR), 2 donate 200 (STR back to max)
export function church(p, choice) {
  if (choice === 0) return ['NOBODY HERE', 'ANSWERS'];
  const cost = choice === 1 ? 15 : 200;
  if (p.gold < cost) return ['YOU CANNOT', 'DO THAT'];
  p.gold -= cost;
  const gain = choice === 2 ? p.maxStr - p.str : randBelow(1);
  if (gain <= 0) return ['NOTHING', 'HAPPENS'];
  p.str += gain;
  return ['YOU GAIN', pad(gain) + ' STRENGTH'];
}

export const pad = (n) => (n < 10 ? '0' : '') + n;
