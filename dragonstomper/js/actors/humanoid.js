// Procedural humanoids: a bone skeleton, a sculpted seamless skinned body (see sculpt.js),
// rigid armour/weapons on the bones, and code-driven animation.
// Faces +z, stands on y = 0, about 1.85 m tall at scale 1.
import * as THREE from 'three';
import { Mocap, hasMocap } from './mocap.js';
import { mats, lathe, ellipsoid, segment, hornGeo, rockGeo, mesh, longsword, axe, halberd } from './parts.js';
import { Sculpt, skinMaterial } from './sculpt.js';
import { heightTex, detailTex } from '../core/texgen.js';
import { pbrMaps, model } from '../core/assets.js';
import { ROM } from '../../data.js';

const V = (x, y, z) => new THREE.Vector3(x, y, z);
const ease = (t) => t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2;
const lerp = THREE.MathUtils.lerp;

const _q = new THREE.Quaternion(), _e = new THREE.Euler();

export class Humanoid {
  constructor({ legLen = 0.46, armLen = 0.3, torso = 1, hunch = 0, shoulderW = 0.2 } = {}) {
    this.bones = [];
    const J = (parent, x, y, z, name) => {
      const b = new THREE.Bone(); b.name = name; b.position.set(x, y, z);
      parent.add(b); this.bones.push(b); return b;
    };
    const root = this.root = new THREE.Group();
    this.body = J(root, 0, 0, 0, 'body');
    const hipH = legLen * 2 + 0.06;
    this.hips = J(this.body, 0, hipH, 0, 'hips');
    this.spine = J(this.hips, 0, 0.1 * torso, 0, 'spine');
    this.chest = J(this.spine, 0, 0.2 * torso, 0, 'chest');
    this.neck = J(this.chest, 0, 0.27 * torso, 0, 'neck');
    this.head = J(this.neck, 0, 0.1, 0, 'head');
    const side = (s, n) => {
      const sh = J(this.chest, s * shoulderW * torso, 0.22 * torso, 0, n + 'sh');
      const up = J(sh, 0, 0, 0, n + 'up'), fo = J(up, 0, -armLen, 0, n + 'fo'), ha = J(fo, 0, -armLen * 0.9, 0, n + 'ha');
      const th = J(this.hips, s * 0.1, -0.04, 0, n + 'th'), sn = J(th, 0, -legLen, 0, n + 'sn'), ft = J(sn, 0, -legLen, 0, n + 'ft');
      return { sh, up, fo, ha, th, sn, ft };
    };
    this.L = side(1, 'L'); this.R = side(-1, 'R');
    this.J = J;
    this.hunch = hunch; this.legLen = legLen; this.armLen = armLen;
    this.phase = 0; this.t = Math.random() * 10;
    this.state = { speed: 0, attack: -1, attackKind: 'slash', block: 0, hit: 0, dead: 0, swingSide: 1 };
    this.hideInFirstPerson = [];
  }
  add(bone, obj) { bone.add(obj); return obj; }
  bi(b) { return this.bones.indexOf(b); }
  P(b) { this.root.updateMatrixWorld(true); return b.getWorldPosition(new THREE.Vector3()); }

