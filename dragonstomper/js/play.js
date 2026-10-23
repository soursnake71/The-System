// Battles, the USE / STATUS menus and the fire-button menu, shared by all three areas.
import * as THREE from '../lib/three.module.min.js';
import { ROM } from '../data.js';
import { G, swing, floatText, shake } from './engine.js';
import { fromSprite, meshGeometry } from './voxel.js';
import { sfx } from './audio.js';
import * as R from './rules.js';

const geoCache = {};
export function monsterMesh(name, scale = 1.25) {
  const m = R.MONSTERS[name];
  let color = m.color === '#000000' ? '#3a3a3a' : m.color;
  if (!geoCache[name]) geoCache[name] = meshGeometry(fromSprite(m.rows, color, 'taper', { depth: 4, recess: false }), 0.3 * scale, 0.22 * scale, 0.3 * scale);
  geoCache[name].userData.keep = true;
  const mat = new THREE.MeshLambertMaterial({ vertexColors: true, emissive: 0x000000 });
  const mesh = new THREE.Mesh(geoCache[name], mat);
  mesh.castShadow = true;
  const g = new THREE.Group();
  g.add(mesh);
  g.userData = { mesh, mat, t: Math.random() * 6, flash: 0, lunge: 0, dying: 0, base: null };
  return g;
}

const itemGeo = {};
export function itemMesh(name, scale = 0.7) {
  const it = ROM.part1.items[name];
  if (!it) return null;
  if (!itemGeo[name]) { itemGeo[name] = meshGeometry(fromSprite(it.rows, it.color, 'taper', { depth: 2, recess: false }), 0.3 * scale, 0.22 * scale, 0.3 * scale); itemGeo[name].userData.keep = true; }
  const m = new THREE.Mesh(itemGeo[name], new THREE.MeshLambertMaterial({ vertexColors: true, emissive: 0x222222 }));
  m.castShadow = true;
  return m;
}

// little pop-up of a found item that spins and fades
export function showPickup(name, pos) {
  const m = itemMesh(name, 0.9);
  if (!m) return;
  m.position.copy(pos); m.position.y += 1;
  G.scene.add(m);
  const t0 = G.time;
  G.anims.push(dt => {
    const t = G.time - t0;
    m.rotation.y += dt * 4; m.position.y += dt * (t < 0.6 ? 3 : 0.5);
    if (t > 2.2) { m.parent?.remove(m); m.material.dispose(); return false; }
    return true;
  });
  G.fx.burst(m.position, ['#fff', '#fc0'], 18, { speed: 3, up: 4, size: 0.12 });
}

export function animateMonsters(dt, list, target) {
  for (const g of list) {
    const u = g.userData;
    u.t += dt;
    if (!u.base) u.base = g.position.clone();
    if (u.dying > 0) {
      u.dying += dt;
      g.rotation.z = Math.min(1.5, u.dying * 4);
      g.scale.setScalar(Math.max(0.01, 1 - u.dying * 0.8));
      continue;
    }
    if (u.rise != null && u.rise < 1) { u.rise = Math.min(1, u.rise + dt * 2.5); }
    const r = u.rise ?? 1;
    const lungeK = u.lunge > 0 ? Math.sin(u.lunge * Math.PI) : 0;
    if (u.lunge > 0) u.lunge = Math.max(0, u.lunge - dt * 2.8);
    g.position.y = u.base.y + Math.abs(Math.sin(u.t * 5)) * 0.25 - (1 - r) * 3;
    if (target) {
      const dx = target.x - u.base.x, dz = target.z - u.base.z, d = Math.hypot(dx, dz) || 1;
      g.position.x = u.base.x + dx / d * lungeK * Math.max(0, d - 2);
      g.position.z = u.base.z + dz / d * lungeK * Math.max(0, d - 2);
      g.rotation.y = Math.atan2(dx, dz);
    }
    if (u.flash > 0) { u.flash -= dt; u.mat.emissive.setRGB(u.flash > 0 ? 0.8 : 0, u.flash > 0 ? 0.8 : 0, u.flash > 0 ? 0.8 : 0); }
  }
}

