// Monsters: the ROM's stats driving real-time AI. Every hit is still decided by the original
// dice: contest() for who connects and randBelow() for damage (see rules.js).
import * as THREE from 'three';
import * as H from '../actors/humanoid.js';
import * as B from '../actors/beasts.js';
import * as R from '../rules.js';
import { resolve } from './physics.js';
import { sfx } from '../core/sound.js';
import { floatText, shake, hitstop } from './fx.js';

// per-type behaviour; the numbers that matter for hits come from the ROM
const TYPES = {
  DEMON: { make: H.demon, reach: 1.9, speed: 4.2, poise: 0, blood: 'blood', voice: 'growl', pitch: 1.2, height: 2.2 },
  BUG: { make: B.bug, reach: 2.1, speed: 4.4, poise: 0, blood: 'ichor', voice: 'hiss', height: 1.2 },
  WARRIOR: { make: H.warrior, reach: 2.1, speed: 3.6, poise: 0.3, blood: 'blood', voice: 'growl', pitch: 1.5, metal: true, height: 2.0 },
  SNAKE: { make: B.snake, reach: 2.5, speed: 3.4, poise: 0, blood: 'blood', voice: 'hiss', height: 1.0 },
  SPIDER: { make: B.spider, reach: 2.0, speed: 4.8, poise: 0, blood: 'ichor', voice: 'shriek', pitch: 1.4, height: 1.2 },
  GHOUL: { make: H.ghoul, reach: 1.9, speed: 3.9, poise: 0, blood: 'blood', voice: 'shriek', pitch: 0.8, height: 1.9 },
  MANIAC: { make: H.maniac, reach: 2.0, speed: 4.1, poise: 0, blood: 'blood', voice: 'growl', pitch: 1.7, height: 1.9 },
  SLIME: { make: B.slime, reach: 1.9, speed: 2.2, poise: 0.5, blood: 'ichor', voice: 'hiss', height: 1.3 },
  BEETLE: { make: B.beetle, reach: 2.2, speed: 3.2, poise: 0.4, blood: 'ichor', voice: 'hiss', shell: true, height: 1.2 },
  GOLEM: { make: H.golem, reach: 2.6, speed: 2.4, poise: 1, blood: 'dust', voice: 'growl', pitch: 0.6, stone: true, height: 2.6 },
  MONKEY: { make: H.monkey, reach: 1.5, speed: 5.2, poise: 0, blood: 'blood', voice: 'shriek', pitch: 1.8, height: 1.4 },
  SCORPION: { make: B.scorpion, reach: 2.8, speed: 3.6, poise: 0.3, blood: 'ichor', voice: 'hiss', shell: true, height: 1.3 },
  GUARD: { make: H.guard, reach: 3.1, speed: 3.2, poise: 1, blood: 'blood', voice: 'growl', pitch: 1.3, metal: true, height: 2.5 },
};

