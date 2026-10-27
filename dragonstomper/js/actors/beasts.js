// Non-humanoid monsters: insects with chitin plates and walking legs, a skinned snake, a jelly slime.
// They share the Humanoid interface: root, state, update(dt), tipWorld(), kind.
import * as THREE from 'three';
import { lite } from '../core/gfx.js';
import { mats, ellipsoid, hornGeo, mesh } from './parts.js';
import { Sculpt, skinMaterial } from './sculpt.js';
import { heightTex, detailTex } from '../core/texgen.js';

const V = (x, y, z) => new THREE.Vector3(x, y, z);
const ease = (t) => t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2;

class Beast {
  constructor() {
    this.root = new THREE.Group();
    this.body = new THREE.Group(); this.root.add(this.body);
    this.state = { speed: 0, attack: -1, attackKind: 'bite', block: 0, hit: 0, dead: 0 };
    this.t = Math.random() * 10; this.phase = 0;
    this.hideInFirstPerson = [];
  }
  tipWorld(out = new THREE.Vector3()) { return (this.mouth || this.body).getWorldPosition(out); }
  setFirstPerson() {}
}

// A jointed leg: coxa, femur, tibia hanging from a hip joint; rotates for the gait
function leg(material, lens, radii, joint) {
  const hip = new THREE.Object3D();
  let parent = hip;
  const segs = [];
  lens.forEach((len, i) => {
    const j = new THREE.Object3D(); parent.add(j);
    if (i > 0) j.position.x = lens[i - 1];
    const seg = mesh(new THREE.CapsuleGeometry(radii[i], len, 4, 10), material, len / 2, 0, 0);
    seg.rotation.z = Math.PI / 2;
    j.add(seg);
    if (joint) j.add(mesh(new THREE.SphereGeometry(radii[i] * 1.3, 10, 8), joint));
    segs.push(j); parent = j;
  });
  const tip = mesh(new THREE.ConeGeometry(radii[radii.length - 1] * 0.9, 0.08, 6), material, lens[lens.length - 1] + 0.03, 0, 0);
  tip.rotation.z = -Math.PI / 2; parent.add(tip);
  return { hip, segs };
}

class Crawler extends Beast {
  // legs: [{ x, z, side, phase }]
  setupLegs(legs, material, lens, radii, joint, spreadUp = 0.5) {
    this.legs = legs.map((L, i) => {
      const l = leg(material, lens, radii, joint);
      l.hip.position.set(L.x, L.y ?? 0, L.z);
      l.side = L.side; l.ph = L.phase ?? (i % 2) * Math.PI; l.yaw0 = L.yaw ?? 0;
      l.hip.rotation.y = (L.side > 0 ? 0 : Math.PI) - L.side * l.yaw0;
      this.body.add(l.hip);
      l.up = spreadUp;
      return l;
    });
  }
  animateLegs(dt, sp) {
    this.phase += dt * (sp > 0.05 ? 4 + sp * 3 : 0);
    for (const l of this.legs) {
      const p = this.phase + l.ph;
      const swing = Math.sin(p) * 0.35 * Math.min(1, sp), lift = Math.max(0, Math.cos(p)) * 0.35 * Math.min(1, sp);
      l.hip.rotation.y = (l.side > 0 ? 0 : Math.PI) - l.side * (l.yaw0 + swing);
      l.segs[0].rotation.z = l.up + lift;
      l.segs[1].rotation.z = -0.9 - lift * 0.5;
      if (l.segs[2]) l.segs[2].rotation.z = -0.9;
    }
  }
}

