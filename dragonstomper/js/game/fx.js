// Visual effects: particles (sparks, blood, dust, embers, magic, fire), sword trails,
// floating damage numbers, screen shake and hit-stop.
import * as THREE from 'three';
import { softDot, flame } from '../core/texgen.js';
import { gfx } from '../core/gfx.js';

const MAX = 4000;
export class Particles {
  constructor(scene) {
    const geo = new THREE.BufferGeometry();
    this.pos = new Float32Array(MAX * 3); this.col = new Float32Array(MAX * 4); this.size = new Float32Array(MAX);
    geo.setAttribute('position', new THREE.BufferAttribute(this.pos, 3).setUsage(THREE.DynamicDrawUsage));
    geo.setAttribute('color', new THREE.BufferAttribute(this.col, 4).setUsage(THREE.DynamicDrawUsage));
    geo.setAttribute('size', new THREE.BufferAttribute(this.size, 1).setUsage(THREE.DynamicDrawUsage));
    const mk = (blending, map) => new THREE.ShaderMaterial({
      uniforms: { map: { value: map }, scale: { value: 1 } },
      vertexShader: `attribute float size; attribute vec4 color; varying vec4 vC; uniform float scale;
        void main() { vC = color; vec4 mv = modelViewMatrix * vec4(position, 1.0); gl_PointSize = size * scale / -mv.z; gl_Position = projectionMatrix * mv; }`,
      fragmentShader: `uniform sampler2D map; varying vec4 vC; void main() { vec4 t = texture2D(map, gl_PointCoord); gl_FragColor = vec4(vC.rgb, vC.a * t.a); if (gl_FragColor.a < 0.003) discard; }`,
      transparent: true, depthWrite: false, blending,
    });
    this.add = new THREE.Points(geo, mk(THREE.AdditiveBlending, softDot()));
    const geo2 = geo.clone();
    this.pos2 = geo2.attributes.position.array; this.col2 = geo2.attributes.color.array; this.size2 = geo2.attributes.size.array;
    this.norm = new THREE.Points(geo2, mk(THREE.NormalBlending, softDot()));
    this.add.frustumCulled = this.norm.frustumCulled = false;
    this.add.renderOrder = 5; this.norm.renderOrder = 4;
    scene.add(this.add, this.norm);
    this.p = [];
  }
  emit(o) { if (this.p.length < MAX * 2) this.p.push(o); }
  // kinds: spark, blood, dust, ember, magic, fire, smoke, gold
  burst(pos, kind, n = 20, dir = null, spread = 1) {
    for (let i = 0; i < n; i++) {
      const v = new THREE.Vector3((Math.random() - 0.5), Math.random() - 0.3, (Math.random() - 0.5)).normalize();
      const P = { x: pos.x, y: pos.y, z: pos.z, vx: 0, vy: 0, vz: 0, life: 1, t: 0, size: 0.1, g: 0, drag: 0.98, add: true, r: 1, gg: 1, b: 1, a: 1, grow: 0, fade: 1 };
      const sp = (s) => { P.vx = v.x * s; P.vy = v.y * s; P.vz = v.z * s; if (dir) { P.vx = P.vx * spread + dir.x * s; P.vy = P.vy * spread + dir.y * s; P.vz = P.vz * spread + dir.z * s; } };
      switch (kind) {
        case 'spark': sp(4 + Math.random() * 6); P.life = 0.25 + Math.random() * 0.35; P.size = 0.05; P.g = 9; P.r = 1; P.gg = 0.75; P.b = 0.35; P.a = 1; P.bright = 5; break;
        case 'blood': sp(1.5 + Math.random() * 3); P.life = 0.5 + Math.random() * 0.4; P.size = 0.06 + Math.random() * 0.05; P.g = 9.8; P.add = false; P.r = 0.35; P.gg = 0.02; P.b = 0.02; P.a = 0.9; break;
        case 'ichor': sp(1.5 + Math.random() * 3); P.life = 0.6; P.size = 0.07; P.g = 9.8; P.add = false; P.r = 0.2; P.gg = 0.45; P.b = 0.1; P.a = 0.9; break;
        case 'dust': sp(0.5 + Math.random()); P.vy = Math.abs(P.vy) * 0.5 + 0.3; P.life = 0.9 + Math.random() * 0.8; P.size = 0.35 + Math.random() * 0.3; P.grow = 0.8; P.add = false; P.r = 0.55; P.gg = 0.5; P.b = 0.42; P.a = 0.35; P.drag = 0.92; break;
        case 'smoke': sp(0.3); P.vy = 0.6 + Math.random() * 0.6; P.life = 2 + Math.random(); P.size = 0.5; P.grow = 1.2; P.add = false; P.r = 0.12; P.gg = 0.11; P.b = 0.1; P.a = 0.4; P.drag = 0.97; break;
        case 'ember': sp(0.6 + Math.random()); P.vy = 0.8 + Math.random() * 1.5; P.life = 1 + Math.random() * 1.5; P.size = 0.04; P.g = -0.3; P.r = 1; P.gg = 0.5; P.b = 0.15; P.bright = 4; break;
        case 'magic': sp(1 + Math.random() * 2); P.life = 0.8 + Math.random() * 0.6; P.size = 0.09; P.g = -1.5; P.r = 0.5 + Math.random() * 0.5; P.gg = 0.6; P.b = 1; P.bright = 4; break;
        case 'heal': sp(0.4); P.vy = 1 + Math.random(); P.life = 1.2; P.size = 0.08; P.g = -0.5; P.r = 0.6; P.gg = 1; P.b = 0.7; P.bright = 3; break;
        case 'gold': sp(2 + Math.random() * 2); P.vy = 3 + Math.random() * 2; P.life = 0.9; P.size = 0.07; P.g = 9; P.r = 1; P.gg = 0.8; P.b = 0.3; P.bright = 3; break;
        case 'fire': sp(1 + Math.random() * 2); P.life = 0.5 + Math.random() * 0.5; P.size = 0.6 + Math.random() * 0.5; P.grow = 2.4; P.drag = 0.94; P.g = -2; P.r = 1; P.gg = 0.45 + Math.random() * 0.25; P.b = 0.12; P.bright = 3; break;
      }
      this.emit(P);
    }
  }
  update(dt) {
    let na = 0, nn = 0;
    for (let i = this.p.length - 1; i >= 0; i--) {
      const P = this.p[i];
      P.t += dt;
      if (P.t >= P.life) { this.p[i] = this.p[this.p.length - 1]; this.p.pop(); continue; }
      P.vy -= P.g * dt; const d = Math.pow(P.drag, dt * 60);
      P.vx *= d; P.vy *= d; P.vz *= d;
      P.x += P.vx * dt; P.y += P.vy * dt; P.z += P.vz * dt;
      if (P.floor != null && P.y < P.floor) { P.y = P.floor; P.vy *= -0.2; P.vx *= 0.5; P.vz *= 0.5; }
      const k = P.t / P.life, fade = 1 - k;
      const sz = P.size * (1 + P.grow * k) * 400;
      const br = P.bright || 1;
      if (P.add && na < MAX) {
        const j = na++;
        this.pos[j * 3] = P.x; this.pos[j * 3 + 1] = P.y; this.pos[j * 3 + 2] = P.z;
        this.col[j * 4] = P.r * br; this.col[j * 4 + 1] = P.gg * br; this.col[j * 4 + 2] = P.b * br; this.col[j * 4 + 3] = P.a * fade;
        this.size[j] = sz;
      } else if (!P.add && nn < MAX) {
        const j = nn++;
        this.pos2[j * 3] = P.x; this.pos2[j * 3 + 1] = P.y; this.pos2[j * 3 + 2] = P.z;
        this.col2[j * 4] = P.r; this.col2[j * 4 + 1] = P.gg; this.col2[j * 4 + 2] = P.b; this.col2[j * 4 + 3] = P.a * fade;
        this.size2[j] = sz;
      }
    }
    const s = (gfx.renderSize?.h || innerHeight) / 1000;
    this.add.material.uniforms.scale.value = this.norm.material.uniforms.scale.value = s;
    this.add.geometry.setDrawRange(0, na); this.norm.geometry.setDrawRange(0, nn);
    for (const g of [this.add.geometry, this.norm.geometry]) for (const a of ['position', 'color', 'size']) g.attributes[a].needsUpdate = true;
  }
}

