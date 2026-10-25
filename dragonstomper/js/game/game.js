// The game: owns the scene, the hero, monsters, allies and pickups, and runs the rules.
import * as THREE from 'three';
import { gfx, setScene, followSun, settings } from '../core/gfx.js';
import { input, pressed, held, lockPointer, unlockPointer } from '../core/input.js';
import { sfx, ambience, music } from '../core/sound.js';
import { knight } from '../actors/humanoid.js';
import { mocapReady } from '../actors/mocap.js';
import { Player } from './player.js';
import { CameraRig } from './camera.js';
import { Monster } from './monsters.js';
import { Pickup } from './items.js';
import { Particles, Trail, floatText, updateFloats, clearFloats, shake, hitstop } from './fx.js';
import { inventoryPanel } from '../ui/hud.js';
import { windUniform } from '../world/trees.js';
import { resolve } from './physics.js';
import * as R from '../rules.js';

const MAGIC_FX = ['Protection', 'Dexterity up', 'Dexterity down', 'Poison', 'Full strength, max +2'];
const SAVE_KEY = 'ds-real-save';

export class Game {
  constructor(hud) {
    this.hud = hud;
    this.cam = new CameraRig(gfx.camera);
    this.monsters = []; this.allies = []; this.pickups = [];
    this.time = 0; this.regenT = 0; this.paused = true; this.busy = false;
  }

  // ---------- areas ----------
  async enter(AreaClass, part, areaState = null, from = 'new') {
    this.paused = true;
    this.hud.clearBars(); clearFloats();
    if (this.area) this.area.dispose?.();
    this.monsters = []; this.allies = []; this.pickups = [];
    const scene = new THREE.Scene();
    this.scene = scene;
    this.fx = new Particles(scene);
    this.trail = new Trail(scene);
    this.stats.part = part;
    await mocapReady;
    const area = new AreaClass(this, areaState || {});
    this.area = area;
    area.fx = this.fx;
    await area.build(scene);
    // the hero
    if (!this.heroActor) this.heroActor = knight();
    Object.assign(this.heroActor.state, { dead: 0, attack: -1, hit: 0, speed: 0 });
    scene.add(this.heroActor.root);
    this.player = new Player(this.heroActor, this.stats);
    this.player.onStrike = (combo) => this.playerStrike(combo);
    area.start(from);
    this.player.place(area);
    this.cam.yaw = this.player.yaw + Math.PI; this.cam.pitch = 0.15;
    this.cam.setMode(settings.view, this.player);
    // allies follow you between areas
    for (let i = 0; i < this.stats.warriors; i++) this.addAlly();
    setScene(scene);
    followSun(this.player.pos);
    ambience(area.ambience);
    music(area.music || null);
    this.snapshot = JSON.stringify({ p: this.stats, area: area.state, part });
    this.save();
    this.paused = false;
  }