export class Monster {
  constructor(name, game, pos) {
    this.name = name; this.game = game;
    this.type = TYPES[name];
    this.stats = R.spawnMonster(name);
    this.actor = this.type.make();
    this.pos = pos.clone(); this.yaw = Math.random() * 6.28;
    this.state = 'idle'; this.t = 0; this.cool = 1 + Math.random(); this.attackT = -1; this.struck = false;
    this.stagger = 0; this.deadT = 0; this.alive = true; this.aggro = false;
    this.radius = name === 'GOLEM' || name === 'GUARD' ? 0.7 : ['SPIDER', 'SCORPION', 'BEETLE', 'SNAKE'].includes(name) ? 0.8 : 0.45;
    this.emerge = 0;
    this.hpShown = 0;
    game.scene.add(this.actor.root);
    this.place();
  }
  get dead() { return !this.alive; }
  place() {
    this.actor.root.position.copy(this.pos);
    if (this.emerge < 1) this.actor.root.position.y -= (1 - this.emerge) * 2.2;
    this.actor.root.rotation.y = this.yaw;
  }
  update(dt, player, area, allies = []) {
    const s = this.actor.state, ty = this.type;
    this.t += dt;
    this.emerge = Math.min(1, this.emerge + dt * 2.2);
    s.hit = Math.max(0, s.hit - dt * 3);
    if (!this.alive) {
      this.deadT += dt; s.dead = Math.min(1, this.deadT * 1.6); s.speed = 0; s.attack = -1;
      this.actor.update(dt);
      if (this.deadT > 6) { this.actor.root.position.y -= dt * 0.3; if (this.deadT > 10) this.remove(); }
      return;
    }
    // a passive monster (the bridge guard) stands its ground until provoked
    if (this.passive) {
      s.speed = 0; s.attack = -1;
      this.pos.y = area.groundAt(this.pos.x, this.pos.z);
      this.actor.update(dt); this.place();
      return;
    }
    const target = this.pickTarget(player, allies);
    const to = new THREE.Vector3().subVectors(target.pos, this.pos); to.y = 0;
    const dist = to.length();
    if (dist < 18 || this.aggro) this.aggro = true;
    // turn to face the target
    if (this.aggro) {
      const want = Math.atan2(to.x, to.z);
      let d = want - this.yaw; d = Math.atan2(Math.sin(d), Math.cos(d));
      const turn = this.attackT >= 0 && this.attackT > 0.35 ? 1.5 : 6;
      this.yaw += THREE.MathUtils.clamp(d, -turn * dt, turn * dt);
    }
    let speed = 0;
    if (this.stagger > 0) this.stagger -= dt;
    else if (this.attackT >= 0) {
      const prev = this.attackT;
      this.attackT += dt / 1.15;
      if (prev < 0.45 && this.attackT >= 0.45 && !this.struck) { this.struck = true; this.strike(target); }
      if (this.attackT >= 1) { this.attackT = -1; this.cool = 0.8 + Math.random() * 1.4 - (this.stats.atk > 17 ? 0.3 : 0); }
    } else if (this.aggro && this.emerge >= 1) {
      this.cool -= dt;
      if (dist > ty.reach * 0.85) speed = ty.speed * (dist > 6 ? 1 : 0.7);
      else if (this.cool <= 0 && !player.dead) { this.attackT = 0; this.struck = false; sfx(ty.voice, { pitch: ty.pitch, vol: 0.6 }); }
      // circle a little when waiting so groups spread out
      if (dist < ty.reach * 1.2 && this.cool > 0) speed = 0.8;
    }
    if (speed > 0) {
      const dir = new THREE.Vector3(Math.sin(this.yaw), 0, Math.cos(this.yaw));
      if (dist < ty.reach * 1.2 && this.cool > 0) dir.set(Math.cos(this.yaw), 0, -Math.sin(this.yaw)).multiplyScalar(Math.sin(this.t * 0.7 + this.pos.x) > 0 ? 1 : -1);
      let nx = this.pos.x + dir.x * speed * dt, nz = this.pos.z + dir.z * speed * dt;
      [nx, nz] = resolve(nx, nz, this.radius, area.colliders);
      // keep apart from each other and from the hero
      for (const o of this.game.monsters) if (o !== this && o.alive) { const dx = nx - o.pos.x, dz = nz - o.pos.z, dd = Math.hypot(dx, dz), m = this.radius + o.radius; if (dd < m && dd > 1e-4) { nx = o.pos.x + dx / dd * m; nz = o.pos.z + dz / dd * m; } }
      const dp = Math.hypot(nx - player.pos.x, nz - player.pos.z), mp = this.radius + player.radius;
      if (dp < mp) { nx = player.pos.x + (nx - player.pos.x) / dp * mp; nz = player.pos.z + (nz - player.pos.z) / dp * mp; }
      if (area.clamp) [nx, nz] = area.clamp(nx, nz, this);
      this.pos.x = nx; this.pos.z = nz;
    }
    this.pos.y = area.groundAt(this.pos.x, this.pos.z);
    s.speed = speed; s.attack = this.attackT; s.attackKind = this.actor.kind;
    this.actor.update(dt);
    this.place();
  }
  pickTarget(player, allies) {
    const living = allies.filter(a => a.alive);
    if (!living.length) return player;
    if (!this._tgt || Math.random() < 0.003) this._tgt = Math.random() < 0.55 ? player : living[Math.floor(Math.random() * living.length)];
    return this._tgt.alive === false ? player : this._tgt;
  }
  // the blow lands now: is the target in reach, in front, not dodging or blocking?
  strike(target) {
    const g = this.game, ty = this.type;
    const to = new THREE.Vector3().subVectors(target.pos, this.pos); to.y = 0;
    const dist = to.length(), fwd = new THREE.Vector3(Math.sin(this.yaw), 0, Math.cos(this.yaw));
    const ang = fwd.angleTo(to);
    if (dist > ty.reach + 0.5 || ang > 1.1) { sfx('miss', { vol: 0.5 }); return; }
    if (target.isAlly) return g.allyHit(target, this);
    const pl = g.player, p = g.stats;
    if (pl.iframes > 0) { floatText(pl.pos.clone().setY(pl.pos.y + 2.1), 'DODGED', 'miss'); return; }
    // blocking with the shield, facing the monster
    const pf = pl.forward();
    if (pl.blocking && pf.angleTo(to.clone().negate()) < 1.2) {
      const cost = 14 + this.stats.dmg * 3;
      pl.stamina -= cost; pl.staminaDelay = 0.9;
      sfx('block'); shake.add(0.25);
      g.fx.burst(pl.pos.clone().add(new THREE.Vector3(0, 1.3, 0)).addScaledVector(pf, 0.5), 'spark', 24);
      if (pl.stamina > 0) { floatText(pl.pos.clone().setY(pl.pos.y + 2.1), 'BLOCKED', 'miss'); return; }
      pl.stamina = 0; floatText(pl.pos.clone().setY(pl.pos.y + 2.1), 'GUARD BROKEN', 'bad');
    }
    // the ROM's roll ($FDB7)
    const r = R.monsterAttack(p, this.stats);
    if (!r.hit) {
      const txt = r.msg.join(' ').includes('NO EFFECT') ? 'NO EFFECT' : r.msg.join(' ').includes('CANNOT SEE') ? 'BLINDED' : 'MISSED';
      floatText(pl.pos.clone().setY(pl.pos.y + 2.1), txt, 'miss'); sfx('miss', { vol: 0.6 }); return;
    }
    g.playerHurt(r.dmg, this);
  }
  // you hit it
  takeHit(dmg, from, combo) {
    const s = this.actor.state, ty = this.type, g = this.game;
    this.aggro = true; this.passive = false;
    s.hit = 1;
    if (Math.random() > ty.poise) { this.stagger = 0.35; if (this.attackT >= 0 && this.attackT < 0.45) this.attackT = -1; }
    const at = this.pos.clone().setY(this.pos.y + ty.height * 0.6);
    g.fx.burst(at, ty.blood, 14 + dmg * 4, from.clone().sub(this.pos).normalize().negate(), 0.8);
    if (ty.metal) { sfx('clang', { vol: 0.7 }); g.fx.burst(at, 'spark', 14); }
    else if (ty.shell) sfx('hitShell');
    else if (ty.stone) { sfx('rock', { vol: 0.6 }); g.fx.burst(at, 'dust', 10); }
    else sfx('hitFlesh');
    floatText(at.clone().setY(at.y + 0.6), String(dmg), 'dmg');
    shake.add(0.15 + dmg * 0.03); hitstop.add(0.05 + dmg * 0.01);
    this.hpShown = 3;
    // knock back a touch
    const push = this.pos.clone().sub(from).setY(0).normalize().multiplyScalar(0.25 * (1 - ty.poise));
    this.pos.add(push);
  }
  die() {
    this.alive = false; this.attackT = -1;
    sfx(this.type.voice, { pitch: (this.type.pitch || 1) * 0.7 });
    this.game.stats.kills++;
  }
  remove() { this.actor.root.parent?.remove(this.actor.root); this.removed = true; }
}

export const MONSTER_TYPES = TYPES;