const wait = (ms) => new Promise(r => setTimeout(r, ms));
const plural = (n) => n.endsWith('Y') && !n.endsWith('EY') ? n.slice(0, -1) + 'IES' : n + 'S';
const article = (n) => /^[AEIOU]/.test(n) ? 'AN ' + n : 'A ' + n;

// ---------- the battle ----------
// names: monster names (a group shares one name); origin: where they jump out from
export async function battle(names, origin, opts = {}) {
  const p = G.p, ui = G.ui;
  const hero = G.hero.position;
  const dir = new THREE.Vector3(origin.x - hero.x, 0, origin.z - hero.z);
  if (dir.length() < 0.1) dir.set(0, 0, -1);
  dir.normalize();
  const side = new THREE.Vector3(-dir.z, 0, dir.x);
  // step back from whatever they jumped out of, and have them stand in front of it
  const objR = opts.objR ?? 0;
  const back = objR + (opts.dist || 6.5);
  hero.x = origin.x - dir.x * back; hero.z = origin.z - dir.z * back;
  if (G.area.clamp) [hero.x, hero.z] = G.area.clamp(hero.x, hero.z);
  if (G.area.ground) hero.y = G.area.ground(hero.x, hero.z);
  const front = new THREE.Vector3(origin.x - dir.x * (objR + 1.3), 0, origin.z - dir.z * (objR + 1.3));
  const foes = names.map((n, i) => {
    const m = R.spawnMonster(n);
    const g = monsterMesh(n, opts.scale || 1.25);
    const off = (i - (names.length - 1) / 2) * 3.2;
    g.position.set(front.x + side.x * off, (opts.y ?? hero.y), front.z + side.z * off);
    g.userData.rise = 0;
    G.scene.add(g);
    G.battleMeshes.push(g);
    m.g = g;
    return m;
  });
  G.inBattle = true;
  G.battleFocus = new THREE.Vector3().copy(hero).lerp(front, 0.45);
  G.hero.rotation.y = Math.atan2(dir.x, dir.z);
  sfx('encounter');
  shake(0.3);
  const n0 = names[0];
  await ui.say(opts.intro || (names.length > 1 ? ['OH NO', plural(n0)] : ['OH NO', article(n0)]));

  const allies = opts.allies || [];
  let result = 'won';
  outer:
  for (const m of foes) {
    while (m.hpLeft >= 0) {
      p.bonus = p.inv.HANDAXE > 0 ? 2 : 0;
      G.ui.status(p);
      const c = await ui.dir4({ up: 'RUN', right: 'FIGHT', left: 'USE', down: 'STATUS' }, 'THE ' + m.name);
      let turnUsed = true;
      if (c === 'right') {
        swing();
        await wait(220);
        const r = R.playerAttack(p, m);
        if (r.hit) hitFx(m.g, r.dmg); else floatText(m.g.position.clone().add(new THREE.Vector3(0, 3.6, 0)), 'MISS', '#aaa');
        await ui.say(r.msg);
        if (r.killed) { await killFx(m); await lootFx(m); break; }
      } else if (c === 'up') {
        if (opts.noEscape) { await ui.say(['YOU CANNOT', 'ESCAPE']); }
        else if (R.tryEscape(p, m)) { await ui.say(['YOU ESCAPE']); result = 'fled'; break outer; }
        else await ui.say(['YOU CANNOT', 'GET AWAY']);
      } else if (c === 'left') {
        const r = await useMenu('battle', m);
        turnUsed = r.turn;
        if (m.hpLeft < 0) { await killFx(m); await lootFx(m); break; }
      } else if (c === 'down') { await statusScreen(); turnUsed = false; }
      else turnUsed = false;
      if (!turnUsed) continue;

      // your warriors take a swing (part 3)
      for (const a of allies) {
        if (!a.alive || m.hpLeft < 0) continue;
        if (R.contest(m.atk, 14)) {
          const d = R.randBelow(4) + 1;
          if (d > m.hpLeft) m.hpLeft = -1; else m.hpLeft -= d;
          a.lunge?.(); hitFx(m.g, d);
          await ui.say(['A WARRIOR HITS', 'FOR ' + R.pad(d) + ' UNITS'], { time: 700 });
        }
      }
      if (m.hpLeft < 0) { await killFx(m); await lootFx(m); break; }

      // the monster's turn
      if (m.stunned > 0) { m.stunned--; await ui.say(['THE ' + m.name, 'IS STUNNED']); continue; }
      const living = allies.filter(a => a.alive);
      if (living.length && R.randBelow(living.length) > 0) {
        const a = living[R.randBelow(living.length - 1)];
        m.g.userData.lunge = 1; await wait(250);
        if (R.contest(14, m.atk)) {
          const d = R.randBelow(m.dmg);
          a.hp -= d;
          sfx('hit');
          if (a.hp <= 0) { a.die?.(); await ui.say(['A WARRIOR', 'WAS KILLED']); }
          else await ui.say(['A WARRIOR', 'WAS HIT']);
        } else await ui.say(['THE ' + m.name, 'MISSED']);
        continue;
      }
      m.g.userData.lunge = 1;
      await wait(250);
      const r = R.monsterAttack(p, m);
      if (r.hit) { hurtFx(r.dmg); }
      G.ui.status(p);
      await ui.say(r.msg);
      if (r.dead || p.str < 0) { result = 'dead'; break outer; }
    }
  }
  for (const m of foes) { m.g.parent?.remove(m.g); m.g.userData.mat.dispose(); }
  G.battleMeshes.length = 0;
  G.inBattle = false; G.battleFocus = null;
  G.ui.status(p);
  if (result === 'dead') await death();
  return result;
}