  // ---------- the frame ----------
  update(dtReal) {
    if (this.paused || !this.area) return;
    let dt = dtReal;
    if (hitstop.t > 0) { hitstop.t -= dt; dt *= 0.08; }
    this.time += dt;
    const p = this.stats, pl = this.player, area = this.area;
    const modal = this.hud.modalOpen || this.busy;
    if (!modal) {
      if (pressed('view')) { settings.view = settings.view === 'first' ? 'third' : 'first'; this.cam.setMode(settings.view, pl); this.hud.toast(settings.view === 'first' ? 'First person' : 'Third person'); }
      if (pressed('inventory')) this.openInventory();
      if (pressed('interact') && this.nearest) this.run(() => this.nearest.action());
      if (pressed('potion') && p.goods.LONGBOW) this.shootArrow();
      ['BLAST', 'FLASH', 'STUN', 'UNLOCK', 'VISION', 'PROTECT'].forEach((n, i) => { if (pressed('spell' + (i + 1))) this.run(() => this.castSpell(n)); });
      if (pressed('use')) this.openInventory();
    }
    pl.frozen = modal || p.stuck > 0;
    pl.update(dt, area, this.cam.yaw, this.cam.mode === 'first');
    // trapped: wiggle free by pressing jump / attack
    if (p.stuck > 0 && !modal && (pressed('jump') || pressed('attack'))) { p.stuck--; sfx('chain'); this.hud.say(p.stuck > 0 ? ['>>TRAP<<', 'STILL STUCK'] : ['YOU ESCAPE']); }
    for (const m of this.monsters) m.update(dt, pl, area, this.allies);
    this.monsters = this.monsters.filter(m => !m.removed);
    for (const a of this.allies) a.update(dt, this);
    for (const k of this.pickups) {
      k.update(dt);
      if (!k.taken && pl.pos.distanceTo(k.group.position) < 1.3) this.take(k);
    }
    this.pickups = this.pickups.filter(k => !k.taken);
    area.update(dt);
    // interaction prompt
    this.nearest = null;
    let best = 1e9;
    for (const it of area.interactables) {
      if (it.off) continue;
      const d = Math.hypot(it.pos.x - pl.pos.x, it.pos.z - pl.pos.z);
      if (d < (it.radius || 2.5) && d < best) { best = d; this.nearest = it; }
    }
    this.hud.prompt(!modal && this.nearest && !pl.dead ? this.nearest.label : null);
    // strength creeps back: one point every 256 frames, as on the 2600 ($FCE2)
    const fighting = this.monsters.some(m => m.alive && m.aggro && m.pos.distanceTo(pl.pos) < 20);
    this.regenT += dt;
    if (this.regenT > 256 / 60) {
      this.regenT = 0;
      if (p.fever) { p.str -= 1; floatText(pl.pos.clone().setY(pl.pos.y + 2.2), 'FEVER', 'bad'); this.checkDeath(); }
      else if (!fighting && p.str < p.maxStr && p.str >= 0) p.str++;
    }
    // sword trail
    if (this.heroActor.weapon) {
      const base = this.heroActor.R.ha.getWorldPosition(new THREE.Vector3()), tip = this.heroActor.tipWorld();
      this.trail.update(pl.attacking && pl.attackT > 0.3 && pl.attackT < 0.7, base, tip);
    }
    this.fx.update(dt);
    windUniform.value += dt;
    this.cam.update(dtReal, pl, area, null);
    followSun(pl.pos);
    updateFloats(dtReal, gfx.camera);
    this.hud.stats(p, pl, { heading: this.cam.yaw, crosshair: this.cam.mode === 'first' });
    this.hud.update(dtReal, gfx.camera, this.monsters);
  }

  // run an async interaction with the world paused for input
  async run(fn) {
    if (this.busy) return;
    this.busy = true;
    unlockPointer();
    try { await fn(); } catch (e) { console.error(e); }
    this.busy = false;
    if (!this.player.dead && !this.ended) lockPointer(gfx.renderer.domElement);
    this.save();
  }