export function spider() {
  const b = new Crawler();
  const hair = skinMaterial({ color: 0x1c1814, height: heightTex('fur'), map: detailTex('fur'), scale: 8, bump: 0.002, roughness: 0.75, sheen: 1, sheenColor: 0x9a8a70 });
  const shell = mats.chitin(0x16120e, 0.4);
  b.body.position.y = 0.45;
  b.body.add(mesh(ellipsoid(0.26, 0.16, 0.3), shell, 0, 0, 0.12));
  const abd = mesh(ellipsoid(0.38, 0.32, 0.48), hair, 0, 0.12, -0.5); b.body.add(abd); b.abd = abd;
  // hourglass mark
  b.body.add(mesh(ellipsoid(0.08, 0.02, 0.12), mats.glow(0xa01010, 1.5), 0, 0.42, -0.48));
  const head = new THREE.Group(); head.position.set(0, 0.02, 0.38); b.body.add(head); b.mouth = head;
  for (let i = 0; i < 8; i++) head.add(mesh(new THREE.SphereGeometry(i < 2 ? 0.035 : 0.022, 10, 8), mats.glow(0xff2020, 3), (i % 4 - 1.5) * 0.045, 0.06 + Math.floor(i / 4) * 0.05, 0.02));
  for (const s of [-1, 1]) { const f = mesh(hornGeo(0.16, 0.025, 1), mats.claw(), s * 0.05, -0.04, 0.03); f.rotation.x = 2.6; head.add(f); b['fang' + s] = f; }
  const legs = [];
  for (let i = 0; i < 4; i++) for (const s of [-1, 1]) legs.push({ x: s * 0.18, z: 0.25 - i * 0.12, side: s, phase: (i % 2 ? 0 : Math.PI) + (s > 0 ? 0 : Math.PI), yaw: -0.9 + i * 0.6 });
  b.setupLegs(legs, hair, [0.42, 0.55, 0.4], [0.035, 0.028, 0.018], shell, 0.8);
  b.update = (dt) => {
    const s = b.state; b.t += dt;
    b.animateLegs(dt, s.speed);
    b.body.position.y = 0.45 + Math.sin(b.t * 9) * 0.01 * Math.min(1, s.speed);
    b.abd.rotation.x = Math.sin(b.t * 1.5) * 0.05;
    let rear = 0;
    if (s.attack >= 0) { const a = s.attack; rear = a < 0.4 ? ease(a / 0.4) : 1 - ease(Math.min(1, (a - 0.4) / 0.25)); }
    b.body.rotation.x = -rear * 0.55;
    b.legs.slice(0, 2).forEach(l => { l.segs[0].rotation.z += rear * 0.9; });
    b['fang1'].rotation.z = -0.2 - rear * 0.4; b['fang-1'].rotation.z = 0.2 + rear * 0.4;
    if (s.hit > 0) b.body.rotation.z = Math.sin(b.t * 40) * s.hit * 0.1;
    if (s.dead > 0) { const d = ease(Math.min(1, s.dead)); b.body.rotation.z = d * Math.PI; b.body.position.y = 0.45 + d * 0.1; b.legs.forEach(l => { l.segs[0].rotation.z = 1.2; l.segs[1].rotation.z = -2; }); }
  };
  b.kind = 'bite';
  b.root.scale.setScalar(1.15);
  return b;
}