  // Sculpt a seamless body for this skeleton. spec sets proportions; extra(sculpt, P, bi) adds features.
  sculptBody(spec, materials, extra = null, buildOpts = {}) {
    const s = new Sculpt(), h = this, P = (b) => h.P(b), bi = (b) => h.bi(b);
    const b = { hipW: 0.15, hipD: 0.11, waist: 0.13, chestW: 0.18, chestD: 0.12, shoulder: 0.065, neck: 0.055, arm: [0.055, 0.045, 0.034], leg: [0.095, 0.062, 0.042], hand: 1, muscle: 0, head: true, foot: 'boot', ...spec };
    const k = 0.05;
    s.ellipsoid(P(h.hips).add(V(0, -0.01, -0.005)), V(b.hipW, 0.12, b.hipD), { bone: bi(h.hips), k });
    s.ellipsoid(P(h.spine).add(V(0, 0.06, 0.01)), V(b.waist, 0.14, b.waist * 0.78), { bone: bi(h.spine), k });
    s.ellipsoid(P(h.chest).add(V(0, 0.12, 0.0)), V(b.chestW, 0.17, b.chestD), { bone: bi(h.chest), k });
    if (b.muscle) for (const sx of [-1, 1]) s.ellipsoid(P(h.chest).add(V(sx * 0.085, 0.16, b.chestD * 0.55)), V(0.085, 0.06, 0.045).multiplyScalar(0.8 + b.muscle * 0.4), { bone: bi(h.chest), k: 0.03 });
    s.capsule(P(h.L.sh).add(V(0, 0.02, 0)), P(h.R.sh).add(V(0, 0.02, 0)), b.shoulder * 0.9, b.shoulder * 0.9, { bone: bi(h.chest), k });
    s.capsule(P(h.neck).add(V(0, -0.04, 0)), P(h.head).add(V(0, 0.04, 0)), b.neck, b.neck * 0.92, { bone: bi(h.neck), k: 0.04 });
    for (const q of [h.L, h.R]) {
      const sh = P(q.sh), el = P(q.fo), wr = P(q.ha), hip = P(q.th), kn = P(q.sn), an = P(q.ft);
      s.ellipsoid(sh.clone().add(V(0, -0.02, 0)), V(b.shoulder * 0.95, b.shoulder * 1.0, b.shoulder * 0.95).multiplyScalar(1 + b.muscle * 0.15), { bone: bi(q.up), k: 0.045 });
      s.capsule(sh, el, b.arm[0], b.arm[1], { bone: bi(q.up), k: 0.03 });
      if (b.muscle) s.ellipsoid(sh.clone().lerp(el, 0.55).add(V(0, 0, 0.025)), V(b.arm[0] * 0.8, 0.07, b.arm[0] * 0.8).multiplyScalar(1 + b.muscle * 0.3), { bone: bi(q.up), k: 0.02 });
      s.capsule(el, wr, b.arm[1] * 1.05, b.arm[2], { bone: bi(q.fo), k: 0.03 });
      s.ellipsoid(wr.clone().add(V(0, -0.06 * b.hand, 0.005)), V(0.036, 0.06, 0.02).multiplyScalar(b.hand), { bone: bi(q.ha), k: 0.02 });
      s.capsule(hip.clone().add(V(0, 0.04, 0)), kn, b.leg[0], b.leg[1], { bone: bi(q.th), k: 0.04 });
      s.ellipsoid(hip.clone().add(V(0, 0, -0.06)), V(0.08, 0.09, 0.07).multiplyScalar(b.hipW / 0.15), { bone: bi(h.hips), k: 0.04 });
      s.capsule(kn, an, b.leg[1] * 0.95, b.leg[2], { bone: bi(q.sn), k: 0.03 });
      s.ellipsoid(kn.clone().lerp(an, 0.3).add(V(0, 0, -0.03)), V(b.leg[1] * 0.8, 0.09, b.leg[1] * 0.7), { bone: bi(q.sn), k: 0.025 });
      if (b.foot === 'boot') s.capsule(an.clone().add(V(0, -0.02, -0.02)), an.clone().add(V(0, -0.045, 0.13)), 0.045, 0.04, { bone: bi(q.ft), k: 0.03 });
      else s.capsule(an.clone().add(V(0, -0.02, 0)), an.clone().add(V(0, -0.05, 0.1)), b.leg[2] * 0.9, b.leg[2] * 0.7, { bone: bi(q.ft), k: 0.03 });
    }
    if (b.head) {
      const hd = P(h.head), hk = bi(h.head);
      const hs = b.headScale || 1;
      s.ellipsoid(hd.clone().add(V(0, 0.11 * hs, -0.005)), V(0.088, 0.105, 0.1).multiplyScalar(hs), { bone: hk, k: 0.03 });
      s.ellipsoid(hd.clone().add(V(0, 0.035 * hs, 0.045 * hs)), V(0.068, 0.05, 0.065).multiplyScalar(hs), { bone: hk, k: 0.04 });
      s.capsule(hd.clone().add(V(-0.05, 0.135, 0.085).multiplyScalar(hs)), hd.clone().add(V(0.05, 0.135, 0.085).multiplyScalar(hs)), 0.02 * hs, 0.02 * hs, { bone: hk, k: 0.02 });
      s.ellipsoid(hd.clone().add(V(0, 0.09, 0.105).multiplyScalar(hs)), V(0.016, 0.03, 0.022).multiplyScalar(hs * (b.nose || 1)), { bone: hk, k: 0.015 });
      for (const sx of [-1, 1]) {
        s.ellipsoid(hd.clone().add(V(sx * 0.055, 0.075, 0.07).multiplyScalar(hs)), V(0.025, 0.02, 0.02).multiplyScalar(hs), { bone: hk, k: 0.02 });
        s.carve(hd.clone().add(V(sx * 0.036, 0.112, 0.1).multiplyScalar(hs)), V(0.02, 0.016, 0.02).multiplyScalar(hs), { k: 0.012 });
      }
      if (b.mouth) s.carve(hd.clone().add(V(0, 0.035, 0.1).multiplyScalar(hs)), V(0.035, 0.008 + b.mouth * 0.02, 0.03).multiplyScalar(hs), { k: 0.01 });
    }
    if (extra) extra(s, P, bi);
    const geo = s.build({ cell: buildOpts.cell || 0.016, noise: buildOpts.noise || 0, noiseScale: buildOpts.noiseScale || 8 });
    const sm = new THREE.SkinnedMesh(geo, materials);
    sm.castShadow = sm.receiveShadow = true;
    this.root.add(sm);
    this.root.updateMatrixWorld(true);
    sm.bind(new THREE.Skeleton(this.bones));
    sm.frustumCulled = false;
    this.skin = sm;
    return sm;
  }
  // eyes sit in the carved sockets
  eyes(color, glow = 6, r = 0.013, hs = 1) {
    const m = glow ? mats.glow(color, glow) : new THREE.MeshPhysicalMaterial({ color, roughness: 0.05, clearcoat: 1 });
    for (const sx of [-1, 1]) this.add(this.head, mesh(new THREE.SphereGeometry(r * hs, 12, 10), m, sx * 0.036 * hs, 0.112 * hs, 0.093 * hs));
  }