  // ---------- combat ----------
  playerStrike(combo) {
    const pl = this.player, p = this.stats;
    const fwd = pl.forward();
    const reach = combo === 2 ? 2.9 : 2.4, arc = combo === 2 ? 0.55 : 1.1;
    p.bonus = p.inv.HANDAXE > 0 ? 2 : 0;
    let any = false;
    const targets = [...this.monsters.filter(m => m.alive), ...(this.area.bossTargets?.() || [])];
    for (const m of targets) {
      const to = new THREE.Vector3().subVectors(m.pos, pl.pos); to.y = 0;
      const d = to.length() - (m.radius || 0.5);
      if (d > reach || fwd.angleTo(to) > arc) continue;
      any = true;
      if (m.onHitByPlayer) { m.onHitByPlayer(combo); continue; }
      // the ROM's roll ($FC6A): contest(monster attack, your DEX), damage randBelow(weapon) + bonus
      const r = R.playerAttack(p, m.stats);
      if (!r.hit) { floatText(m.pos.clone().setY(m.pos.y + m.type.height * 0.8), 'MISS', 'miss'); sfx('miss'); continue; }
      const dmg = r.dmg + (combo === 2 && !r.killed ? 0 : 0);
      m.takeHit(dmg, pl.pos, combo);
      if (r.killed) this.kill(m);
    }
    if (!any) sfx('miss', { vol: 0.4 });
  }
  kill(m) {
    m.die();
    this.hud.say(['YOU KILLED', 'THE ' + m.name]);
    const l = R.loot(m.stats);
    const at = m.pos.clone();
    if (l.gold) this.drop('GOLD', at.clone().add(new THREE.Vector3(0.5, 0, 0.3)), l.gold);
    if (l.item) this.drop(l.item, at.clone().add(new THREE.Vector3(-0.4, 0, 0.4)));
    this.area.onKill?.(m);
  }
  drop(name, pos, amount = 1) {
    pos.y = this.area.groundAt(pos.x, pos.z);
    this.pickups.push(new Pickup(this.scene, name, pos, amount));
  }
  take(k) {
    k.remove();
    const p = this.stats;
    if (k.name === 'GOLD') { p.gold += k.amount; sfx('coin'); this.hud.say(['YOU FIND', k.amount + ' GOLD']); floatText(k.group.position.clone().setY(k.group.position.y + 1), '+' + k.amount + ' gold', 'gold'); }
    else { p.inv[k.name] = (p.inv[k.name] || 0) + 1; sfx('item'); this.hud.say(['YOU FIND', /^[AEIOU]/.test(k.name) ? 'AN ' + k.name : 'A ' + k.name]); this.hud.toast(k.name + ' added to your pack', 'item'); }
    this.fx.burst(k.group.position.clone().setY(k.group.position.y + 0.4), k.name === 'GOLD' ? 'gold' : 'magic', 16);
  }
  playerHurt(dmg, from) {
    const pl = this.player;
    pl.hurt(dmg);
    sfx('hurt'); shake.add(0.35 + dmg * 0.05); hitstop.add(0.06);
    this.fx.burst(pl.pos.clone().setY(pl.pos.y + 1.3), 'blood', 10 + dmg * 3);
    floatText(pl.pos.clone().setY(pl.pos.y + 2.1), '-' + dmg, 'hurt');
    document.getElementById('vignette').classList.add('hit');
    setTimeout(() => document.getElementById('vignette').classList.remove('hit'), 220);
    this.checkDeath();
  }
  checkDeath() {
    if (this.stats.str < 0 && !this.player.dead) {
      this.player.dead = true;
      sfx('death');
      this.hud.say(['YOU ARE DEAD'], 4);
      setTimeout(() => this.onDeath?.(), 2600);
    }
  }

  // ---------- spawning ----------
  spawn(names, at, { emerge = true, aggro = true } = {}) {
    const list = names.map((n, i) => {
      const off = new THREE.Vector3((i - (names.length - 1) / 2) * 2.2, 0, (i % 2) * 1.2);
      const pos = at.clone().add(off); pos.y = this.area.groundAt(pos.x, pos.z);
      const m = new Monster(n, this, pos);
      m.emerge = emerge ? 0 : 1; m.aggro = aggro;
      if (this.player) m.yaw = Math.atan2(this.player.pos.x - pos.x, this.player.pos.z - pos.z);
      this.monsters.push(m);
      return m;
    });
    return list;
  }
  get fighting() { return this.monsters.some(m => m.alive && m.aggro); }

  // ---------- allies (enlisted warriors) ----------
  addAlly() {
    const a = new Ally(this, this.allies.length);
    this.allies.push(a);
    return a;
  }
  allyHit(a, m) {
    if (R.contest(14, m.stats.atk)) {
      const d = R.randBelow(m.stats.dmg);
      if (!d) return;
      a.hp -= d; a.actor.state.hit = 1;
      this.fx.burst(a.pos.clone().setY(a.pos.y + 1.3), 'blood', 8);
      sfx('clang', { vol: 0.4 });
      if (a.hp <= 0) { a.die(); this.hud.say(['A WARRIOR', 'WAS KILLED']); }
    }
  }