export function bug() {
  // a giant ant: head with mandibles and antennae, thorax, gaster
  const b = new Crawler();
  const shell = mats.chitin(0x0f2a20, 1);
  b.body.position.y = 0.5;
  b.body.add(mesh(ellipsoid(0.16, 0.14, 0.26), shell, 0, 0.05, 0.05));
  const gaster = mesh(ellipsoid(0.3, 0.26, 0.42), shell, 0, 0.14, -0.55); b.body.add(gaster); b.abd = gaster;
  b.body.add(mesh(ellipsoid(0.08, 0.08, 0.12), shell, 0, 0.07, -0.22));
  const head = new THREE.Group(); head.position.set(0, 0.12, 0.36); b.body.add(head); b.mouth = head;
  head.add(mesh(ellipsoid(0.17, 0.14, 0.16), shell));
  for (const s of [-1, 1]) {
    head.add(mesh(ellipsoid(0.05, 0.06, 0.04), mats.chitin(0x050505, 0.2), s * 0.13, 0.04, 0.08));
    const m = mesh(hornGeo(0.22, 0.03, 1.2), shell, s * 0.07, -0.06, 0.12); m.rotation.set(1.4, 0, -s * 0.9); head.add(m); b['mand' + s] = m;
    const a = mesh(hornGeo(0.45, 0.012, 0.6), shell, s * 0.05, 0.1, 0.1); a.rotation.set(-0.6, 0, -s * 0.5); head.add(a);
  }
  const legs = [];
  for (let i = 0; i < 3; i++) for (const s of [-1, 1]) legs.push({ x: s * 0.12, z: 0.18 - i * 0.14, side: s, phase: ((i + (s > 0 ? 0 : 1)) % 2) * Math.PI, yaw: -0.5 + i * 0.5 });
  b.setupLegs(legs, shell, [0.35, 0.5, 0.35], [0.03, 0.022, 0.015], null, 0.7);
  b.update = (dt) => {
    const s = b.state; b.t += dt;
    b.animateLegs(dt, s.speed);
    let k = 0;
    if (s.attack >= 0) { const a = s.attack; k = a < 0.35 ? ease(a / 0.35) : 1 - ease(Math.min(1, (a - 0.35) / 0.3)); }
    head.rotation.x = -k * 0.4 + Math.sin(b.t * 2) * 0.05;
    head.position.z = 0.36 + k * 0.15;
    b['mand1'].rotation.z = -0.9 + k * 0.6; b['mand-1'].rotation.z = 0.9 - k * 0.6;
    b.abd.rotation.x = Math.sin(b.t * 1.7) * 0.06;
    if (s.dead > 0) { const d = ease(Math.min(1, s.dead)); b.body.rotation.z = d * Math.PI; b.body.position.y = 0.5 + d * 0.05; }
  };
  b.kind = 'bite';
  b.root.scale.setScalar(1.3);
  return b;
}

export function beetle() {
  const b = new Crawler();
  const shell = mats.chitin(0x2a1a50, 1);
  b.body.position.y = 0.42;
  const elytra = new THREE.Group(); b.body.add(elytra);
  for (const s of [-1, 1]) { const e = mesh(new THREE.SphereGeometry(0.42, 32, 18, s > 0 ? 0 : Math.PI, Math.PI, 0, Math.PI * 0.55), shell, 0, 0, -0.2); e.scale.set(0.75, 0.75, 1.25); elytra.add(e); }
  b.body.add(mesh(ellipsoid(0.3, 0.12, 0.5), mats.chitin(0x100820, 0.5), 0, -0.02, -0.2));
  b.body.add(mesh(ellipsoid(0.26, 0.16, 0.2), shell, 0, 0.04, 0.3));
  const head = new THREE.Group(); head.position.set(0, 0.04, 0.5); b.body.add(head); b.mouth = head;
  head.add(mesh(ellipsoid(0.15, 0.1, 0.12), shell));
  for (const s of [-1, 1]) { const m = mesh(hornGeo(0.42, 0.04, 1.4), mats.chitin(0x1a0f08, 0.3), s * 0.08, 0, 0.06); m.rotation.set(1.3, 0, -s * 0.35); head.add(m); b['mand' + s] = m; }
  head.add(mesh(new THREE.SphereGeometry(0.03, 8, 6), mats.glow(0xff8030, 2), 0.1, 0.05, 0.08), mesh(new THREE.SphereGeometry(0.03, 8, 6), mats.glow(0xff8030, 2), -0.1, 0.05, 0.08));
  const legs = [];
  for (let i = 0; i < 3; i++) for (const s of [-1, 1]) legs.push({ x: s * 0.2, z: 0.25 - i * 0.22, side: s, phase: ((i + (s > 0 ? 0 : 1)) % 2) * Math.PI, yaw: -0.5 + i * 0.5 });
  b.setupLegs(legs, shell, [0.3, 0.4, 0.3], [0.04, 0.03, 0.02], null, 0.5);
  b.update = (dt) => {
    const s = b.state; b.t += dt;
    b.animateLegs(dt, s.speed);
    let k = 0;
    if (s.attack >= 0) { const a = s.attack; k = a < 0.4 ? ease(a / 0.4) : 1 - ease(Math.min(1, (a - 0.4) / 0.25)); }
    head.rotation.x = -k * 0.6; b.body.rotation.x = -k * 0.15;
    b['mand1'].rotation.z = -0.35 - k * 0.5 + (s.attack > 0.4 ? 0.5 : 0) * k; b['mand-1'].rotation.z = 0.35 + k * 0.5;
    if (s.dead > 0) { const d = ease(Math.min(1, s.dead)); b.body.rotation.z = d * Math.PI; }
  };
  b.kind = 'bite';
  b.root.scale.setScalar(1.35);
  return b;
}

