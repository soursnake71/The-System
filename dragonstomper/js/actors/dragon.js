// The dragon: a sculpted, skinned body on a ~40-bone skeleton, with membranous wings,
// teeth, horns and back spikes, and code-driven animation. Faces +z; about 12 m nose to tail.
import * as THREE from 'three';
import { lite } from '../core/gfx.js';
import { mats, mesh, hornGeo } from './parts.js';
import { Sculpt, skinMaterial } from './sculpt.js';
import { heightTex, detailTex } from '../core/texgen.js';

const V = (x, y, z) => new THREE.Vector3(x, y, z);
const ease = (t) => t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2;

export class Dragon {
  constructor() {
    this.root = new THREE.Group();
    this.bones = [];
    const B = (parent, x, y, z, name) => { const b = new THREE.Bone(); b.name = name; b.position.set(x, y, z); parent.add(b); this.bones.push(b); return b; };
    const body = this.bodyBone = B(this.root, 0, 0, 0, 'body');
    this.pelvis = B(body, 0, 1.95, -0.6, 'pelvis');
    this.spine1 = B(this.pelvis, 0, 0.15, 1.1, 'spine1');
    this.chest = B(this.spine1, 0, 0.15, 1.2, 'chest');
    // neck rises up and forward
    this.neck = [];
    let p = this.chest;
    const nd = [[0, 0.5, 0.7], [0, 0.5, 0.5], [0, 0.45, 0.5], [0, 0.35, 0.5], [0, 0.22, 0.5], [0, 0.08, 0.48], [0, -0.05, 0.42]];
    nd.forEach((d, i) => { p = B(p, ...d, 'neck' + i); this.neck.push(p); });
    this.head = B(p, 0, 0, 0.35, 'head');
    this.jaw = B(this.head, 0, -0.12, 0.05, 'jaw');
    this.tail = [];
    p = this.pelvis;
    for (let i = 0; i < 10; i++) { p = B(p, 0, i ? -0.04 : -0.1, i ? -0.62 : -0.8, 'tail' + i); this.tail.push(p); }
    const legs = (parent, x, y, z, n, front) => {
      const sh = B(parent, x, y, z, n + 'sh'), up = B(sh, 0, 0, 0, n + 'up');
      const lo = B(up, 0, -0.8, front ? 0.15 : -0.3, n + 'lo'), ft = B(lo, 0, -0.75, front ? -0.08 : 0.2, n + 'ft');
      return { sh, up, lo, ft };
    };
    this.legs = {
      FL: legs(this.chest, 0.75, -0.35, 0.2, 'FL', true), FR: legs(this.chest, -0.75, -0.35, 0.2, 'FR', true),
      HL: legs(this.pelvis, 0.8, -0.25, -0.1, 'HL', false), HR: legs(this.pelvis, -0.8, -0.25, -0.1, 'HR', false),
    };
    // wings: humerus, forearm, then three long fingers
    this.wings = [];
    for (const s of [1, -1]) {
      const root = B(this.chest, s * 0.55, 0.75, -0.1, 'w' + s), hum = B(root, 0, 0, 0, 'wh' + s);
      const fore = B(hum, s * 1.6, 0.25, -0.25, 'wf' + s), hand = B(fore, s * 2.0, -0.1, 0.35, 'wa' + s);
      const fingers = [[s * 2.6, -0.2, -1.2], [s * 2.2, -0.6, -2.2], [s * 1.2, -0.8, -2.8]].map((d, i) => B(hand, ...d, 'wd' + s + i));
      this.wings.push({ s, root, hum, fore, hand, fingers });
    }
    this.root.updateMatrixWorld(true);
    this.sculpt();
    this.state = { speed: 0, attack: -1, attackKind: 'bite', hit: 0, dead: 0, roar: 0 };
    this.t = 0; this.phase = 0;
    this.lookTarget = null;
    this.kind = 'bite';
    this.hideInFirstPerson = [];
  }
  P(b) { return b.getWorldPosition(new THREE.Vector3()); }
  bi(b) { return this.bones.indexOf(b); }