  // ---------- items and spells ----------
  async openInventory() {
    if (this.busy) return;
    this.busy = true; unlockPointer();
    const v = await inventoryPanel(this);
    this.busy = false;
    if (v) await this.useItem(v[0], v[1]);
    if (!this.player.dead) lockPointer(gfx.renderer.domElement);
  }
  async useItem(kind, name) {
    const p = this.stats, pl = this.player;
    if (kind === 'spell') return this.castSpell(name);
    if (kind === 'goods') {
      if (name === 'MEDICINE') { p.goods[name]--; p.fever = 0; p.str = Math.min(p.maxStr, p.str + 5); sfx('heal'); this.hud.say(['THE FEVER', 'IS GONE']); this.fx.burst(pl.pos.clone().setY(pl.pos.y + 1), 'heal', 30); }
      else if (name === 'ELIXIR') { p.goods[name]--; p.str = p.maxStr; sfx('heal'); this.hud.say(['YOU FEEL', 'STRONG AGAIN']); this.fx.burst(pl.pos.clone().setY(pl.pos.y + 1), 'heal', 40); }
      else if (name === 'VITAMINS') { p.goods[name]--; p.maxStr = Math.min(60, p.maxStr + 3); p.str += 3; sfx('heal'); this.hud.say(['MAX STRENGTH', 'UP 3']); }
      else this.hud.toast(name + ': always in use while you carry it');
      return;
    }
    if (R.MAGIC.includes(name)) {
      const before = { str: p.str, dex: p.dex, max: p.maxStr };
      const effect = p.magic[name];
      R.useMagic(p, name);
      sfx('magic'); this.fx.burst(pl.pos.clone().setY(pl.pos.y + 1.2), 'magic', 40);
      // the 2600 only ever said this:
      this.hud.say(['THE ' + name, 'FEELS WEIRD'], 3.5);
      p.known = p.known || {}; p.known[name] = MAGIC_FX[effect];
      const at = pl.pos.clone().setY(pl.pos.y + 2.3);
      if (p.str !== before.str) floatText(at, (p.str > before.str ? '+' : '') + (p.str - before.str) + ' STR', p.str > before.str ? 'good' : 'bad');
      if (p.dex !== before.dex) floatText(at.clone().setY(at.y + 0.3), (p.dex > before.dex ? '+' : '') + (p.dex - before.dex) + ' DEX', p.dex > before.dex ? 'good' : 'bad');
      if (effect === 0) floatText(at, 'PROTECTED', 'good');
      this.checkDeath();
      return;
    }
    switch (name) {
      case 'KEY': case 'CHEST':
        if (p.inv.KEY > 0 && p.inv.CHEST > 0) {
          const item = R.openChest(p);
          sfx('door'); sfx('coin');
          this.hud.say(['YOU OPEN THE CHEST', '75 GOLD' + (item ? ' AND A ' + item : '')], 4);
          return;
        }
        if (this.area.onKey && name === 'KEY' && await this.area.onKey()) return;
        this.hud.say(name === 'CHEST' ? ['THE CHEST', 'IS LOCKED'] : ['NOTHING HERE', 'IS LOCKED']);
        return;
      case 'PAPER':
        if (this.area.onPaper && await this.area.onPaper()) return;
        this.hud.say(['IT IS AN ID', 'SHOW IT TO THE GUARD']);
        return;
      case 'HANDAXE': this.hud.toast('The handaxe adds 2 to every blow while you carry it'); return;
      case 'SHIELD': this.hud.toast('The shield takes 1 off every blow while you carry it'); return;
    }
  }
  nearestMonster(range = 18) {
    let best = null, bd = range;
    for (const m of this.monsters) if (m.alive) { const d = m.pos.distanceTo(this.player.pos); if (d < bd) { bd = d; best = m; } }
    return best;
  }
  async castSpell(name) {
    const p = this.stats, pl = this.player;
    if (!p.spells[name]) { this.hud.toast('You have no ' + name + ' spell. The Magic Shop sells them.'); sfx('bad'); return; }
    if (this.area.onSpell && await this.area.onSpell(name)) return;
    if (name === 'PROTECT') { p.spells[name]--; p.protect = 3; sfx('magic'); this.fx.burst(pl.pos.clone().setY(pl.pos.y + 1), 'magic', 40); this.hud.say(['YOU ARE', 'PROTECTED']); return; }
    if (name === 'UNLOCK' || name === 'VISION') { this.hud.say(['NO EFFECT', 'HERE']); sfx('bad'); return; }
    const m = this.nearestMonster();
    if (!m) { this.hud.say(['NOBODY HERE']); sfx('bad'); return; }
    p.spells[name]--;
    sfx('magic');
    const at = m.pos.clone().setY(m.pos.y + m.type.height * 0.6);
    if (name === 'BLAST') {
      const d = R.randBelow(12) + 6;
      this.fx.burst(at, 'fire', 40); this.fx.burst(at, 'ember', 30); sfx('boom'); shake.add(0.5);
      if (d > m.stats.hpLeft) { m.stats.hpLeft = -1; m.takeHit(d, pl.pos); this.kill(m); } else { m.stats.hpLeft -= d; m.takeHit(d, pl.pos); }
      this.hud.say(['BLAST', 'FOR ' + R.pad(d) + ' UNITS']);
    } else if (name === 'FLASH') {
      for (const o of this.monsters) if (o.alive && o.pos.distanceTo(pl.pos) < 14) { o.stats.blind = 2; this.fx.burst(o.pos.clone().setY(o.pos.y + 1.5), 'magic', 20); }
      this.hud.say(['A BLINDING FLASH']);
    } else if (name === 'STUN') {
      m.stagger = 5; m.attackT = -1;
      this.fx.burst(at, 'magic', 30);
      this.hud.say(['THE ' + m.name, 'IS STUNNED']);
    }
  }
  shootArrow() {
    const pl = this.player;
    if (this._bowCool > this.time) return;
    this._bowCool = this.time + 0.8;
    sfx('arrow');
    const dir = new THREE.Vector3(-Math.sin(this.cam.yaw), -Math.sin(this.cam.pitch) * 0.5, -Math.cos(this.cam.yaw)).normalize();
    const from = pl.pos.clone().setY(pl.pos.y + 1.5);
    this.area.arrow?.(from, dir);
    if (this.area.arrow) return;
    // ordinary monsters: hit the first one along the aim
    for (const m of this.monsters) {
      if (!m.alive) continue;
      const to = m.pos.clone().setY(m.pos.y + 1).sub(from);
      if (to.length() < 30 && dir.angleTo(to) < 0.12) {
        if (R.contest(m.stats.atk, this.stats.dex)) { const d = R.randBelow(3) + 1; if (d > m.stats.hpLeft) { m.stats.hpLeft = -1; m.takeHit(d, pl.pos); this.kill(m); } else { m.stats.hpLeft -= d; m.takeHit(d, pl.pos); } }
        else floatText(m.pos.clone().setY(m.pos.y + 2), 'MISS', 'miss');
        return;
      }
    }
  }

