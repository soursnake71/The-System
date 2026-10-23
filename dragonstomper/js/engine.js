// Renderer, camera, hero, collisions, particles and saving - shared by the three areas.
import * as THREE from '../lib/three.module.min.js';
import { artMesh, voxMat } from './voxel.js';
import { input } from './input.js';
import { drawText } from './ui.js';
import { sfx } from './audio.js';

export const G = {
  THREE, renderer: null, scene: null, camera: null, clock: new THREE.Clock(),
  hero: null, p: null, ui: null, area: null, colliders: [], time: 0, shakeAmt: 0,
  camOffset: new THREE.Vector3(0, 12, 14), camLook: new THREE.Vector3(0, 0.8, -2),
  walkSpeed: 7, frozen: false, anims: [], battleMeshes: [],
};

export function initEngine(canvas) {
  const r = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
  r.setPixelRatio(Math.min(devicePixelRatio, 2));
  r.shadowMap.enabled = true;
  r.shadowMap.type = THREE.PCFSoftShadowMap;
  r.outputColorSpace = THREE.SRGBColorSpace;
  G.renderer = r;
  G.camera = new THREE.PerspectiveCamera(55, 1, 0.1, 400);
  resize();
  addEventListener('resize', resize);
  G.fx = new Particles();
}
function resize() {
  const w = innerWidth, h = innerHeight;
  G.renderer.setSize(w, h, false);
  G.camera.aspect = w / h;
  // keep a similar view on tall phones
  G.camera.fov = w < h ? 70 : 55;
  G.camera.updateProjectionMatrix();
  viewShift = NaN;
}

// A fresh scene with sky, fog and sun. The area adds its own stuff.
export function newScene({ sky = 0x8fb8e8, fog = [60, 190], ground = 0x505916, sun = 1.6, ambient = 0.9, hemiGround = 0x403810 } = {}) {
  // the hero lives on between areas: take him out before the old scene is thrown away
  if (G.hero) G.hero.parent?.remove(G.hero);
  if (G.scene) disposeScene(G.scene);
  const s = new THREE.Scene();
  s.background = new THREE.Color(sky);
  s.fog = new THREE.Fog(sky, fog[0], fog[1]);
  const hemi = new THREE.HemisphereLight(0xffffff, hemiGround, ambient);
  s.add(hemi);
  const dl = new THREE.DirectionalLight(0xfff2d6, sun);
  dl.position.set(-14, 26, 10);
  dl.castShadow = sun > 0;
  dl.shadow.mapSize.set(1024, 1024);
  const sc = dl.shadow.camera; sc.left = -30; sc.right = 30; sc.top = 30; sc.bottom = -30; sc.near = 1; sc.far = 80;
  dl.shadow.bias = -0.0008;
  s.add(dl); s.add(dl.target);
  G.sun = dl; G.hemi = hemi;
  G.scene = s;
  G.colliders = [];
  G.fx.attach(s);
  return s;
}
function disposeScene(s) {
  s.traverse(o => {
    if (o.geometry && !o.geometry.userData.keep) o.geometry.dispose();
    if (o.material && o.material !== voxMat && !o.material.userData?.keep) {
      if (o.material.map) o.material.map.dispose();
      o.material.dispose?.();
    }
  });
}

// ---------- the hero: a little voxel knight, with a sword arm that swings ----------
const HERO = [
  '...ww...',
  '..hhhh..',
  '..hssh..',
  '..hssh..',
  '...hh...',
  '.bbbbbb.',
  'sbbggbbs',
  's.bbbb.s',
  '..bbbb..',
  '..gggg..',
  '..l..l..',
  '..l..l..',
  '..l..l..',
  '.kk..kk.',
];
const HEROPAL = { w: '#fc5454', h: '#c0c0c0', s: '#fcbc74', b: '#4248c8', g: '#e8cc63', l: '#694d14', k: '#2a2a2a' };
const SWORD = ['.c.', '.c.', '.c.', '.c.', '.c.', 'ggg', '.k.'];
export function makeHero() {
  const g = new THREE.Group();
  const body = artMesh(HERO, HEROPAL, { w: 2, h: 4, s: 3, b: 4, g: 4, l: 2, k: 3 }, 0.32, 0.2, 0.3);
  g.add(body);
  const arm = new THREE.Group();
  const sword = artMesh(SWORD, { c: '#ececec', g: '#e8cc63', k: '#694d14' }, { c: 1, g: 1, k: 1 }, 0.14, 0.2, 0.14);
  sword.position.set(0, -0.3, 0);
  arm.add(sword);
  arm.position.set(1.25, 1.55, 0.25);
  g.add(arm);
  const shield = new THREE.Mesh(new THREE.BoxGeometry(0.15, 0.9, 0.7), new THREE.MeshLambertMaterial({ color: 0xa33915 }));
  shield.position.set(-1.25, 1.25, 0.1); shield.visible = false; shield.castShadow = true;
  g.add(shield);
  g.userData = { body, arm, shield, swing: 0, bob: 0, hurt: 0 };
  G.hero = g;
  return g;
}
export function swing() { G.hero.userData.swing = 1; sfx('swing'); }