function hitFx(g, dmg) {
  sfx('hit'); shake(0.25);
  g.userData.flash = 0.18;
  const pos = g.position.clone(); pos.y += 1.6;
  G.fx.burst(pos, ['#fff', '#fc5454', '#fc9090'], 10 + dmg * 3, { speed: 5, up: 5, size: 0.15 });
  floatText(pos.clone().add(new THREE.Vector3(0, 2, 0)), '-' + dmg, '#fc5454', 1.3);
}
export function hurtFx(dmg) {
  sfx('hurt'); shake(0.45);
  G.hero.userData.hurt = 0.6;
  const pos = G.hero.position.clone(); pos.y += 1.5;
  G.fx.burst(pos, ['#fc5454', '#940000'], 8 + dmg * 2, { speed: 4, up: 5, size: 0.14 });
  floatText(pos.clone().add(new THREE.Vector3(0, 2, 0)), '-' + dmg, '#fcbc74', 1.3);
}
async function killFx(m) {
  sfx('kill');
  m.g.userData.dying = 0.001;
  G.p.kills++;
  G.fx.burst(m.g.position.clone().add(new THREE.Vector3(0, 1.5, 0)), [m.color === '#000000' ? '#555' : m.color, '#fff'], 40, { speed: 7, up: 7, size: 0.2 });
  await wait(450);
}
async function lootFx(m) {
  const l = R.loot(m);
  const pos = m.g.position.clone();
  if (l.gold) { G.p.gold += l.gold; sfx('coin'); floatText(pos.clone().add(new THREE.Vector3(0, 2.5, 0)), '+' + l.gold + ' GOLD', '#fce070'); await G.ui.say(['YOU FIND', l.gold + ' GOLD']); }
  if (l.item) { G.p.inv[l.item]++; sfx('item'); showPickup(l.item, pos); await G.ui.say(['YOU FIND', article(l.item)]); }
  G.ui.status(G.p);
}

export async function death() {
  sfx('death');
  G.dead = true;
  await G.ui.say(['YOU ARE DEAD']);
}