  // ---------- saving ----------
  save() {
    if (!this.area || this.player?.dead || this.ended) return;
    try { localStorage.setItem(SAVE_KEY, JSON.stringify({ v: 2, p: this.stats, part: this.stats.part, area: this.area.state, pos: this.player ? [this.player.pos.x, this.player.pos.z] : null })); } catch (e) {}
  }
  static loadSave() { try { const s = JSON.parse(localStorage.getItem(SAVE_KEY)); return s && s.v === 2 ? s : null; } catch (e) { return null; } }
  static wipeSave() { try { localStorage.removeItem(SAVE_KEY); } catch (e) {} }
}

// An enlisted warrior: follows you, fights what you fight
class Ally {
  constructor(game, i) {
    this.game = game; this.i = i; this.isAlly = true; this.alive = true; this.hp = 15;
    this.actor = knight({ steelTint: 0x8a8a90, cloth: '#1e4a24', emblem: false, cape: null, plume: 0x1e6a2a, helm: 'kettle', shield: true });
    this.pos = game.player.pos.clone().add(new THREE.Vector3((i - 1) * 2, 0, 2.5));
    this.yaw = game.player.yaw; this.cool = 1; this.attackT = -1; this.radius = 0.45;
    game.scene.add(this.actor.root);
  }
  die() { this.alive = false; this.deadT = 0; this.game.stats.warriors = Math.max(0, this.game.stats.warriors - 1); }
  update(dt, game) {
    const s = this.actor.state, pl = game.player, area = game.area;
    if (!this.alive) { this.deadT += dt; s.dead = Math.min(1, this.deadT * 1.5); this.actor.update(dt); this.actor.root.position.copy(this.pos); return; }
    s.hit = Math.max(0, s.hit - dt * 3);
    // pick the nearest awake monster near the hero
    let foe = null, fd = 14;
    for (const m of game.monsters) if (m.alive && m.aggro) { const d = m.pos.distanceTo(this.pos); if (d < fd) { fd = d; foe = m; } }
    const bossT = area.bossTargets?.()[0];
    if (!foe && bossT && bossT.pos.distanceTo(this.pos) < 30) { foe = bossT; fd = foe.pos.distanceTo(this.pos); }
    let want;
    if (foe) want = foe.pos.clone().add(new THREE.Vector3(Math.cos(this.i * 2.1) * (foe.radius + 1.4), 0, Math.sin(this.i * 2.1) * (foe.radius + 1.4)));
    else { const a = pl.yaw + Math.PI + (this.i - 1) * 0.9; want = pl.pos.clone().add(new THREE.Vector3(Math.sin(a) * 3, 0, Math.cos(a) * 3)); }
    const to = want.clone().sub(this.pos); to.y = 0;
    const d = to.length();
    let speed = 0;
    if (d > 0.6) { speed = Math.min(d * 2, foe ? 4.5 : (d > 6 ? 6.5 : 3.4)); to.normalize(); }
    let nx = this.pos.x + to.x * speed * dt, nz = this.pos.z + to.z * speed * dt;
    [nx, nz] = resolve(nx, nz, this.radius, area.colliders);
    this.pos.x = nx; this.pos.z = nz;
    if (area.clamp) [this.pos.x, this.pos.z] = area.clamp(this.pos.x, this.pos.z, this);
    this.pos.y = area.groundAt(this.pos.x, this.pos.z);
    const look = foe ? foe.pos : (speed > 0.1 ? this.pos.clone().add(to) : pl.pos);
    const wantYaw = Math.atan2(look.x - this.pos.x, look.z - this.pos.z);
    let dy = wantYaw - this.yaw; dy = Math.atan2(Math.sin(dy), Math.cos(dy)); this.yaw += dy * Math.min(1, dt * 8);
    // swing at the foe
    this.cool -= dt;
    if (foe && fd < (foe.radius || 0.5) + 2.2 && this.cool <= 0 && this.attackT < 0) { this.attackT = 0; this.struck = false; }
    if (this.attackT >= 0) {
      this.attackT += dt / 0.7;
      if (this.attackT > 0.42 && !this.struck && foe) {
        this.struck = true;
        if (foe.onHitByAlly) foe.onHitByAlly(this);
        else if (foe.alive && R.contest(foe.stats.atk, 14)) {
          const dmg = R.randBelow(4) + 1;
          if (dmg > foe.stats.hpLeft) { foe.stats.hpLeft = -1; foe.takeHit(dmg, this.pos); game.kill(foe); }
          else { foe.stats.hpLeft -= dmg; foe.takeHit(dmg, this.pos); }
        }
      }
      if (this.attackT >= 1) { this.attackT = -1; this.cool = 1.2 + Math.random(); }
    }
    s.speed = speed; s.attack = this.attackT; s.attackKind = 'slash'; s.swingSide = this.i % 2 ? 1 : -1;
    this.actor.update(dt);
    this.actor.root.position.copy(this.pos); this.actor.root.rotation.y = this.yaw;
  }
}