  // advance the animation from this.state
  update(dt) {
    const s = this.state, L = this.L, R = this.R;
    this.t += dt;
    const sp = Math.min(s.speed, 8);
    this.phase += dt * (sp > 0.1 ? 2.6 + sp * 0.9 : 0);
    const run = THREE.MathUtils.smoothstep(sp, 0.2, 2), sprint = THREE.MathUtils.smoothstep(sp, 3.5, 6.5);
    const ph = this.phase;
    const amp = 0.55 * run + 0.25 * sprint;
    // motion capture drives walking, running and idling when it has loaded
    const mc = this.mocap !== false && hasMocap() ? (this.mc ||= new Mocap(this)) : null;
    if (mc) {
      mc.update(dt, sp);
      if (this.hunch) { this.spine.rotateX(this.hunch); this.neck.rotateX(-this.hunch * 0.8); }
    } else {
      L.th.rotation.x = -Math.sin(ph) * amp; R.th.rotation.x = Math.sin(ph) * amp;
      L.sn.rotation.x = Math.max(0, Math.cos(ph)) * (0.9 * run + 0.5 * sprint) + 0.05;
      R.sn.rotation.x = Math.max(0, -Math.cos(ph)) * (0.9 * run + 0.5 * sprint) + 0.05;
      L.ft.rotation.x = -L.th.rotation.x * 0.3 - L.sn.rotation.x * 0.4; R.ft.rotation.x = -R.th.rotation.x * 0.3 - R.sn.rotation.x * 0.4;
      const breathe = Math.sin(this.t * 1.8) * 0.015;
      this.hips.position.y = this.legLen * 2 + 0.06 - Math.abs(Math.cos(ph)) * 0.05 * run - 0.03 * run - (this.hunch > 0.3 ? 0.04 : 0);
      this.spine.rotation.set(0.12 * run + 0.12 * sprint + this.hunch, Math.sin(ph) * 0.12 * run, 0);
      this.chest.rotation.x = breathe;
      this.neck.rotation.x = -this.hunch * 0.8 - 0.08 * run;
    }
    let lUx = Math.sin(ph) * amp * 0.7, rUx = -Math.sin(ph) * amp * 0.7;
    let lUz = 0.12, rUz = -0.12, lFx = -0.25 - 0.4 * run, rFx = -0.25 - 0.4 * run, rUy = 0, lUy = 0;
    if (this.weapon) { rFx -= 0.35; rUx -= 0.1; }
    if (s.block > 0) {
      const b = ease(s.block);
      lUx = lerp(lUx, -1.2, b); lUz = lerp(lUz, 0.2, b); lFx = lerp(lFx, -1.0, b); lUy = lerp(0, 0.7, b);
    }
    if (!mc) this.chest.rotation.y = 0;
    if (s.attack >= 0) {
      const a = s.attack, side = s.swingSide, k = s.attackKind;
      let ux, uz, fx, tw = 0;
      if (k === 'thrust') {
        const draw = a < 0.35 ? ease(a / 0.35) : 1 - ease(Math.min(1, (a - 0.35) / 0.2));
        const back = a < 0.55 ? 0 : ease((a - 0.55) / 0.45);
        ux = lerp(-1.5, -0.7, draw) * (1 - back) + -0.5 * back; fx = lerp(-0.1, -1.7, draw) * (1 - back) - 0.6 * back; uz = -0.15; tw = 0.35 * draw - 0.25 * (1 - draw) * (1 - back);
      } else if (k === 'claw' || k === 'smash') {
        const up = a < 0.4 ? ease(a / 0.4) : 1 - ease(Math.min(1, (a - 0.4) / 0.22));
        ux = lerp(-0.5, -2.7, up); fx = lerp(-0.3, -1.0, up); uz = -0.3; tw = 0.35 * (1 - up) * (a > 0.4 ? 1 : 0);
        lUx = lerp(-0.5, -2.7, up * (k === 'smash' ? 1 : 0.6)); lFx = fx; lUz = 0.3;
        this.spine.rotation.x += (k === 'smash' ? 0.5 : 0.25) * (1 - up) * (a > 0.4 ? 1 : 0);
      } else {
        const wind = a < 0.35 ? ease(a / 0.35) : 0, strike = a < 0.35 ? 0 : a < 0.6 ? ease((a - 0.35) / 0.25) : 1, rec = a < 0.6 ? 0 : ease((a - 0.6) / 0.4);
        const raise = Math.max(wind * (1 - strike), a >= 0.35 ? 0 : wind);
        const cut = strike * (1 - rec);
        ux = lerp(-0.3, -2.5, raise) - cut * 1.0;
        uz = side > 0 ? lerp(-0.2, -1.2, raise) + cut * 1.1 : lerp(-0.2, 0.5, raise) - cut * 0.9;
        fx = lerp(-0.6, -1.4, raise) + cut * 1.0;
        tw = side * (raise * 0.55 - cut * 0.7);
        rUy = side * (raise * 0.6 - cut * 0.6);
      }
      rUx = ux; rUz = uz; rFx = fx;
      this.spine.rotateY(tw);
      this.chest.rotateY(tw * 0.5);
    }
    if (!mc) {
      L.up.rotation.set(lUx, lUy, lUz); R.up.rotation.set(rUx, rUy, rUz);
      L.fo.rotation.x = lFx; R.fo.rotation.x = rFx;
    } else {
      // the mocap swings the arms; fighting takes them over
      const both = s.attack >= 0 && (s.attackKind === 'claw' || s.attackKind === 'smash');
      // a sword arm keeps its grip: carried forward, swinging a little with the stride
      if (s.attack >= 0 || this.weapon) { R.up.rotation.set(rUx, rUy, rUz); R.fo.rotation.set(rFx, 0, 0); R.ha.quaternion.identity(); }
      if (both || s.block > 0) {
        const w = both ? 1 : ease(Math.min(1, s.block));
        L.up.quaternion.slerp(_q.setFromEuler(_e.set(lUx, lUy, lUz)), w);
        L.fo.quaternion.slerp(_q.setFromEuler(_e.set(lFx, 0, 0)), w);
      }
    }
    if (this.tail) this.tail.forEach((t, i) => { t.rotation.y = Math.sin(this.t * 2.2 - i * 0.6) * 0.18; t.rotation.x = -0.12 + Math.sin(this.t * 1.3 - i * 0.4) * 0.05; });
    if (this.cape) this.cape.rotation.x = 0.1 + run * 0.35 + sprint * 0.25 + Math.sin(this.t * 2.5) * 0.03 * (1 + run * 2);
    if (s.hit > 0) { this.spine.rotation.x -= s.hit * 0.35; this.neck.rotation.x -= s.hit * 0.2; }
    if (s.dead > 0) {
      const d = ease(Math.min(1, s.dead));
      this.body.rotation.x = -d * 1.45;
      this.body.position.y = d * 0.12;
      this.body.position.z = -d * 0.35;
      for (const q of [L, R]) { q.up.rotation.x = lerp(q.up.rotation.x, -2.4, d); q.up.rotation.z = (q === L ? 1 : -1) * lerp(0.12, 0.9, d); q.sn.rotation.x = lerp(q.sn.rotation.x, 0.25, d); }
    } else { this.body.rotation.x = 0; this.body.position.set(0, 0, 0); }
  }
  tipWorld(out = new THREE.Vector3()) {
    if (!this.weapon) return this.R.ha.getWorldPosition(out);
    return this.weapon.localToWorld(out.copy(this.weapon.userData.tip || V(0, 0.9, 0)));
  }
  setFirstPerson(on) { for (const o of this.hideInFirstPerson) o.visible = !on; }
}