export function scorpion() {
  const b = new Crawler();
  const shell = mats.chitin(0x6a4a1a, 0.5);
  b.body.position.y = 0.32;
  b.body.add(mesh(ellipsoid(0.24, 0.1, 0.32), shell, 0, 0, 0.1));
  for (let i = 0; i < 4; i++) b.body.add(mesh(ellipsoid(0.24 - i * 0.02, 0.09, 0.09), shell, 0, 0.01, -0.22 - i * 0.12));
  // the tail: a chain of segments curling over the back, ending in a stinger
  let prev = new THREE.Object3D(); prev.position.set(0, 0.03, -0.66); b.body.add(prev);
  b.tail = [];
  for (let i = 0; i < 6; i++) {
    const j = new THREE.Object3D(); j.position.z = i ? -0.13 : 0; prev.add(j);
    j.add(mesh(ellipsoid(0.07 - i * 0.004, 0.07 - i * 0.004, 0.08), shell, 0, 0, -0.06));
    prev = j; b.tail.push(j);
  }
  const sting = new THREE.Object3D(); sting.position.z = -0.14; prev.add(sting);
  sting.add(mesh(ellipsoid(0.07, 0.07, 0.09), mats.chitin(0x8a2a10, 0.3), 0, 0, -0.03));
  const barb = mesh(hornGeo(0.14, 0.022, 1.2), mats.claw(), 0, 0, -0.08); barb.rotation.x = -1.8; sting.add(barb);
  b.mouth = sting;
  // pincers: an arm reaching forward, then a big claw with a moving finger
  b.claws = [];
  for (const s of [-1, 1]) {
    const arm = new THREE.Object3D(); arm.position.set(s * 0.16, 0.02, 0.36); arm.rotation.y = s * 0.45; b.body.add(arm);
    const up = mesh(new THREE.CapsuleGeometry(0.035, 0.26, 4, 8), shell, 0, 0, 0.15); up.rotation.x = Math.PI / 2; arm.add(up);
    const fore = new THREE.Object3D(); fore.position.set(0, 0, 0.3); fore.rotation.y = -s * 0.9; arm.add(fore);
    fore.add(mesh(ellipsoid(0.07, 0.055, 0.15), shell, 0, 0, 0.12));
    const fixed = mesh(hornGeo(0.17, 0.028, 0.7), shell, s * 0.035, 0, 0.24); fixed.rotation.x = Math.PI / 2; fore.add(fixed);
    const pin2 = mesh(hornGeo(0.15, 0.024, -0.7), shell, -s * 0.035, 0, 0.24); pin2.rotation.x = Math.PI / 2; fore.add(pin2);
    b.claws.push({ arm, fore, pin2, s });
  }
  const legs = [];
  for (let i = 0; i < 4; i++) for (const s of [-1, 1]) legs.push({ x: s * 0.16, z: 0.12 - i * 0.1, side: s, phase: ((i + (s > 0 ? 0 : 1)) % 2) * Math.PI, yaw: -0.3 + i * 0.35 });
  b.setupLegs(legs, shell, [0.25, 0.32, 0.25], [0.025, 0.02, 0.014], null, 0.6);
  b.update = (dt) => {
    const s = b.state; b.t += dt;
    b.animateLegs(dt, s.speed);
    let k = 0;
    if (s.attack >= 0) { const a = s.attack; k = a < 0.45 ? ease(a / 0.45) : 1 - ease(Math.min(1, (a - 0.45) / 0.2)); }
    // curl the tail up over the back; strike forward on attack
    b.tail.forEach((j, i) => { j.rotation.x = (i === 0 ? -0.9 : -0.42) + Math.sin(b.t * 2 + i) * 0.04 + (s.attack >= 0 ? (s.attack < 0.45 ? -k * 0.12 : (1 - k) * 0.25) : 0); });
    b.claws.forEach(c => { c.pin2.rotation.z = c.s * (Math.max(0, Math.sin(b.t * 3 + c.s)) * 0.4 + k * 0.4); c.arm.rotation.y = c.s * 0.45 + Math.sin(b.t * 1.4 + c.s) * 0.08; });
    if (s.dead > 0) { const d = ease(Math.min(1, s.dead)); b.body.rotation.z = d * Math.PI; b.body.position.y = 0.32 + d * 0.15; }
  };
  b.kind = 'sting';
  b.root.scale.setScalar(1.6);
  return b;
}