// A ribbon behind the sword tip
export class Trail {
  constructor(scene, n = 14) {
    this.n = n; this.pts = [];
    const geo = new THREE.BufferGeometry();
    this.pos = new Float32Array(n * 2 * 3); this.alpha = new Float32Array(n * 2);
    geo.setAttribute('position', new THREE.BufferAttribute(this.pos, 3).setUsage(THREE.DynamicDrawUsage));
    geo.setAttribute('alpha', new THREE.BufferAttribute(this.alpha, 1).setUsage(THREE.DynamicDrawUsage));
    const idx = []; for (let i = 0; i < n - 1; i++) { const a = i * 2; idx.push(a, a + 1, a + 2, a + 1, a + 3, a + 2); }
    geo.setIndex(idx);
    this.mesh = new THREE.Mesh(geo, new THREE.ShaderMaterial({
      vertexShader: 'attribute float alpha; varying float vA; void main() { vA = alpha; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }',
      fragmentShader: 'varying float vA; void main() { gl_FragColor = vec4(vec3(1.0, 0.95, 0.85) * 1.6, vA * 0.45); }',
      transparent: true, depthWrite: false, side: THREE.DoubleSide, blending: THREE.AdditiveBlending,
    }));
    this.mesh.frustumCulled = false;
    scene.add(this.mesh);
  }
  update(on, base, tip) {
    if (on) { this.pts.unshift([base.clone(), tip.clone()]); } else this.pts.unshift(this.pts[0] || [base.clone(), tip.clone()]);
    if (this.pts.length > this.n) this.pts.length = this.n;
    for (let i = 0; i < this.n; i++) {
      const p = this.pts[Math.min(i, this.pts.length - 1)];
      this.pos.set([p[0].x, p[0].y, p[0].z, p[1].x, p[1].y, p[1].z], i * 6);
      const a = on && i < this.pts.length ? (1 - i / this.n) : 0;
      this.alpha[i * 2] = a * 0.2; this.alpha[i * 2 + 1] = a;
    }
    if (!on) this.pts.length = Math.max(0, this.pts.length - 2);
    this.mesh.geometry.attributes.position.needsUpdate = true; this.mesh.geometry.attributes.alpha.needsUpdate = true;
  }
}