// ---------- movement and collisions ----------
export function addCollider(x, z, r, data = null) { const c = { x, z, r, data }; G.colliders.push(c); return c; }
export function removeCollider(c) { const i = G.colliders.indexOf(c); if (i >= 0) G.colliders.splice(i, 1); }

let stepT = 0;
export function moveHero(dt, opts = {}) {
  const h = G.hero, u = h.userData;
  const speed = (opts.speed || G.walkSpeed) * (G.p.stuck ? 0 : 1);
  let vx = input.x * speed, vz = input.y * speed;
  const moving = !G.frozen && Math.hypot(vx, vz) > 0.1;
  let bumped = null;
  if (moving) {
    let nx = h.position.x + vx * dt, nz = h.position.z + vz * dt;
    for (const c of G.colliders) {
      const dx = nx - c.x, dz = nz - c.z, d = Math.hypot(dx, dz), m = c.r + 0.8;
      if (d < m) {
        if (!bumped && c.data) bumped = c;
        if (c.soft) continue;
        const k = (m - d) / (d || 1);
        nx += dx * k; nz += dz * k;
      }
    }
    if (opts.clamp) [nx, nz] = opts.clamp(nx, nz);
    h.position.x = nx; h.position.z = nz;
    const want = Math.atan2(vx, vz);
    let da = want - h.rotation.y;
    da = Math.atan2(Math.sin(da), Math.cos(da));
    h.rotation.y += da * Math.min(1, dt * 12);
    u.bob += dt * 12;
    stepT += dt;
    if (stepT > 0.28) { stepT = 0; sfx('step'); }
  } else u.bob *= 0.8;
  if (opts.ground) h.position.y = opts.ground(h.position.x, h.position.z);
  return bumped;
}

export function animateHero(dt) {
  const u = G.hero.userData;
  u.body.position.y = Math.abs(Math.sin(u.bob)) * 0.18;
  u.body.rotation.z = Math.sin(u.bob) * 0.05;
  if (u.swing > 0) {
    u.swing = Math.max(0, u.swing - dt * 3.5);
    const s = u.swing;
    u.arm.rotation.x = -Math.sin(s * Math.PI) * 2.2;
  } else u.arm.rotation.x = Math.sin(u.bob) * 0.4;
  if (u.hurt > 0) {
    u.hurt -= dt;
    G.hero.visible = Math.floor(u.hurt * 20) % 2 === 0;
  } else G.hero.visible = true;
  u.shield.visible = G.p.inv.SHIELD > 0;
}

// ---------- camera ----------
const tmp = new THREE.Vector3(), look = new THREE.Vector3();
// centre the picture in the part of the screen the message window doesn't cover
let viewShift = 0;
function fitView() {
  const band = document.getElementById('band'), box = document.getElementById('boxcv');
  const h = innerHeight;
  const r = (getComputedStyle(band).top !== 'auto' && band.getBoundingClientRect().top < h / 2) ? box.getBoundingClientRect() : band.getBoundingClientRect();
  let top = 0, bottom = h;
  if (r.height) { if (r.top < h / 2) top = r.bottom; else bottom = r.top; }
  const s = Math.round((top + bottom) / 2 - h / 2);
  if (s !== viewShift) {
    viewShift = s;
    G.camera.setViewOffset(innerWidth, h, 0, -s, innerWidth, h);
  }
}
export function updateCamera(dt, focus = null, snap = false) {
  fitView();
  const h = G.hero.position;
  const target = focus || h;
  tmp.copy(target).add(G.camOffset);
  look.copy(target).add(G.camLook);
  const k = snap ? 1 : 1 - Math.pow(0.002, dt);
  G.camera.position.lerp(tmp, k);
  if (G.shakeAmt > 0) {
    G.camera.position.x += (Math.random() - 0.5) * G.shakeAmt;
    G.camera.position.y += (Math.random() - 0.5) * G.shakeAmt;
    G.shakeAmt = Math.max(0, G.shakeAmt - dt * 2);
  }
  G.camLookCur = G.camLookCur || look.clone();
  G.camLookCur.lerp(look, k);
  G.camera.lookAt(G.camLookCur);
  if (G.sun) {
    G.sun.position.set(h.x - 14, h.y + 26, h.z + 10);
    G.sun.target.position.set(h.x, h.y, h.z - 4);
  }
}
export const shake = (a) => { G.shakeAmt = Math.max(G.shakeAmt, a); };