// ---------- materials for sculpted bodies ----------
function skinM(color, { height = 'skin', scale = 4, bump = 0.002, roughness = 0.55, sheen = 0.35, detail = 'skin', clearcoat = 0, metalness = 0 } = {}) {
  return skinMaterial({ color, height: heightTex(height), map: detail ? detailTex(detail) : null, scale, bump, roughness, sheen, sheenColor: new THREE.Color(color).lerp(new THREE.Color(0xffd8c8), 0.5), clearcoat, metalness });
}
const hornM = () => skinMaterial({ color: 0x2a221a, height: heightTex('wrinkle'), scale: 12, bump: 0.0015, roughness: 0.35, clearcoat: 0.6 });

// ---------- the hero: a knight in plate over mail ----------
function heraldry(ctx, n) {
  const rows = ROM.part1.monsters[0].rows;
  const px = n / 18, ox = n / 2 - 4 * px, oy = n * 0.3;
  ctx.fillStyle = '#d8b050';
  rows.forEach((r, y) => { for (let x = 0; x < 8; x++) if ((r >> (7 - x)) & 1) ctx.fillRect(ox + x * px, oy + y * px, px + 0.5, px + 0.5); });
  ctx.strokeStyle = '#d8b050'; ctx.lineWidth = n / 40; ctx.strokeRect(n * 0.06, n * 0.06, n * 0.88, n * 0.88);
}