// Fire breath: one GPU particle system of flame (white-hot core cooling through orange
// to dull red, in HDR so the bloom catches it) plus a layer of rolling smoke behind it.
const FIRE_VS = `
attribute float aLife; attribute float aSize; attribute float aRot;
varying float vLife; varying float vRot;
uniform float uScale;
void main() {
  vLife = aLife; vRot = aRot;
  vec4 mv = modelViewMatrix * vec4(position, 1.0);
  gl_PointSize = aLife < 0.0 ? 0.0 : aSize * uScale / max(0.1, -mv.z);
  gl_Position = projectionMatrix * mv;
}`;
const FIRE_FS = `
uniform sampler2D uMap; uniform float uSmoke;
varying float vLife; varying float vRot;
void main() {
  if (vLife < 0.0) discard;
  vec2 p = gl_PointCoord - 0.5;
  float c = cos(vRot), s = sin(vRot);
  p = mat2(c, -s, s, c) * p + 0.5;
  vec4 t = texture2D(uMap, p);
  t.a *= smoothstep(0.5, 0.15, length(gl_PointCoord - 0.5)); // round, soft edges whatever the texture
  float l = vLife;
  float fadeIn = smoothstep(0.0, 0.08, l);
  if (uSmoke > 0.5) {
    float a = t.a * fadeIn * (1.0 - l) * 0.38;
    gl_FragColor = vec4(vec3(0.05, 0.045, 0.04) + 0.06 * (1.0 - l), a);
  } else {
    vec3 hot = vec3(3.2, 2.1, 0.9), mid = vec3(2.4, 0.75, 0.12), cool = vec3(0.7, 0.1, 0.02);
    vec3 col = mix(hot, mid, smoothstep(0.0, 0.18, l));
    col = mix(col, cool, smoothstep(0.35, 0.85, l));
    float a = t.a * fadeIn * (1.0 - smoothstep(0.6, 1.0, l)) * 0.55;
    gl_FragColor = vec4(col * a, a);
  }
}`;
class PointPool {
  constructor(scene, n, smoke) {
    this.n = n; this.free = [...Array(n).keys()]; this.p = []; this.data = [];
    const g = new THREE.BufferGeometry();
    this.pos = new Float32Array(n * 3); this.life = new Float32Array(n).fill(-1); this.size = new Float32Array(n); this.rot = new Float32Array(n);
    g.setAttribute('position', new THREE.BufferAttribute(this.pos, 3).setUsage(THREE.DynamicDrawUsage));
    g.setAttribute('aLife', new THREE.BufferAttribute(this.life, 1).setUsage(THREE.DynamicDrawUsage));
    g.setAttribute('aSize', new THREE.BufferAttribute(this.size, 1).setUsage(THREE.DynamicDrawUsage));
    g.setAttribute('aRot', new THREE.BufferAttribute(this.rot, 1).setUsage(THREE.DynamicDrawUsage));
    this.geo = g;
    const m = new THREE.ShaderMaterial({
      uniforms: { uMap: { value: flame() }, uScale: { value: 600 }, uSmoke: { value: smoke ? 1 : 0 } },
      vertexShader: FIRE_VS, fragmentShader: FIRE_FS, transparent: true, depthWrite: false,
      blending: smoke ? THREE.NormalBlending : THREE.CustomBlending,
    });
    if (!smoke) { m.blendSrc = THREE.OneFactor; m.blendDst = THREE.OneFactor; } // additive, premultiplied
    this.points = new THREE.Points(g, m); this.points.frustumCulled = false;
    this.points.renderOrder = smoke ? 1 : 2;
    // world-size particles: pixels per metre at unit distance for this viewport
    const v = new THREE.Vector2();
    this.points.onBeforeRender = (r, sc, cam) => { m.uniforms.uScale.value = r.getDrawingBufferSize(v).y * 0.5 / Math.tan(THREE.MathUtils.degToRad(cam.fov) / 2); };
    scene.add(this.points);
  }
  spawn(o) { if (!this.free.length) return; const i = this.free.pop(); o.i = i; o.t = 0; this.data.push(o); }
  update(dt, step) {
    for (let k = this.data.length - 1; k >= 0; k--) {
      const f = this.data[k], i = f.i;
      f.t += dt;
      if (f.t >= f.life) { this.life[i] = -1; this.free.push(i); this.data.splice(k, 1); f.dead = true; continue; }
      step(f, dt);
      this.pos[i * 3] = f.p.x; this.pos[i * 3 + 1] = f.p.y; this.pos[i * 3 + 2] = f.p.z;
      this.life[i] = f.t / f.life; this.size[i] = f.size; this.rot[i] = f.rot;
    }
    for (const a of ['position', 'aLife', 'aSize', 'aRot']) this.geo.attributes[a].needsUpdate = true;
  }
}
export class FireStream {
  constructor(scene) {
    this.fire = new PointPool(scene, 600, false);
    this.smoke = new PointPool(scene, 300, true);
  }
  emit(origin, dir, rate, dt) {
    let k = rate * dt * 2.5;
    while (k > 0) {
      if (k < 1 && Math.random() > k) break;
      k--;
      const d = dir.clone().add(new THREE.Vector3((Math.random() - 0.5) * 0.22, (Math.random() - 0.5) * 0.16, (Math.random() - 0.5) * 0.22)).normalize();
      this.fire.spawn({ p: origin.clone(), v: d.multiplyScalar(17 + Math.random() * 7), life: 0.55 + Math.random() * 0.4, rot: Math.random() * 6.28, spin: (Math.random() - 0.5) * 3, size: 0.5 });
    }
  }
  update(dt) {
    this.fire.update(dt, (f, dt) => {
      f.v.multiplyScalar(Math.pow(0.3, dt)); f.v.y += dt * 3;
      f.p.addScaledVector(f.v, dt);
      const k = f.t / f.life;
      f.size = 0.5 + k * 3.6; f.rot += f.spin * dt;
      if (k > 0.55 && !f.smoked && Math.random() < 0.5) { f.smoked = true; this.smoke.spawn({ p: f.p.clone(), v: f.v.clone().multiplyScalar(0.5).add(new THREE.Vector3(0, 1.2, 0)), life: 1.6 + Math.random(), rot: Math.random() * 6.28, spin: (Math.random() - 0.5), size: 2.5 }); }
    });
    this.smoke.update(dt, (f, dt) => {
      f.v.multiplyScalar(Math.pow(0.5, dt)); f.v.y += dt * 0.8;
      f.p.addScaledVector(f.v, dt);
      f.size = 2.5 + (f.t / f.life) * 4; f.rot += f.spin * dt;
    });
  }
  // points the flames have reached, for hit tests
  get points() { return this.fire.data.filter(f => f.t > 0.08 && f.t / f.life < 0.75).map(f => f.p); }
  get list() { return this.fire.data; }
}