// ---------- USE ----------
// returns { turn: did it take your turn }
export async function useMenu(ctx = 'field', foe = null) {
  const p = G.p;
  const items = [];
  for (const n of R.ITEM_NAMES) if (p.inv[n] > 0) items.push({ label: n + (p.inv[n] > 1 ? ' X' + p.inv[n] : ''), value: ['inv', n] });
  for (const n of ['MEDICINE', 'ELIXIR', 'VITAMINS']) if (p.goods[n] > 0) items.push({ label: n + (p.goods[n] > 1 ? ' X' + p.goods[n] : ''), value: ['goods', n] });
  for (const n of Object.keys(R.SPELLS)) if (p.spells[n] > 0) items.push({ label: 'SPELL ' + n + ' X' + p.spells[n], value: ['spell', n] });
  if (!items.length) { await G.ui.say(['YOU HAVE', 'NOTHING TO USE']); return { turn: false }; }
  const v = await G.ui.list('USE WHAT?', items);
  if (!v) return { turn: false };
  const [kind, name] = v;
  if (kind === 'inv') return useItem(name, ctx, foe);
  if (kind === 'goods') return useGood(name);
  return castSpell(name, ctx, foe);
}

async function useItem(name, ctx, foe) {
  const p = G.p;
  if (R.MAGIC.includes(name)) {
    const before = { str: p.str, dex: p.dex, max: p.maxStr };
    const msg = R.useMagic(p, name);
    sfx('magic');
    G.fx.burst(G.hero.position.clone().add(new THREE.Vector3(0, 2, 0)), ['#fc90fc', '#84fcd4', '#fce070'], 30, { speed: 3, up: 6, size: 0.12, gravity: 4 });
    G.ui.status(p);
    await G.ui.say(msg);
    if (p.str !== before.str) floatText(G.hero.position.clone().add(new THREE.Vector3(0, 4, 0)), (p.str > before.str ? '+' : '') + (p.str - before.str) + ' STR', p.str > before.str ? '#84fcd4' : '#fc5454');
    if (p.dex !== before.dex) floatText(G.hero.position.clone().add(new THREE.Vector3(0, 3.2, 0)), (p.dex > before.dex ? '+' : '') + (p.dex - before.dex) + ' DEX', p.dex > before.dex ? '#84fcd4' : '#fc5454');
    return { turn: true };
  }
  switch (name) {
    case 'KEY':
    case 'CHEST':
      if (p.inv.KEY > 0 && p.inv.CHEST > 0) {
        if (ctx === 'battle') { await G.ui.say(['YOU CANNOT', 'DO THAT NOW']); return { turn: false }; }
        const item = R.openChest(p);
        sfx('door'); sfx('coin');
        await G.ui.say(['YOU OPEN', 'THE CHEST', 'YOU FIND 75 GOLD']);
        if (item) { showPickup(item, G.hero.position); sfx('item'); await G.ui.say(['AND', article(item)]); }
        G.ui.status(p);
        return { turn: false };
      }
      await G.ui.say(name === 'CHEST' ? ['THE CHEST', 'IS LOCKED'] : ['NOTHING HERE', 'IS LOCKED']);
      return { turn: false };
    case 'HANDAXE':
      await G.ui.say(['THE HANDAXE', 'IS YOUR WEAPON', 'HIT +2']);
      return { turn: false };
    case 'SHIELD':
      await G.ui.say(['THE SHIELD', 'TURNS BLOWS', 'DAMAGE -1']);
      return { turn: false };
    case 'PAPER':
      if (G.area.onPaper) return G.area.onPaper();
      await G.ui.say(['IT SAYS', 'BEARER MAY', 'CROSS THE BRIDGE']);
      return { turn: false };
  }
  await G.ui.say(['NO EFFECT']);
  return { turn: false };
}

async function useGood(name) {
  const p = G.p;
  p.goods[name]--;
  if (name === 'MEDICINE') { p.fever = 0; p.str = Math.min(p.maxStr, p.str + 5); await G.ui.say(['THE FEVER', 'IS GONE']); }
  if (name === 'ELIXIR') { p.str = p.maxStr; await G.ui.say(['YOU FEEL', 'STRONG AGAIN']); }
  if (name === 'VITAMINS') { p.maxStr = Math.min(60, p.maxStr + 3); p.str += 3; await G.ui.say(['MAX STRENGTH', 'UP 3']); }
  sfx('magic'); G.ui.status(p);
  return { turn: true };
}