export function knight({ steelTint = 0xd0d2d8, cloth = '#1d2f6e', emblem = true, cape = '#6e1414', plume = 0xa01010, gold = false, weapon = 'sword', shield = true, scale = 1, helm = 'great' } = {}) {
  const h = new Humanoid();
  const st = mats.steel(steelTint, 0.3), lea = mats.leather();
  const trim = gold ? mats.gold() : st;
  const tab = mats.cloth(cloth, emblem ? heraldry : null);
  // a continuous mail-and-padding body under the plates
  const mailM = skinMaterial({ color: 0x8a8c92, height: heightTex('mail'), scale: 55, bump: 0.0012, roughness: 0.42, metalness: 1 });
  h.sculptBody({ chestW: 0.17, waist: 0.14, arm: [0.05, 0.042, 0.034], leg: [0.08, 0.056, 0.042], head: false }, [mailM]);
  // cuirass, fauld, belt, tabard
  h.add(h.chest, mesh(lathe([[0.001, 0.335], [0.1, 0.325], [0.19, 0.285], [0.215, 0.2], [0.21, 0.1], [0.185, 0], [0.165, -0.08], [0.16, -0.12]], { sx: 1, sz: 0.72, segs: 40 }), st));
  const collar = mesh(new THREE.TorusGeometry(0.165, 0.014, 10, 40), trim, 0, 0.315, 0); collar.rotation.x = Math.PI / 2; collar.scale.set(1, 0.72, 1); h.add(h.chest, collar);
  h.add(h.spine, mesh(lathe([[0.165, 0.08], [0.175, 0], [0.195, -0.1], [0.215, -0.18]], { sz: 0.78, segs: 40 }), st));
  const belt = mesh(new THREE.TorusGeometry(0.182, 0.024, 10, 40), lea, 0, 0.02, 0); belt.rotation.x = Math.PI / 2; belt.scale.set(1, 0.78, 1);
  h.add(h.spine, belt);
  h.add(h.spine, mesh(new THREE.BoxGeometry(0.06, 0.05, 0.02), mats.gold(), 0, 0.02, 0.145));
  for (const s of [1, -1]) {
    const g = new THREE.PlaneGeometry(0.34, 0.66, 4, 8);
    const p = g.attributes.position;
    for (let i = 0; i < p.count; i++) { const x = p.getX(i), y = p.getY(i); p.setZ(i, -x * x * 1.2 + (y < -0.1 ? (y + 0.1) * 0.12 : 0)); }
    g.computeVertexNormals();
    const panel = mesh(g, tab, 0, -0.12, s * 0.16);
    if (s < 0) panel.rotation.y = Math.PI;
    h.add(h.spine, panel);
  }
  // gorget and helm
  h.add(h.neck, mesh(lathe([[0.135, -0.05], [0.115, 0.02], [0.085, 0.09]], { sz: 0.92, segs: 32 }), st));
  const helmG = new THREE.Group(); h.add(h.head, helmG);
  if (helm === 'great') {
    helmG.add(mesh(lathe([[0.137, -0.035], [0.143, 0.06], [0.141, 0.2], [0.122, 0.262], [0.07, 0.287], [0.001, 0.292]], { sz: 1.1, segs: 40 }), st));
    helmG.add(mesh(new THREE.BoxGeometry(0.21, 0.016, 0.04), mats.dark(), 0, 0.155, 0.146));
    helmG.add(mesh(new THREE.BoxGeometry(0.022, 0.25, 0.03), trim, 0, 0.12, 0.153));
    for (let i = 0; i < 6; i++) { const hole = mesh(new THREE.CylinderGeometry(0.004, 0.004, 0.03, 6), mats.dark(), (i % 2 ? 1 : -1) * (0.03 + Math.floor(i / 2) * 0.016), 0.065, 0.145); hole.rotation.x = Math.PI / 2; helmG.add(hole); }
  } else {
    helmG.add(mesh(lathe([[0.13, 0.03], [0.137, 0.12], [0.127, 0.22], [0.08, 0.28], [0.001, 0.3]], { sz: 1.08, segs: 36 }), st));
    helmG.add(mesh(new THREE.SphereGeometry(0.21, 36, 8, 0, Math.PI * 2, Math.PI * 0.45, Math.PI * 0.08), st, 0, 0.12, 0));
    helmG.add(mesh(new THREE.BoxGeometry(0.15, 0.13, 0.03), st, 0, 0.09, 0.128));
    helmG.add(mesh(new THREE.BoxGeometry(0.13, 0.012, 0.04), mats.dark(), 0, 0.125, 0.13));
  }
  if (plume) {
    const pm = new THREE.MeshPhysicalMaterial({ color: plume, roughness: 0.9, sheen: 1, sheenColor: new THREE.Color(plume).lerp(new THREE.Color(0xffffff), 0.4) });
    for (let i = 0; i < 11; i++) {
      const f = mesh(new THREE.ConeGeometry(0.022, 0.32, 6), pm, (i % 2 ? 0.008 : -0.008), 0.33 - i * 0.004, -0.03 - i * 0.017);
      f.rotation.x = -0.45 - i * 0.13;
      helmG.add(f);
    }
  }
  h.hideInFirstPerson.push(helmG);
  for (const [q, s] of [[h.L, 1], [h.R, -1]]) {
    const pa = mesh(new THREE.SphereGeometry(0.118, 32, 12, 0, Math.PI * 2, 0, Math.PI * 0.55), st, s * 0.025, 0.025, 0);
    pa.scale.set(1.1, 0.9, 1); pa.rotation.z = -s * 0.35; h.add(q.up, pa);
    const pa2 = mesh(new THREE.SphereGeometry(0.11, 32, 12, 0, Math.PI * 2, 0, Math.PI * 0.42), trim, s * 0.035, -0.035, 0);
    pa2.scale.set(1.12, 0.8, 1.04); pa2.rotation.z = -s * 0.5; h.add(q.up, pa2);
    h.add(q.up, mesh(segment(0.26, 0.056, 0.05), st, 0, -0.03, 0));
    const couter = mesh(new THREE.SphereGeometry(0.058, 20, 12), st); h.add(q.fo, couter);
    const wing = mesh(new THREE.CircleGeometry(0.055, 20), trim, s * 0.045, 0, -0.01); wing.rotation.y = s * Math.PI / 2; h.add(q.fo, wing);
    h.add(q.fo, mesh(segment(0.25, 0.052, 0.043, { bulge: 0.2 }), st, 0, -0.02, 0));
    const hand = new THREE.Group();
    hand.add(mesh(new THREE.BoxGeometry(0.078, 0.09, 0.04), st, 0, -0.035, 0));
    for (let f = 0; f < 4; f++) { const fg = mesh(new THREE.CylinderGeometry(0.012, 0.011, 0.075, 8), st, -0.028 + f * 0.019, -0.095, 0.015); fg.rotation.x = 0.9; hand.add(fg); }
    const th = mesh(new THREE.CylinderGeometry(0.013, 0.011, 0.06, 8), st, s * 0.042, -0.05, 0.022); th.rotation.z = s * 0.7; hand.add(th);
    h.add(q.ha, hand);
    h.add(q.th, mesh(segment(0.42, 0.088, 0.064), st, 0, -0.02, 0.005));
    const knee = mesh(new THREE.SphereGeometry(0.066, 20, 12), trim, 0, 0, 0.02); knee.scale.set(1, 1, 0.9); h.add(q.sn, knee);
    const fan = mesh(new THREE.CircleGeometry(0.06, 20), st, s * 0.055, 0, 0.01); fan.rotation.y = s * Math.PI / 2; h.add(q.sn, fan);
    h.add(q.sn, mesh(segment(0.42, 0.062, 0.045, { sz: 0.95 }), st, 0, -0.03, 0));
    h.add(q.ft, mesh(ellipsoid(0.057, 0.045, 0.135), st, 0, -0.02, 0.055));
  }
  if (cape) {
    const cg = new THREE.PlaneGeometry(0.56, 1.15, 8, 12);
    cg.translate(0, -0.575, 0);
    const p = cg.attributes.position;
    for (let i = 0; i < p.count; i++) { const x = p.getX(i), y = p.getY(i); p.setZ(i, -Math.cos(x * 5) * 0.03 + y * 0.04 - Math.sin(x * 9) * 0.015); p.setX(i, x * (1 - y * 0.3)); }
    cg.computeVertexNormals();
    const capeM = mesh(cg, mats.cloth(cape));
    const pivot = new THREE.Object3D(); pivot.position.set(0, 0.28, -0.15); h.chest.add(pivot);
    pivot.add(capeM); capeM.rotation.y = Math.PI;
    h.cape = pivot;
    h.hideInFirstPerson.push(pivot);
  }
  if (weapon === 'sword') { const w = longsword(); w.rotation.x = Math.PI / 2; w.position.set(0, -0.06, 0.02); h.add(h.R.ha, w); h.weapon = w; }
  if (weapon === 'halberd') { const w = halberd(); w.rotation.x = Math.PI / 2 - 0.2; w.position.set(0, -0.06, 0.02); h.add(h.R.ha, w); h.weapon = w; w.userData.tip = V(0, 1.75, 0); }
  if (shield) {
    model('kite_shield').then(sc => {
      const box = new THREE.Box3().setFromObject(sc), dim = box.getSize(new THREE.Vector3());
      const k = 0.85 / Math.max(dim.x, dim.y, dim.z);
      sc.scale.setScalar(k);
      sc.position.copy(box.getCenter(new THREE.Vector3()).multiplyScalar(-k));
      const holder = new THREE.Group(); holder.add(sc);
      // stand the shield on its point, face outwards from the forearm
      const tall = dim.y >= dim.x && dim.y >= dim.z ? 'y' : dim.x >= dim.z ? 'x' : 'z';
      if (tall === 'x') sc.parent.rotation.z = Math.PI / 2;
      if (tall === 'z') sc.parent.rotation.x = Math.PI / 2;
      const wrap = new THREE.Group(); wrap.add(holder);
      wrap.rotation.set(0, Math.PI / 2, 0);
      wrap.position.set(0.075, -0.14, 0.02);
      h.add(h.L.fo, wrap); h.shield = wrap;
    });
  }
  h.root.scale.setScalar(scale);
  h.kind = 'slash';
  return h;
}