// ---------- particles: little voxel bits ----------
class Particles {
  constructor() {
    this.max = 600;
    this.mesh = new THREE.InstancedMesh(new THREE.BoxGeometry(1, 1, 1), new THREE.MeshBasicMaterial({ vertexColors: false }), this.max);
    this.mesh.geometry.userData.keep = true; this.mesh.material.userData = { keep: true };
    this.mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
    this.mesh.frustumCulled = false;
    this.p = [];
    this.m = new THREE.Matrix4(); this.c = new THREE.Color();
    for (let i = 0; i < this.max; i++) this.mesh.setColorAt(i, this.c.set(0xffffff));
  }
  attach(scene) { scene.add(this.mesh); this.p.length = 0; }
  burst(pos, color, n = 20, { speed = 6, up = 6, size = 0.18, life = 1, gravity = 18, spread = 1 } = {}) {
    for (let i = 0; i < n && this.p.length < this.max; i++) {
      const a = Math.random() * Math.PI * 2, s = speed * (0.4 + Math.random() * 0.6);
      this.p.push({
        x: pos.x + (Math.random() - 0.5) * spread, y: pos.y + Math.random() * spread, z: pos.z + (Math.random() - 0.5) * spread,
        vx: Math.cos(a) * s, vy: up * (0.5 + Math.random()), vz: Math.sin(a) * s,
        life, t: life, size: size * (0.6 + Math.random() * 0.8), col: new THREE.Color(Array.isArray(color) ? color[i % color.length] : color), g: gravity,
      });
    }
  }
  update(dt) {
    let n = 0;
    for (let i = this.p.length - 1; i >= 0; i--) {
      const q = this.p[i];
      q.t -= dt;
      if (q.t <= 0) { this.p.splice(i, 1); continue; }
      q.vy -= q.g * dt; q.x += q.vx * dt; q.y += q.vy * dt; q.z += q.vz * dt;
      if (q.y < (q.floor ?? -100)) { q.y = q.floor; q.vy *= -0.3; q.vx *= 0.7; q.vz *= 0.7; }
    }
    for (const q of this.p) {
      const s = q.size * Math.min(1, q.t / q.life * 2);
      this.m.makeScale(s, s, s); this.m.setPosition(q.x, q.y, q.z);
      this.mesh.setMatrixAt(n, this.m); this.mesh.setColorAt(n, q.col); n++;
    }
    this.mesh.count = n;
    this.mesh.instanceMatrix.needsUpdate = true;
    if (this.mesh.instanceColor) this.mesh.instanceColor.needsUpdate = true;
  }
}

// ---------- floating pixel-font text (damage numbers, pick-ups) ----------
const floaters = [];
export function floatText(pos, text, color = '#fff', size = 1) {
  const cv = document.createElement('canvas');
  cv.width = text.length * 4 + 2; cv.height = 7;
  const c = cv.getContext('2d');
  drawText(c, text, 2, 1, '#000');
  drawText(c, text, 1, 1, color);
  const tex = new THREE.CanvasTexture(cv);
  tex.magFilter = THREE.NearestFilter; tex.minFilter = THREE.NearestFilter; tex.colorSpace = THREE.SRGBColorSpace;
  const sp = new THREE.Sprite(new THREE.SpriteMaterial({ map: tex, transparent: true, depthTest: false, fog: false }));
  sp.scale.set(cv.width * 0.12 * size, cv.height * 0.12 * size, 1);
  sp.position.copy(pos); sp.renderOrder = 10;
  G.scene.add(sp);
  floaters.push({ sp, t: 1.4 });
}
function updateFloaters(dt) {
  for (let i = floaters.length - 1; i >= 0; i--) {
    const f = floaters[i];
    f.t -= dt; f.sp.position.y += dt * 1.6;
    f.sp.material.opacity = Math.min(1, f.t * 2);
    if (f.t <= 0) { f.sp.parent?.remove(f.sp); f.sp.material.map.dispose(); f.sp.material.dispose(); floaters.splice(i, 1); }
  }
}

export function stepWorld(dt) {
  G.fx.update(dt);
  updateFloaters(dt);
}

// ---------- saving (per-browser; a private window simply won't remember) ----------
const KEY = 'ds3d-save';
export function save(extra = {}) {
  try { localStorage.setItem(KEY, JSON.stringify({ p: G.p, ...extra, v: 1 })); } catch (e) {}
}
export function load() {
  try { const s = JSON.parse(localStorage.getItem(KEY)); return s && s.v === 1 ? s : null; } catch (e) { return null; }
}
export function wipe() { try { localStorage.removeItem(KEY); } catch (e) {} }

// seeded layout randomness, separate from the game's dice
export function mulberry(a) {
  return () => { a |= 0; a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
}