// Damage numbers and labels in the DOM, projected from 3D
const layer = () => document.getElementById('floaters');
const floats = [];
export function floatText(pos, text, cls = '') {
  const el = document.createElement('div');
  el.className = 'float ' + cls; el.textContent = text;
  layer().appendChild(el);
  floats.push({ el, pos: pos.clone(), t: 0, vy: 1.2 + Math.random() * 0.4, dx: (Math.random() - 0.5) * 30 });
}
export function updateFloats(dt, camera) {
  const v = new THREE.Vector3();
  for (let i = floats.length - 1; i >= 0; i--) {
    const f = floats[i];
    f.t += dt; f.pos.y += f.vy * dt;
    if (f.t > 1.3) { f.el.remove(); floats.splice(i, 1); continue; }
    v.copy(f.pos).project(camera);
    if (v.z > 1) { f.el.style.opacity = 0; continue; }
    f.el.style.transform = `translate(${(v.x * 0.5 + 0.5) * innerWidth + f.dx * f.t}px, ${(-v.y * 0.5 + 0.5) * innerHeight}px) translate(-50%, -50%) scale(${1 + Math.max(0, 0.3 - f.t)})`;
    f.el.style.opacity = Math.min(1, (1.3 - f.t) * 2);
  }
}
export function clearFloats() { for (const f of floats) f.el.remove(); floats.length = 0; }

export const shake = { amt: 0, add(a) { this.amt = Math.min(1.2, this.amt + a); } };
export const hitstop = { t: 0, add(s) { this.t = Math.max(this.t, s); } };