// ---------- humanoid monsters ----------
function clawsOn(s, h, P, bi, len = 0.09, n = 3) {
  for (const q of [h.L, h.R]) {
    const wr = P(q.ha);
    for (let i = 0; i < n; i++) {
      const x = (i - (n - 1) / 2) * 0.022;
      s.chain([wr.clone().add(V(x, -0.11, 0.01)), wr.clone().add(V(x, -0.11 - len * 0.6, 0.03)), wr.clone().add(V(x * 1.1, -0.11 - len, 0.06))], 0.011, 0.002, { bone: bi(q.ha), mat: 1, k: 0.006 });
    }
  }
}

export function demon() {
  const h = new Humanoid({ hunch: 0.15, shoulderW: 0.22 });
  const extra = (s, P, bi) => {
    const hd = P(h.head);
    for (const sx of [-1, 1]) s.chain([hd.clone().add(V(sx * 0.06, 0.17, 0.02)), hd.clone().add(V(sx * 0.13, 0.25, -0.02)), hd.clone().add(V(sx * 0.16, 0.33, -0.1)), hd.clone().add(V(sx * 0.14, 0.36, -0.18))], 0.03, 0.004, { bone: bi(h.head), mat: 1, k: 0.015 });
    clawsOn(s, h, P, bi, 0.08);
    // tail
    let prev = h.hips, pts = [P(h.hips).add(V(0, -0.05, -0.1))], bonesT = [];
    for (let i = 0; i < 7; i++) { const b = h.J(prev, 0, i ? 0.01 : -0.05, i ? -0.13 : -0.1, 'tail' + i); prev = b; bonesT.push(b); pts.push(P(b)); }
    h.tail = bonesT;
    for (let i = 0; i < 7; i++) s.capsule(pts[i], pts[i + 1], 0.045 - i * 0.005, 0.04 - i * 0.005, { bone: bi(bonesT[i]), k: 0.02 });
    s.chain([pts[7], pts[7].clone().add(V(0.04, 0, -0.06)), pts[7].clone().add(V(0, 0, -0.14))], 0.03, 0.003, { bone: bi(bonesT[6]), mat: 1, k: 0.01 });
  };
  h.sculptBody({ chestW: 0.2, chestD: 0.13, waist: 0.14, shoulder: 0.07, arm: [0.066, 0.056, 0.04], leg: [0.105, 0.07, 0.046], muscle: 1, mouth: 0.6, foot: 'paw' },
    [skinM(0x6e1a12, { height: 'wrinkle', scale: 3, bump: 0.0025, sheen: 0.5 }), hornM()], extra);
  h.eyes(0xff7010, 9);
  h.kind = 'claw';
  return h;
}