export function snake() {
  const b = new Beast();
  const n = 22, segLen = 0.18;
  const bones = [];
  let parent = b.body;
  const rootBone = new THREE.Bone(); rootBone.position.set(0, 0.12, 1.2); parent.add(rootBone); bones.push(rootBone);
  parent = rootBone;
  for (let i = 1; i < n; i++) { const bn = new THREE.Bone(); bn.position.z = -segLen; parent.add(bn); bones.push(bn); parent = bn; }
  b.root.updateMatrixWorld(true);
  const s = new Sculpt();
  const P = (bn) => bn.getWorldPosition(new THREE.Vector3());
  const radius = (i) => 0.11 * Math.sin(Math.min(1, (i + 2) / 5) * Math.PI / 2) * (1 - Math.pow(i / n, 2) * 0.85);
  for (let i = 0; i < n - 1; i++) s.capsule(P(bones[i]), P(bones[i + 1]), radius(i), radius(i + 1), { bone: i, k: 0.05 });
  // head on the first bone, pointing +z
  const h0 = P(bones[0]);
  s.ellipsoid(h0.clone().add(V(0, 0.02, 0.12)), V(0.1, 0.07, 0.16), { bone: 0, k: 0.05 });
  s.ellipsoid(h0.clone().add(V(0, -0.02, 0.18)), V(0.07, 0.04, 0.1), { bone: 0, mat: 1, k: 0.03 });
  for (const sx of [-1, 1]) s.carve(h0.clone().add(V(sx * 0.06, 0.06, 0.17)), V(0.022, 0.018, 0.022), { k: 0.008 });
  const geo = s.build({ cell: 0.02 });
  const scaleM = skinMaterial({ color: 0x1e2a6a, height: heightTex('scales'), map: detailTex('scales'), scale: 9, bump: 0.003, roughness: 0.3, clearcoat: 0.8, sheen: 0.3, sheenColor: 0x6080ff });
  const belly = skinMaterial({ color: 0xa8a070, height: heightTex('wrinkle'), scale: 6, bump: 0.002, roughness: 0.4 });
  const sm = new THREE.SkinnedMesh(geo, [scaleM, belly]);
  sm.castShadow = sm.receiveShadow = true; sm.frustumCulled = false;
  b.root.add(sm); b.root.updateMatrixWorld(true);
  sm.bind(new THREE.Skeleton(bones));
  for (const sx of [-1, 1]) rootBone.add(mesh(new THREE.SphereGeometry(0.02, 10, 8), mats.glow(0xffd020, 4), sx * 0.06, 0.06 - 0.12 * 0 + 0.0, 0.17));
  const tongue = mesh(new THREE.CylinderGeometry(0.004, 0.004, 0.12, 4), mats.glow(0x801020, 1), 0, -0.02, 0.32); tongue.rotation.x = Math.PI / 2; rootBone.add(tongue);
  b.mouth = rootBone; b.bones = bones;
  b.update = (dt) => {
    const st = b.state; b.t += dt;
    const sp = st.speed;
    b.phase += dt * (1.5 + sp * 2.5);
    let k = 0;
    if (st.attack >= 0) { const a = st.attack; k = a < 0.5 ? ease(a / 0.5) * 0.4 : a < 0.65 ? 0.4 + ease((a - 0.5) / 0.15) * 0.6 : 1 - ease((a - 0.65) / 0.35); }
    // the front third rears up like a cobra; the rest slithers
    const rear = 0.6 + k * 0.4;
    bones.forEach((bn, i) => {
      const f = i / n;
      bn.rotation.y = Math.sin(b.phase - i * 0.5) * 0.28 * (0.3 + f) * (i < 6 ? 0.4 : 1);
      bn.rotation.x = i === 0 ? rear * 0.9 - k * 0.9 : i < 5 ? -rear * 0.32 : i === 5 ? rear * 0.25 : 0;
    });
    rootBone.position.z = 1.2 + k * 0.4;
    tongue.visible = Math.sin(b.t * 7) > 0.6;
    if (st.dead > 0) { const d = ease(Math.min(1, st.dead)); bones.forEach((bn, i) => { bn.rotation.x *= (1 - d); bn.rotation.z = d * 0.2; }); }
  };
  b.kind = 'bite';
  return b;
}

