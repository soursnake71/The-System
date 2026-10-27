// Third-person over-the-shoulder camera with collision, and a first-person mode.
import * as THREE from 'three';
import { input } from '../core/input.js';
import { segmentHit } from './physics.js';
import { shake } from './fx.js';

export class CameraRig {
  constructor(camera) {
    this.cam = camera;
    this.yaw = 0; this.pitch = 0.18; this.dist = 4.2; this.wantDist = 4.2;
    this.mode = 'third';
    this.pos = new THREE.Vector3(); this.look = new THREE.Vector3();
    this.bob = 0;
    this.extraDist = 0; // areas pull the camera back for big fights
  }
  setMode(m, player) {
    this.mode = m;
    player.actor.setFirstPerson(m === 'first');
  }
  update(dt, player, area, focus = null) {
    if (!player) return;
    this.yaw -= input.lookX; this.pitch += input.lookY;
    this.pitch = THREE.MathUtils.clamp(this.pitch, this.mode === 'first' ? -1.4 : -0.5, this.mode === 'first' ? 1.4 : 1.2);
    if (input.wheel) this.wantDist = THREE.MathUtils.clamp(this.wantDist + input.wheel * 0.6, 2.2, 9);
    const p = player.pos;
    // f: where the camera looks (pitch > 0 looks down)
    const f = new THREE.Vector3(-Math.sin(this.yaw) * Math.cos(this.pitch), -Math.sin(this.pitch), -Math.cos(this.yaw) * Math.cos(this.pitch));
    if (this.mode === 'first') {
      const head = player.actor.head.getWorldPosition(new THREE.Vector3());
      const hs = Math.hypot(player.vel.x, player.vel.z);
      this.bob += dt * hs * 2.2;
      head.y += 0.1 + Math.sin(this.bob * 2) * 0.03 * Math.min(1, hs / 3);
      head.addScaledVector(new THREE.Vector3(Math.sin(player.yaw), 0, Math.cos(player.yaw)), 0.14);
      this.cam.position.copy(head);
      this.cam.lookAt(head.clone().add(f));
    } else {
      const scale = player.actor.root.scale.y;
      const pivot = new THREE.Vector3(p.x, p.y + 1.62 * scale, p.z);
      const right = new THREE.Vector3(Math.cos(this.yaw), 0, -Math.sin(this.yaw));
      pivot.addScaledVector(right, 0.55);
      if (focus) pivot.lerp(focus, 0.25);
      const wd = this.wantDist + this.extraDist;
      const want = pivot.clone().addScaledVector(f, -wd);
      const hit = segmentHit(pivot.x, pivot.z, want.x, want.z, area.colliders, 0.3);
      let d = wd * Math.max(0.25, hit);
      for (let i = 1; i <= 8; i++) {
        const t = i / 8, q = pivot.clone().addScaledVector(f, -d * t);
        if (q.y < area.groundAt(q.x, q.z) + 0.35) { d = Math.max(0.8, d * (t - 0.1)); break; }
      }
      if (area.ceiling) { const q = pivot.clone().addScaledVector(f, -d); if (q.y > area.ceiling(q.x, q.z) - 0.4) d *= 0.6; }
      this.dist += (d - this.dist) * Math.min(1, dt * (d < this.dist ? 18 : 4));
      this.cam.position.copy(pivot).addScaledVector(f, -this.dist);
      this.cam.lookAt(pivot.clone().addScaledVector(f, 4));
    }
    if (shake.amt > 0) {
      const a = shake.amt * shake.amt * 0.12;
      this.cam.position.x += (Math.random() - 0.5) * a; this.cam.position.y += (Math.random() - 0.5) * a;
      this.cam.rotation.z += (Math.random() - 0.5) * a * 0.3;
      shake.amt = Math.max(0, shake.amt - dt * 2.5);
    }
  }
  // the horizontal facing the camera looks along
  get facing() { return this.yaw; }
}