export function ghoul() {
  const h = new Humanoid({ hunch: 0.5, armLen: 0.34, shoulderW: 0.18 });
  const extra = (s, P, bi) => {
    const c = P(h.chest);
    for (let i = 0; i < 5; i++) for (const sx of [-1, 1]) s.capsule(c.clone().add(V(sx * 0.03, 0.2 - i * 0.045, 0.1)), c.clone().add(V(sx * 0.14, 0.17 - i * 0.045, 0.03)), 0.013, 0.012, { bone: bi(h.chest), k: 0.012 });
    clawsOn(s, h, P, bi, 0.13, 4);
  };
  h.sculptBody({ chestW: 0.15, chestD: 0.1, waist: 0.1, hipW: 0.12, shoulder: 0.05, neck: 0.04, arm: [0.034, 0.028, 0.022], leg: [0.058, 0.04, 0.03], mouth: 1, headScale: 0.95, nose: 0.5, foot: 'paw' },
    [skinM(0x6f7660, { height: 'wrinkle', scale: 3.5, bump: 0.0025, sheen: 0.25 }), hornM()], extra);
  h.eyes(0xd8ff40, 7, 0.011, 0.95);
  const rag = mesh(lathe([[0.15, 0.05], [0.17, -0.1], [0.22, -0.34]], { sz: 0.8, segs: 14 }), mats.cloth('#3a3226'));
  h.add(h.hips, rag);
  h.kind = 'claw';
  return h;
}

