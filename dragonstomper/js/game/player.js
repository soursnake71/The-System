// The hero: movement, stamina, jumping, dodging, sword combos and blocking.
import * as THREE from 'three';
import { input, pressed, held } from '../core/input.js';
import { resolve } from './physics.js';
import { sfx } from '../core/sound.js';

export class Player {
  constructor(actor, stats) {
    this.actor = actor; this.p = stats;
    this.pos = new THREE.Vector3(); this.vel = new THREE.Vector3();
    this.yaw = Math.PI; this.radius = 0.42;
    this.onGround = true; this.vy = 0;
    this.stamina = 100; this.staminaDelay = 0;
    this.attackT = -1; this.combo = 0; this.queued = false; this.struck = false;
    this.dodgeT = -1; this.dodgeDir = new THREE.Vector3();
    this.iframes = 0; this.hitT = 0; this.frozen = false; this.dead = false; this.deadT = 0;
    this.stepT = 0;
    this.onStrike = null; // set by the game: called at the moment a swing lands
  }
  get attacking() { return this.attackT >= 0; }
  get blocking() { return this.actor.state.block > 0.6 && !this.attacking && this.dodgeT < 0; }

  update(dt, area, camYaw, firstPerson) {
    const a = this.actor, s = a.state, p = this.p;
    if (this.dead) { this.deadT += dt; s.dead = Math.min(1, this.deadT * 1.3); s.speed = 0; a.update(dt); this.place(area); return; }
    this.hitT = Math.max(0, this.hitT - dt * 3); s.hit = this.hitT;
    this.iframes = Math.max(0, this.iframes - dt);
    // stamina
    this.staminaDelay -= dt;
    if (this.staminaDelay <= 0) this.stamina = Math.min(100, this.stamina + 28 * dt);
    const canAct = !this.frozen;
    // movement intent relative to the camera
    let mx = canAct ? input.moveX : 0, my = canAct ? input.moveY : 0;
    const fwd = new THREE.Vector3(-Math.sin(camYaw), 0, -Math.cos(camYaw)), right = new THREE.Vector3(-fwd.z, 0, fwd.x);
    const want = new THREE.Vector3().addScaledVector(fwd, my).addScaledVector(right, mx);
    const mag = Math.min(1, want.length());
    if (mag > 0.01) want.normalize();
    let sprinting = canAct && held('sprint') && mag > 0.3 && this.stamina > 1 && !this.attacking && !this.blocking;
    if (sprinting) { this.stamina -= 16 * dt; this.staminaDelay = 0.6; }
    let speed = mag * (sprinting ? 6.8 : 3.4);
    if (this.blocking) speed *= 0.45;
    if (this.attacking) speed *= 0.25;
    // dodge roll
    if (canAct && pressed('dodge') && this.dodgeT < 0 && this.stamina > 20 && this.onGround) {
      this.dodgeT = 0; this.iframes = 0.35; this.stamina -= 22; this.staminaDelay = 0.8;
      this.dodgeDir.copy(mag > 0.1 ? want : fwd.clone().negate());
      this.attackT = -1; sfx('dodge');
    }
    const targetVel = want.clone().multiplyScalar(speed);
    if (this.dodgeT >= 0) {
      this.dodgeT += dt;
      targetVel.copy(this.dodgeDir).multiplyScalar(this.dodgeT < 0.35 ? 8.5 : 2);
      if (this.dodgeT > 0.5) this.dodgeT = -1;
    }
    const accel = this.onGround ? 14 : 3;
    this.vel.x += (targetVel.x - this.vel.x) * Math.min(1, accel * dt);
    this.vel.z += (targetVel.z - this.vel.z) * Math.min(1, accel * dt);
    // facing: toward movement, or the camera direction when fighting or in first person
    const fighting = this.attacking || this.blocking || firstPerson;
    let face = this.yaw;
    if (fighting) face = camYaw + Math.PI;
    else if (this.dodgeT >= 0) face = Math.atan2(this.dodgeDir.x, this.dodgeDir.z);
    else if (mag > 0.1) face = Math.atan2(want.x, want.z);
    let d = face - this.yaw; d = Math.atan2(Math.sin(d), Math.cos(d));
    this.yaw += d * Math.min(1, dt * (fighting ? 18 : 10));
    // jump and gravity
    if (canAct && pressed('jump') && this.onGround && this.stamina > 10) { this.vy = 5.2; this.onGround = false; this.stamina -= 10; this.staminaDelay = 0.5; sfx('jump'); }
    this.vy -= 18 * dt;
    let nx = this.pos.x + this.vel.x * dt, nz = this.pos.z + this.vel.z * dt;
    [nx, nz] = resolve(nx, nz, this.radius, area.colliders);
    if (area.clamp) [nx, nz] = area.clamp(nx, nz, this);
    this.pos.x = nx; this.pos.z = nz;
    const gy = area.groundAt(nx, nz);
    this.pos.y += this.vy * dt;
    if (this.pos.y <= gy) {
      if (!this.onGround && this.vy < -6) { sfx('land'); area.fx?.burst(this.pos, 'dust', 6); }
      this.pos.y = gy; this.vy = 0; this.onGround = true;
    } else if (this.pos.y - gy > 0.3) this.onGround = false;
    else if (this.vy <= 0) { this.pos.y = gy; this.vy = 0; this.onGround = true; }
    // attacks: three-hit combo, each swing commits for a moment
    const swingLen = 0.6;
    if (canAct && pressed('attack') && this.dodgeT < 0) {
      if (!this.attacking && this.stamina > 5) this.startSwing();
      else if (this.attacking && this.attackT > 0.35) this.queued = true;
    }
    if (this.attacking) {
      const prev = this.attackT;
      this.attackT += dt / swingLen;
      if (prev < 0.42 && this.attackT >= 0.42 && !this.struck) { this.struck = true; this.onStrike?.(this.combo); }
      if (this.attackT >= 1) {
        if (this.queued && this.stamina > 5 && this.combo < 2) { this.combo++; this.startSwing(true); }
        else { this.attackT = -1; this.combo = 0; }
      }
    }
    s.attack = this.attackT; s.attackKind = this.combo === 2 ? 'thrust' : 'slash'; s.swingSide = this.combo % 2 ? -1 : 1;
    // block with the shield
    const wantBlock = canAct && held('block') && this.dodgeT < 0 && !this.attacking;
    s.block = THREE.MathUtils.clamp(s.block + (wantBlock ? dt * 7 : -dt * 5), 0, 1);
    // footsteps
    const hs = Math.hypot(this.vel.x, this.vel.z);
    s.speed = this.dodgeT >= 0 ? 4 : hs;
    if (this.onGround && hs > 0.5) { this.stepT += dt * hs; if (this.stepT > 1.15) { this.stepT = 0; sfx(area.floor === 'stone' ? 'stepStone' : area.floor === 'cave' ? 'stepCave' : 'step'); } }
    a.update(dt);
    // roll: tip the whole body forward through the dodge
    a.body.rotation.x = this.dodgeT >= 0 ? Math.sin(Math.min(1, this.dodgeT / 0.45) * Math.PI) * 1.2 : a.body.rotation.x;
    a.body.position.y = this.dodgeT >= 0 ? -Math.sin(Math.min(1, this.dodgeT / 0.45) * Math.PI) * 0.5 : a.body.position.y;
    this.place(area);
  }
  startSwing(chained = false) {
    this.attackT = 0; this.struck = false; this.queued = false;
    if (!chained) this.combo = 0;
    this.stamina -= 9; this.staminaDelay = 0.7;
    sfx('swing');
  }
  place() {
    this.actor.root.position.copy(this.pos);
    this.actor.root.rotation.y = this.yaw;
  }
  forward() { return new THREE.Vector3(Math.sin(this.yaw), 0, Math.cos(this.yaw)); }
  hurt(dmg) {
    this.hitT = 1; this.iframes = 0.4;
  }
}