export function slime() {
  const b = new Beast();
  const geo = new THREE.SphereGeometry(0.75, 64, 40);
  const mat = lite(new THREE.MeshPhysicalMaterial({ color: 0x40b0a0, roughness: 0.05, metalness: 0, transmission: 0.92, thickness: 1.4, ior: 1.35, attenuationColor: new THREE.Color(0x10806a), attenuationDistance: 0.5, clearcoat: 1, clearcoatRoughness: 0.05 }));
  const u = { uT: { value: 0 }, uSquash: { value: 0 } };
  mat.onBeforeCompile = (sh) => {
    Object.assign(sh.uniforms, u);
    sh.vertexShader = sh.vertexShader
      .replace('#include <common>', '#include <common>\nuniform float uT; uniform float uSquash;')
      .replace('#include <begin_vertex>', `vec3 transformed = position;
float w = sin(position.x * 6.0 + uT * 3.0) * sin(position.y * 5.0 + uT * 2.3) * sin(position.z * 6.0 + uT * 2.7) * 0.06;
transformed += normal * w;
transformed.y = transformed.y * (0.72 - uSquash * 0.25) + 0.0;
transformed.xz *= 1.0 + uSquash * 0.2 + max(0.0, -position.y) * 0.25;`);
  };
  mat.customProgramCacheKey = () => 'slime';
  const blob = mesh(geo, mat, 0, 0.55, 0); blob.castShadow = true;
  b.body.add(blob);
  const core = mesh(new THREE.SphereGeometry(0.22, 24, 16), mats.glow(0x60ffb0, 1.6), 0, 0.5, 0); b.body.add(core);
  for (let i = 0; i < 6; i++) b.body.add(mesh(new THREE.SphereGeometry(0.03 + Math.random() * 0.03, 10, 8), mats.bone(), (Math.random() - 0.5) * 0.8, 0.3 + Math.random() * 0.5, (Math.random() - 0.5) * 0.8));
  b.mouth = core;
  b.update = (dt) => {
    const s = b.state; b.t += dt; u.uT.value = b.t;
    let k = 0;
    if (s.attack >= 0) { const a = s.attack; k = a < 0.4 ? -ease(a / 0.4) : 1 - ease(Math.min(1, (a - 0.4) / 0.3)) * 2; }
    u.uSquash.value = Math.sin(b.t * 3) * 0.08 + Math.min(1, s.speed) * Math.abs(Math.sin(b.t * 6)) * 0.3 - k * 0.5;
    b.body.position.z = Math.max(0, k) * 0.6;
    core.position.y = 0.5 + Math.sin(b.t * 2) * 0.05;
    if (s.dead > 0) { const d = Math.min(1, s.dead); b.body.scale.set(1 + d * 0.8, Math.max(0.05, 1 - d), 1 + d * 0.8); }
  };
  b.kind = 'slam';
  b.root.scale.setScalar(1.2);
  return b;
}