export function maniac() {
  const h = new Humanoid({ hunch: 0.22 });
  h.sculptBody({ chestW: 0.18, waist: 0.15, arm: [0.046, 0.04, 0.03], leg: [0.075, 0.052, 0.038], mouth: 0.5 },
    [skinM(0xa87862, { height: 'skin', scale: 4, bump: 0.0025, sheen: 0.4 })]);
  h.eyes(0xf0f0e8, 0, 0.012);
  const rags = mats.cloth('#4a3a28');
  h.add(h.chest, mesh(lathe([[0.001, 0.34], [0.12, 0.31], [0.215, 0.22], [0.215, 0.05], [0.185, -0.12]], { sz: 0.78, segs: 24 }), rags));
  h.add(h.hips, mesh(lathe([[0.17, 0.06], [0.205, -0.1], [0.24, -0.42]], { sz: 0.82, segs: 14 }), rags));
  h.add(h.head, mesh(new THREE.SphereGeometry(0.135, 20, 12, 0, Math.PI * 2, 0, Math.PI * 0.58), mats.cloth('#2a221a'), 0, 0.11, -0.02));
  const w = axe(0.8, 1.3); w.rotation.x = Math.PI / 2; w.position.set(0, -0.06, 0.02); h.add(h.R.ha, w); h.weapon = w; w.userData.tip = V(0.12, 0.62, 0);
  h.kind = 'slash';
  return h;
}

export function golem() {
  const h = new Humanoid({ hunch: 0.12, armLen: 0.33, shoulderW: 0.26 });
  const R = pbrMaps('rock_face_03');
  const rockSkin = skinMaterial({ color: 0xb8aea4, map: R.map, height: heightTex('rock'), scale: 1.6, bump: 0.012, roughness: 0.95 });
  h.sculptBody({ chestW: 0.25, chestD: 0.17, waist: 0.17, hipW: 0.17, shoulder: 0.09, neck: 0.07, arm: [0.075, 0.07, 0.065], leg: [0.1, 0.085, 0.075], hand: 1.6, muscle: 1.5, headScale: 0.9, foot: 'paw' },
    [rockSkin], null, { noise: 0.022, noiseScale: 9, cell: 0.02 });
  h.eyes(0x40c0ff, 9, 0.016, 0.9);
  h.add(h.chest, mesh(new THREE.SphereGeometry(0.05, 14, 10), mats.glow(0x40c0ff, 6), 0, 0.15, 0.17));
  h.root.scale.setScalar(1.3);
  h.kind = 'smash';
  return h;
}

export function monkey() {
  const h = new Humanoid({ hunch: 0.55, armLen: 0.36, legLen: 0.34 });
  const extra = (s, P, bi) => {
    const hd = P(h.head);
    s.ellipsoid(hd.clone().add(V(0, 0.06, 0.08)), V(0.06, 0.045, 0.05), { bone: bi(h.head), mat: 1, k: 0.03 });
    for (const sx of [-1, 1]) s.ellipsoid(hd.clone().add(V(sx * 0.1, 0.11, -0.01)), V(0.012, 0.035, 0.03), { bone: bi(h.head), mat: 1, k: 0.01 });
    s.ellipsoid(P(h.spine).add(V(0, 0.04, 0.05)), V(0.14, 0.15, 0.12), { bone: bi(h.spine), k: 0.06 });
    let prev = h.hips, pts = [P(h.hips).add(V(0, -0.04, -0.1))], bonesT = [];
    for (let i = 0; i < 8; i++) { const b = h.J(prev, 0, i ? 0.02 : 0, -0.09, 'tail' + i); b.rotation.x = -0.2; prev = b; bonesT.push(b); }
    h.root.updateMatrixWorld(true);
    for (const b of bonesT) pts.push(P(b));
    for (let i = 0; i < 8; i++) s.capsule(pts[i], pts[i + 1], 0.028, 0.024, { bone: bi(bonesT[i]), k: 0.015 });
    h.tail = bonesT;
  };
  h.sculptBody({ chestW: 0.17, chestD: 0.14, waist: 0.15, shoulder: 0.06, arm: [0.045, 0.038, 0.03], leg: [0.065, 0.045, 0.035], hand: 1.2, headScale: 1.05, nose: 0.3, mouth: 0.3, foot: 'paw' },
    [skinM(0x4a3018, { height: 'fur', scale: 4, bump: 0.0025, sheen: 1, roughness: 0.85, detail: 'fur' }), skinM(0xb08868, { scale: 4, bump: 0.0025 })], extra);
  h.eyes(0x1a0e06, 0, 0.012, 1.05);
  h.root.scale.setScalar(0.72);
  h.kind = 'claw';
  return h;
}

export function warrior() {
  const h = knight({ steelTint: 0x3a3c42, cloth: '#5a0e0e', emblem: false, cape: null, plume: null, helm: 'kettle' });
  h.kind = 'slash';
  return h;
}
export function guard() {
  const h = knight({ steelTint: 0xb8b4a8, cloth: '#2a2a2a', emblem: false, cape: '#d8b040', plume: 0xd8b040, gold: true, weapon: 'halberd', shield: false, scale: 1.3 });
  h.kind = 'thrust';
  return h;
}