  sculpt() {
    const s = new Sculpt(), P = (b) => this.P(b), bi = (b) => this.bi(b);
    const pel = P(this.pelvis), sp = P(this.spine1), ch = P(this.chest);
    s.ellipsoid(pel.clone().add(V(0, 0, -0.1)), V(1.05, 0.95, 1.2), { bone: bi(this.pelvis), k: 0.35 });
    s.ellipsoid(sp.clone().add(V(0, -0.05, 0)), V(1.25, 1.1, 1.25), { bone: bi(this.spine1), k: 0.35 });
    s.ellipsoid(ch.clone().add(V(0, 0.05, 0.1)), V(1.15, 1.15, 1.0), { bone: bi(this.chest), k: 0.35 });
    // belly plates
    s.ellipsoid(sp.clone().add(V(0, -0.55, 0)), V(0.8, 0.5, 1.5), { bone: bi(this.spine1), mat: 1, k: 0.15 });
    s.ellipsoid(ch.clone().add(V(0, -0.45, 0.35)), V(0.7, 0.5, 0.7), { bone: bi(this.chest), mat: 1, k: 0.15 });
    // neck
    let prev = ch.clone().add(V(0, 0.3, 0.4));
    this.neck.forEach((b, i) => { const q = P(b); s.capsule(prev, q, 0.56 - i * 0.04, 0.52 - i * 0.04, { bone: bi(b), k: 0.12 }); s.ellipsoid(prev.clone().lerp(q, 0.5).add(V(0, -0.22, 0.05)), V(0.28, 0.2, 0.32), { bone: bi(b), mat: 1, k: 0.08 }); prev = q; });
    // head: skull, snout, brow, jaw, horns
    const hd = P(this.head), jw = P(this.jaw);
    s.ellipsoid(hd.clone().add(V(0, 0.08, 0.05)), V(0.4, 0.33, 0.42), { bone: bi(this.head), k: 0.1 });
    s.capsule(hd.clone().add(V(0, 0.05, 0.2)), hd.clone().add(V(0, 0.0, 0.95)), 0.27, 0.17, { bone: bi(this.head), k: 0.1 });
    for (const x of [-1, 1]) {
      s.capsule(hd.clone().add(V(x * 0.22, 0.28, 0.0)), hd.clone().add(V(x * 0.16, 0.2, 0.55)), 0.08, 0.05, { bone: bi(this.head), k: 0.06 });
      s.carve(hd.clone().add(V(x * 0.25, 0.18, 0.32)), V(0.07, 0.05, 0.08), { k: 0.03 });
      s.chain([hd.clone().add(V(x * 0.22, 0.32, -0.15)), hd.clone().add(V(x * 0.38, 0.52, -0.55)), hd.clone().add(V(x * 0.45, 0.6, -1.05)), hd.clone().add(V(x * 0.4, 0.55, -1.45))], 0.11, 0.015, { bone: bi(this.head), mat: 2, k: 0.04 });
      s.chain([hd.clone().add(V(x * 0.36, 0.0, -0.1)), hd.clone().add(V(x * 0.6, -0.05, -0.4)), hd.clone().add(V(x * 0.7, -0.1, -0.7))], 0.06, 0.01, { bone: bi(this.head), mat: 2, k: 0.03 });
      s.ellipsoid(hd.clone().add(V(x * 0.08, 0.13, 0.98)), V(0.04, 0.03, 0.04), { bone: bi(this.head), k: 0.03 });
    }
    s.capsule(jw.clone().add(V(0, -0.05, 0)), jw.clone().add(V(0, -0.07, 0.85)), 0.2, 0.12, { bone: bi(this.jaw), k: 0.08 });
    s.ellipsoid(jw.clone().add(V(0, -0.15, 0.3)), V(0.16, 0.08, 0.4), { bone: bi(this.jaw), mat: 1, k: 0.05 });
    // tail
    prev = pel.clone().add(V(0, 0, -0.6));
    this.tail.forEach((b, i) => { const q = P(b); s.capsule(prev, q, 0.72 * (1 - i / 10.5) + 0.05, 0.72 * (1 - (i + 1) / 10.5) + 0.04, { bone: bi(b), k: 0.15 }); prev = q; });
    s.chain([prev, prev.clone().add(V(0.25, 0, -0.35)), prev.clone().add(V(0, 0, -0.7)), prev.clone().add(V(-0.25, 0, -0.35))], 0.12, 0.02, { bone: bi(this.tail[9]), mat: 2, k: 0.05 });
    // legs and claws
    for (const L of Object.values(this.legs)) {
      const a = P(L.up), b = P(L.lo), c = P(L.ft);
      const front = L === this.legs.FL || L === this.legs.FR;
      s.ellipsoid(a.clone().add(V(0, 0.05, front ? 0.05 : -0.1)), V(0.5, 0.7, front ? 0.55 : 0.75), { bone: bi(L.up), k: 0.25 });
      s.capsule(a, b, front ? 0.42 : 0.55, 0.28, { bone: bi(L.up), k: 0.15 });
      s.capsule(b, c, 0.27, 0.19, { bone: bi(L.lo), k: 0.1 });
      s.ellipsoid(c.clone().add(V(0, -0.05, 0.22)), V(0.28, 0.13, 0.38), { bone: bi(L.ft), k: 0.1 });
      for (let t = -1; t <= 1; t++) s.chain([c.clone().add(V(t * 0.12, -0.05, 0.4)), c.clone().add(V(t * 0.15, -0.1, 0.6)), c.clone().add(V(t * 0.16, -0.18, 0.7))], 0.05, 0.008, { bone: bi(L.ft), mat: 2, k: 0.02 });
    }
    // wing arms
    for (const w of this.wings) {
      const r = P(w.root), h = P(w.fore), a = P(w.hand);
      s.capsule(r, h, 0.2, 0.14, { bone: bi(w.hum), k: 0.08 });
      s.capsule(h, a, 0.13, 0.08, { bone: bi(w.fore), k: 0.05 });
      for (const f of w.fingers) s.capsule(a, P(f), 0.06, 0.02, { bone: bi(f), k: 0.03 });
      s.chain([a, a.clone().add(V(w.s * 0.15, 0.25, 0.1)), a.clone().add(V(w.s * 0.2, 0.4, 0.05))], 0.06, 0.01, { bone: bi(w.hand), mat: 2, k: 0.02 });
    }
    const geo = s.build({ cell: 0.065 });
    const body = skinMaterial({ color: 0x2a4a22, height: heightTex('scales'), map: detailTex('scales'), scale: 1.4, bump: 0.012, roughness: 0.55, clearcoat: 0.08, sheen: 0.12, sheenColor: 0x6a8050 });
    const belly = skinMaterial({ color: 0x8a7444, height: heightTex('wrinkle'), scale: 1.2, bump: 0.01, roughness: 0.55 });
    const horn = skinMaterial({ color: 0x2a2218, height: heightTex('wrinkle'), scale: 3, bump: 0.004, roughness: 0.3, clearcoat: 0.6 });
    const sm = new THREE.SkinnedMesh(geo, [body, belly, horn]);
    sm.castShadow = sm.receiveShadow = true; sm.frustumCulled = false;
    this.root.add(sm); this.root.updateMatrixWorld(true);
    const skel = new THREE.Skeleton(this.bones);
    sm.bind(skel);
    this.skin = sm;
    // wing membranes, skinned to the wing bones
    for (const w of this.wings) this.root.add(this.membrane(w, skel));
    // teeth, eyes, back spikes as rigid parts on the bones
    const tooth = mats.bone();
    for (let i = 0; i < 9; i++) for (const x of [-1, 1]) {
      const t1 = mesh(new THREE.ConeGeometry(0.03, 0.14, 6), tooth, x * (0.13 - i * 0.004), -0.15, 0.25 + i * 0.075); t1.rotation.x = Math.PI; this.head.add(t1);
      const t2 = mesh(new THREE.ConeGeometry(0.025, 0.1, 6), tooth, x * (0.11 - i * 0.004), 0.06, 0.25 + i * 0.07); this.jaw.add(t2);
    }
    for (const x of [-1, 1]) this.head.add(mesh(new THREE.SphereGeometry(0.06, 14, 10), mats.glow(0xffa020, 7), x * 0.24, 0.18, 0.36));
    const spike = mats.horn();
    const addSpike = (bone, h, x = 0, z = 0) => { const sp2 = mesh(new THREE.ConeGeometry(h * 0.25, h, 6), spike, x, 0.5 + h * 0.3, z); sp2.rotation.x = -0.5; bone.add(sp2); };
    [this.pelvis, this.spine1, this.chest].forEach((b, i) => { addSpike(b, 0.6 - i * 0.05, 0, -0.3); addSpike(b, 0.55, 0, 0.35); });
    this.neck.forEach((b, i) => addSpike(b, 0.35 - i * 0.03, 0, 0));
    this.tail.slice(0, 8).forEach((b, i) => { const sp3 = mesh(new THREE.ConeGeometry(0.1 * (1 - i / 9), 0.4 * (1 - i / 9), 6), spike, 0, 0.45 * (1 - i / 11), 0); sp3.rotation.x = -0.6; b.add(sp3); });
    this.mouth = new THREE.Object3D(); this.mouth.position.set(0, -0.05, 1.05); this.head.add(this.mouth);
    this.fireLight = new THREE.PointLight(0xff6a20, 0, 25, 2); this.fireLight.position.set(0, 0, 1.3); this.head.add(this.fireLight);
  }