export async function castSpell(name, ctx, foe) {
  const p = G.p;
  // the area gets first go (UNLOCK, VISION, the dragon)
  if (G.area.onSpell) { const r = await G.area.onSpell(name, foe); if (r) return r; }
  if (!foe) {
    if (name === 'PROTECT') { p.spells[name]--; p.protect = 3; sfx('magic'); await G.ui.say(['YOU ARE', 'PROTECTED']); return { turn: true }; }
    await G.ui.say(['NO EFFECT', 'HERE']);
    return { turn: false };
  }
  p.spells[name]--;
  sfx('magic');
  if (foe.dragon && p.goods.SAPPHIRE <= 0 && R.randBelow(2) === 0) { await G.ui.say(['DRAGON MAGIC', 'CANCELS IT']); return { turn: true }; }
  switch (name) {
    case 'BLAST': {
      const d = R.randBelow(12) + 6;
      G.fx.burst(foe.g.position.clone().add(new THREE.Vector3(0, 1.5, 0)), ['#fc9090', '#fce070', '#fff'], 50, { speed: 8, up: 6, size: 0.22 });
      shake(0.5); sfx('boom');
      if (d > foe.hpLeft) foe.hpLeft = -1; else foe.hpLeft -= d;
      floatText(foe.g.position.clone().add(new THREE.Vector3(0, 4, 0)), '-' + d, '#fce070', 1.4);
      await G.ui.say(['BLAST', 'FOR ' + R.pad(d) + ' UNITS']);
      break;
    }
    case 'FLASH': foe.blind = 2; G.fx.burst(foe.g.position.clone().add(new THREE.Vector3(0, 2, 0)), '#fff', 40, { speed: 9, up: 3, size: 0.1, gravity: 0 }); await G.ui.say(['THE ' + (foe.name || 'DRAGON'), 'IS BLINDED']); break;
    case 'STUN': foe.stunned = 2; await G.ui.say(['THE ' + (foe.name || 'DRAGON'), 'IS STUNNED']); break;
    case 'PROTECT': p.protect = 3; await G.ui.say(['YOU ARE', 'PROTECTED']); break;
    default: await G.ui.say(['NO EFFECT']);
  }
  return { turn: true };
}

// ---------- STATUS ----------
export async function statusScreen() {
  const p = G.p;
  await G.ui.say([
    String(p.gold).padStart(4, '0') + ' GOLD',
    ' ' + R.pad(Math.max(0, p.str)) + '/' + R.pad(p.maxStr) + ' STR',
    ' ' + R.pad(p.dex) + ' DEX' + (p.fever ? ' FEVER' : ''),
    'HIT 0-' + p.weapon + (p.bonus ? ' +' + p.bonus : '') + (p.warriors ? ' MEN ' + p.warriors : ''),
  ]);
  const inv = [];
  for (const n of R.ITEM_NAMES) if (p.inv[n]) inv.push(n + (p.inv[n] > 1 ? ' X' + p.inv[n] : ''));
  for (const n of Object.keys(p.goods)) if (p.goods[n]) inv.push(n + (p.goods[n] > 1 ? ' X' + p.goods[n] : ''));
  for (const n of Object.keys(p.spells)) if (p.spells[n]) inv.push('SPELL ' + n + ' X' + p.spells[n]);
  if (inv.length) await G.ui.list('INVENTORY', inv.map(l => ({ label: l, value: l }))); else await G.ui.say(['INVENTORY', 'EMPTY']);
}

// ---------- the fire-button menu, as in the original ----------
export async function fieldMenu() {
  const c = await G.ui.dir4({ up: 'MOVE', right: 'FIGHT', left: 'USE', down: 'STATUS' });
  if (c === 'right') { swing(); if (G.area.onFight) await G.area.onFight(); else await G.ui.say(['OH NO', 'NOBODY HERE']); }
  else if (c === 'left') await useMenu(G.area.ctx || 'field');
  else if (c === 'down') await statusScreen();
}

export { wait, article, plural };