  // A membrane between the forearm, the hand and the finger tips, back to the flank
  membrane(w, skel) {
    const P = (b) => this.P(b);
    const r = P(w.root), f = P(w.fore), h = P(w.hand), tips = w.fingers.map(P);
    const flank = P(this.pelvis).add(V(w.s * 0.7, 0.4, -0.2));
    // outline from body to tips; fan triangles from the hand joint
    const ring = [r, f, h, tips[0], tips[1], tips[2], flank];
    const ringBones = [w.hum, w.fore, w.hand, w.fingers[0], w.fingers[1], w.fingers[2], this.pelvis];
    const pos = [], idx = [], si = [], sw = [];
    const steps = 8;
    const add = (p, bones, weights) => { pos.push(p.x, p.y, p.z); const bi = bones.map(b => this.bi(b)); while (bi.length < 4) { bi.push(0); weights.push(0); } si.push(...bi); sw.push(...weights); return pos.length / 3 - 1; };
    // build quads between consecutive "spokes" from the hand to each outline point, scalloped between fingers
    const spokes = ring.map((p, i) => {
      const vs = [];
      for (let k = 0; k <= steps; k++) {
        const t = k / steps;
        const q = h.clone().lerp(p, t);
        vs.push(add(q, [w.hand, ringBones[i]], [1 - t, t]));
      }
      return vs;
    });
    for (let i = 0; i < ring.length - 1; i++) {
      // the edge between fingers sags inwards (scallop)
      const a = spokes[i], b = spokes[i + 1];
      for (let k = 0; k < steps; k++) idx.push(a[k], b[k], a[k + 1], a[k + 1], b[k], b[k + 1]);
      if (i >= 2) {
        const mid = add(ring[i].clone().lerp(ring[i + 1], 0.5).lerp(h, 0.25), [ringBones[i], ringBones[i + 1], w.hand], [0.4, 0.4, 0.2]);
        idx.push(a[steps], mid, b[steps]);
      }
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    g.setAttribute('skinIndex', new THREE.Uint16BufferAttribute(si, 4));
    g.setAttribute('skinWeight', new THREE.Float32BufferAttribute(sw, 4));
    g.setIndex(idx); g.computeVertexNormals();
    const m = lite(new THREE.MeshPhysicalMaterial({ color: 0x3a2018, roughness: 0.55, side: THREE.DoubleSide, sheen: 0.6, sheenColor: new THREE.Color(0xa05030), transmission: 0, emissive: 0x200805, emissiveIntensity: 0.4 }));
    const sm = new THREE.SkinnedMesh(g, m);
    sm.castShadow = sm.receiveShadow = true; sm.frustumCulled = false;
    sm.bind(skel);
    return sm;
  }

  update(dt) {
    const s = this.state; this.t += dt;
    const t = this.t;
    const sp = Math.min(s.speed, 6);
    this.phase += dt * (sp > 0.1 ? 1.2 + sp * 0.5 : 0);
    const ph = this.phase, walk = Math.min(1, sp);
    // breathing and idle sway
    const breath = Math.sin(t * 1.1);
    this.chest.scale.set(1 + breath * 0.015, 1 + breath * 0.02, 1);
    this.pelvis.position.y = 1.95 - Math.abs(Math.sin(ph)) * 0.08 * walk;
    // legs: diagonal pairs move together
    const legSwing = (L, p) => { L.up.rotation.x = -Math.sin(p) * 0.45 * walk; L.lo.rotation.x = Math.max(0, Math.cos(p)) * 0.6 * walk; L.ft.rotation.x = -L.up.rotation.x * 0.5; };
    legSwing(this.legs.FL, ph); legSwing(this.legs.HR, ph); legSwing(this.legs.FR, ph + Math.PI); legSwing(this.legs.HL, ph + Math.PI);
    // tail
    this.tail.forEach((b, i) => { b.rotation.y = Math.sin(t * 0.9 - i * 0.45) * 0.09 * (1 + i * 0.15); b.rotation.x = 0.04 + Math.sin(t * 0.6 - i * 0.3) * 0.02; });
    // neck follows the target; attacks override
    let lookYaw = 0, lookPitch = 0;
    if (this.lookTarget) {
      const hp = this.root.worldToLocal(this.lookTarget.clone());
      lookYaw = THREE.MathUtils.clamp(Math.atan2(hp.x, Math.max(0.5, hp.z - 3)), -0.9, 0.9);
      lookPitch = THREE.MathUtils.clamp(Math.atan2(4.5 - hp.y, Math.max(2, hp.z - 2)) * 0.6, -0.2, 0.7);
    }
    let rear = 0, thrust = 0, jaw = 0.05 + Math.max(0, Math.sin(t * 0.7)) * 0.05, wingSpread = 0.05, flap = 0, claw = 0;
    if (s.attack >= 0) {
      const a = s.attack;
      if (s.attackKind === 'breath') {
        rear = a < 0.35 ? ease(a / 0.35) : a < 0.5 ? 1 - ease((a - 0.35) / 0.15) : 0;
        thrust = a < 0.35 ? 0 : a < 0.5 ? ease((a - 0.35) / 0.15) : 1 - ease(Math.max(0, (a - 0.85) / 0.15));
        jaw = a > 0.35 ? 0.7 * (1 - Math.max(0, (a - 0.85) / 0.15)) : 0.2 * rear;
        wingSpread = rear * 0.6;
      } else if (s.attackKind === 'bite') {
        rear = a < 0.4 ? ease(a / 0.4) : 0;
        thrust = a < 0.4 ? 0 : a < 0.55 ? ease((a - 0.4) / 0.15) : 1 - ease((a - 0.55) / 0.45);
        jaw = a < 0.45 ? rear * 0.8 : Math.max(0, 0.8 - (a - 0.45) * 8);
      } else if (s.attackKind === 'claw') {
        claw = a < 0.4 ? ease(a / 0.4) : 1 - ease(Math.min(1, (a - 0.4) / 0.3));
        rear = claw * 0.4;
      } else if (s.attackKind === 'roar') {
        rear = Math.sin(Math.min(1, a) * Math.PI) * 0.8; jaw = rear; wingSpread = rear; flap = rear;
      }
    }
    // positive x pitches the neck forward and down: lower it toward the target, rear back
    // before a strike, then lunge; the head counter-rotates to keep its eyes on you
    let bend = 0;
    this.neck.forEach((b, i) => {
      const k = i / this.neck.length;
      b.rotation.y = lookYaw / this.neck.length + Math.sin(t * 0.8 - i * 0.4) * 0.03;
      b.rotation.x = lookPitch * 0.1 * (1 - k * 0.5) - rear * 0.1 + thrust * 0.2 * (1 - k * 0.4) + Math.sin(t * 1.1 - i * 0.5) * 0.015;
      bend += b.rotation.x;
    });
    this.head.rotation.x = -bend * 0.75 + lookPitch * 0.35 - rear * 0.25 + thrust * 0.15;
    this.jaw.rotation.x = jaw;
    // front claw swipe
    this.legs.FR.up.rotation.x += -claw * 1.3; this.legs.FR.lo.rotation.x += claw * 0.4;
    this.spine1.rotation.x = -rear * 0.12;
    // wings: folded at rest, spread to roar
    for (const w of this.wings) {
      const beat = Math.sin(t * 6) * flap;
      w.hum.rotation.z = w.s * (-0.55 + wingSpread * 0.7 + beat * 0.5);
      w.hum.rotation.y = w.s * (0.7 - wingSpread * 0.8);
      w.fore.rotation.y = w.s * (-1.4 + wingSpread * 1.2);
      w.hand.rotation.y = w.s * (1.9 - wingSpread * 1.6);
      w.fingers.forEach((f, i) => { f.rotation.y = w.s * (0.25 - wingSpread * 0.2) * (i - 1); });
    }
    if (s.hit > 0) this.chest.rotation.z = Math.sin(t * 30) * s.hit * 0.05;
    if (s.dead > 0) {
      const d = ease(Math.min(1, s.dead));
      this.bodyBone.rotation.z = d * 1.3; this.bodyBone.position.y = -d * 0.8;
      this.neck.forEach(b => { b.rotation.x = d * 0.3; }); this.jaw.rotation.x = 0.3 * d;
      for (const w of this.wings) { w.hum.rotation.z = w.s * (-0.2 + d * 0.3); }
    }
  }
  mouthWorld(out = new THREE.Vector3()) { return this.mouth.getWorldPosition(out); }
  tipWorld(out) { return this.mouthWorld(out); }
  setFirstPerson() {}
}
