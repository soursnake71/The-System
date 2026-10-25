// Procedural textures drawn on canvases: the surfaces Poly Haven doesn't have
// (hammered steel, chainmail, scaled hide, cloth, skin, fur) plus normal maps made from them.
import * as THREE from 'three';
import { ImprovedNoise } from 'three/addons/math/ImprovedNoise.js';

const perlin = new ImprovedNoise();
const cache = {};

export function fbm(x, y, z = 0, oct = 5) {
  let v = 0, a = 0.5, f = 1;
  for (let i = 0; i < oct; i++) { v += a * perlin.noise(x * f, y * f, z * f); f *= 2; a *= 0.5; }
  return v;
}

function canvas(n) { const c = document.createElement('canvas'); c.width = c.height = n; return c; }

// height function (0..1) -> normal map texture
function normalFrom(n, h, strength = 2) {
  const c = canvas(n), ctx = c.getContext('2d'), img = ctx.createImageData(n, n);
  const H = (x, y) => h[((y + n) % n) * n + ((x + n) % n)];
  for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
    const dx = (H(x + 1, y) - H(x - 1, y)) * strength, dy = (H(x, y + 1) - H(x, y - 1)) * strength;
    const l = Math.hypot(dx, dy, 1), i = (y * n + x) * 4;
    img.data[i] = (-dx / l * 0.5 + 0.5) * 255; img.data[i + 1] = (dy / l * 0.5 + 0.5) * 255; img.data[i + 2] = (1 / l * 0.5 + 0.5) * 255; img.data[i + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  return tex(c);
}
function tex(c, srgb = false, rep = 1) {
  const t = new THREE.CanvasTexture(c);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.repeat.set(rep, rep);
  t.colorSpace = srgb ? THREE.SRGBColorSpace : THREE.NoColorSpace;
  t.anisotropy = 8;
  return t;
}
function gray(n, f) {
  const c = canvas(n), ctx = c.getContext('2d'), img = ctx.createImageData(n, n), h = new Float32Array(n * n);
  for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
    const v = Math.max(0, Math.min(1, f(x / n, y / n, x, y)));
    h[y * n + x] = v;
    const i = (y * n + x) * 4; img.data[i] = img.data[i + 1] = img.data[i + 2] = v * 255; img.data[i + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  return { c, h };
}
// tileable noise: sample on a torus
const tile = (u, v, s, z = 0) => {
  const a = u * Math.PI * 2, b = v * Math.PI * 2;
  return fbm(Math.cos(a) * s / 6.28, Math.sin(a) * s / 6.28 + Math.cos(b) * s / 6.28, Math.sin(b) * s / 6.28 + z);
};

// Hammered, scratched steel: roughness and normal
export function steel() {
  if (cache.steel) return cache.steel;
  const n = 512;
  const scratches = new Float32Array(n * n);
  for (let k = 0; k < 260; k++) {
    let x = Math.random() * n, y = Math.random() * n;
    const a = Math.random() * Math.PI, len = 10 + Math.random() * 80;
    for (let s = 0; s < len; s++) { scratches[((y | 0) % n) * n + ((x | 0) % n)] = 1; x = (x + Math.cos(a) + n) % n; y = (y + Math.sin(a) + n) % n; }
  }
  const h = gray(n, (u, v, x, y) => 0.5 + tile(u, v, 10) * 0.6 + tile(u, v, 40, 3) * 0.15 - scratches[y * n + x] * 0.2);
  const r = gray(n, (u, v, x, y) => 0.28 + tile(u, v, 6, 7) * 0.3 + scratches[y * n + x] * 0.25);
  return (cache.steel = { normal: normalFrom(n, h.h, 1.2), rough: tex(r.c) });
}

// Chainmail rings
export function chainmail() {
  if (cache.mail) return cache.mail;
  const n = 256, rings = 8, s = n / rings;
  const h = gray(n, (u, v, x, y) => {
    let best = 0;
    for (let oy = -1; oy <= 1; oy++) for (let ox = -1; ox <= 1; ox++) {
      const row = Math.floor(y / s) + oy, off = row % 2 ? s / 2 : 0;
      const cx = (Math.floor((x - off) / s) + ox) * s + off + s / 2, cy = row * s + s / 2;
      const d = Math.hypot(x - cx, (y - cy) * 1.2) / (s * 0.55);
      best = Math.max(best, Math.max(0, 1 - Math.abs(d - 0.75) * 5));
    }
    return best;
  });
  return (cache.mail = { normal: normalFrom(n, h.h, 3), ao: tex(h.c) });
}

// Overlapping reptile scales
export function scales(base = '#3a5a2a', belly = '#a89a5a') {
  const key = 'scales' + base;
  if (cache[key]) return cache[key];
  const n = 512, rows = 16, s = n / rows;
  const hh = new Float32Array(n * n);
  const c = canvas(n), ctx = c.getContext('2d'), img = ctx.createImageData(n, n);
  const b = new THREE.Color(base), d = b.clone().multiplyScalar(0.45);
  for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
    const row = Math.floor(y / s), off = row % 2 ? s / 2 : 0;
    const cx = Math.floor((x - off) / s) * s + off + s / 2, cy = row * s;
    const dx = (x - cx) / (s * 0.55), dy = (y - cy) / s;
    const h = Math.max(0, 1 - dx * dx - (dy - 0.2) * (dy - 0.2) * 1.4);
    hh[y * n + x] = h;
    const k = 0.55 + h * 0.6 + tile(x / n, y / n, 20) * 0.25;
    const col = d.clone().lerp(b, Math.min(1, k));
    const i = (y * n + x) * 4;
    img.data[i] = col.r * 255; img.data[i + 1] = col.g * 255; img.data[i + 2] = col.b * 255; img.data[i + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  return (cache[key] = { map: tex(c, true), normal: normalFrom(n, hh, 2.5) });
}

// Woven cloth with an optional painted emblem
export function cloth(color = '#1d2f6e', emblem = null) {
  const key = 'cloth' + color + (emblem ? 'e' : '');
  if (cache[key]) return cache[key];
  const n = 512;
  const h = gray(n, (u, v, x, y) => 0.5 + 0.35 * Math.sin(x * 1.6) * Math.sin(y * 1.6) + tile(u, v, 30) * 0.2);
  const c = canvas(n), ctx = c.getContext('2d');
  ctx.fillStyle = color; ctx.fillRect(0, 0, n, n);
  const img = ctx.getImageData(0, 0, n, n);
  for (let i = 0; i < n * n; i++) { const k = 0.8 + h.h[i] * 0.35; img.data[i * 4] *= k; img.data[i * 4 + 1] *= k; img.data[i * 4 + 2] *= k; }
  ctx.putImageData(img, 0, 0);
  if (emblem) emblem(ctx, n);
  return (cache[key] = { map: tex(c, true), normal: normalFrom(n, h.h, 1.5) });
}

// Skin / hide with pores and mottling
export function skin(color = '#b0806a', mottle = 0.25, key = '') {
  const k = 'skin' + color + key;
  if (cache[k]) return cache[k];
  const n = 512;
  const h = gray(n, (u, v) => 0.5 + tile(u, v, 60) * 0.4 + tile(u, v, 12, 2) * 0.3);
  const c = canvas(n), ctx = c.getContext('2d'), img = ctx.createImageData(n, n);
  const b = new THREE.Color(color);
  for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
    const m = tile(x / n, y / n, 5, 9) * mottle;
    const i = (y * n + x) * 4, k2 = 0.9 + m + h.h[y * n + x] * 0.15;
    img.data[i] = Math.min(255, b.r * 255 * k2); img.data[i + 1] = Math.min(255, b.g * 255 * k2); img.data[i + 2] = Math.min(255, b.b * 255 * k2); img.data[i + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  return (cache[k] = { map: tex(c, true), normal: normalFrom(n, h.h, 1.6) });
}

// Fur: streaky strands
export function fur(color = '#5a4028') {
  const k = 'fur' + color;
  if (cache[k]) return cache[k];
  const n = 256;
  const h = gray(n, (u, v) => 0.5 + fbm(u * 60, v * 6, 0, 3) * 0.8);
  const c = canvas(n), ctx = c.getContext('2d'), img = ctx.createImageData(n, n);
  const b = new THREE.Color(color);
  for (let i = 0; i < n * n; i++) { const k2 = 0.55 + h.h[i] * 0.8; img.data[i * 4] = b.r * 255 * k2; img.data[i * 4 + 1] = b.g * 255 * k2; img.data[i * 4 + 2] = b.b * 255 * k2; img.data[i * 4 + 3] = 255; }
  ctx.putImageData(img, 0, 0);
  return (cache[k] = { map: tex(c, true), normal: normalFrom(n, h.h, 3) });
}

// Rippling water normals for the river
export function waterNormal() {
  if (cache.water) return cache.water;
  const n = 512;
  const h = gray(n, (u, v) => 0.5 + tile(u, v, 8) * 0.5 + tile(u, v, 24, 5) * 0.25);
  return (cache.water = normalFrom(n, h.h, 4));
}

// Soft round sprite for particles (smoke, glow) and a flame
export function softDot() {
  if (cache.dot) return cache.dot;
  const n = 64, c = canvas(n), ctx = c.getContext('2d');
  const g = ctx.createRadialGradient(n / 2, n / 2, 0, n / 2, n / 2, n / 2);
  g.addColorStop(0, 'rgba(255,255,255,1)'); g.addColorStop(0.4, 'rgba(255,255,255,0.5)'); g.addColorStop(1, 'rgba(255,255,255,0)');
  ctx.fillStyle = g; ctx.fillRect(0, 0, n, n);
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace;
  return (cache.dot = t);
}
export function flame() {
  if (cache.flame) return cache.flame;
  const n = 128, c = canvas(n), ctx = c.getContext('2d'), img = ctx.createImageData(n, n);
  for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
    const u = x / n - 0.5, v = y / n;
    const w = 0.32 * Math.pow(v, 0.7) * (1 - v * 0.2);
    const d = Math.abs(u) / (w + 0.001);
    const k = Math.max(0, 1 - d) * Math.min(1, v * 3) * (0.8 + 0.2 * fbm(x * 0.08, y * 0.05));
    const i = (y * n + x) * 4;
    img.data[i] = 255; img.data[i + 1] = 120 + 135 * k * k; img.data[i + 2] = 40 * k * k * k; img.data[i + 3] = 255 * k;
  }
  ctx.putImageData(img, 0, 0);
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; t.flipY = false;
  return (cache.flame = t);
}

// Painted wooden sign with lettering
export function signTexture(text, { bg = '#5a3a1e', ink = '#f0d890', w = 512, h = 160, font = '600 64px Cinzel, Georgia, serif' } = {}) {
  const c = document.createElement('canvas'); c.width = w; c.height = h;
  const ctx = c.getContext('2d');
  ctx.fillStyle = bg; ctx.fillRect(0, 0, w, h);
  for (let i = 0; i < 40; i++) { ctx.strokeStyle = `rgba(0,0,0,${0.05 + Math.random() * 0.1})`; ctx.beginPath(); const y = Math.random() * h; ctx.moveTo(0, y); ctx.bezierCurveTo(w / 3, y + 6, 2 * w / 3, y - 6, w, y + 2); ctx.stroke(); }
  ctx.strokeStyle = '#2a1a0c'; ctx.lineWidth = 10; ctx.strokeRect(5, 5, w - 10, h - 10);
  ctx.fillStyle = ink; ctx.font = font; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
  ctx.shadowColor = 'rgba(0,0,0,0.6)'; ctx.shadowBlur = 4; ctx.fillText(text, w / 2, h / 2 + 4);
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; t.anisotropy = 8;
  return t;
}

// Grayscale height textures for the creature skin shader (triplanar bump in rest pose)
export function heightTex(kind) {
  const k = 'h-' + kind;
  if (cache[k]) return cache[k];
  const n = 512;
  let f;
  if (kind === 'skin') f = (u, v) => 0.5 + tile(u, v, 40) * 0.25 + tile(u, v, 8, 2) * 0.4;
  else if (kind === 'wrinkle') f = (u, v) => 0.5 + Math.abs(tile(u, v, 6, 4)) * -1.2 + tile(u, v, 30) * 0.15;
  else if (kind === 'fur') f = (u, v) => 0.5 + fbm(u * 90, v * 9, 0, 3) * 0.9;
  else if (kind === 'knobbly') f = (u, v) => 0.5 + Math.pow(Math.abs(tile(u, v, 22, 3)), 0.6) * 0.8 + tile(u, v, 80) * 0.15;
  else if (kind === 'rock') f = (u, v) => 0.5 + tile(u, v, 8) * 0.6 + tile(u, v, 30, 3) * 0.3;
  else if (kind === 'scales') {
    const rows = 20, s = n / rows;
    f = (u, v, x, y) => { const row = Math.floor(y / s), off = row % 2 ? s / 2 : 0; const cx = Math.floor((x - off) / s) * s + off + s / 2, cy = row * s; const dx = (x - cx) / (s * 0.55), dy = (y - cy) / s; return Math.max(0, 1 - dx * dx - (dy - 0.2) * (dy - 0.2) * 1.4); };
  } else if (kind === 'mail') {
    const rings = 16, s = n / rings;
    f = (u, v, x, y) => { let best = 0; for (let oy = -1; oy <= 1; oy++) for (let ox = -1; ox <= 1; ox++) { const row = Math.floor(y / s) + oy, off = row % 2 ? s / 2 : 0; const cx = (Math.floor((x - off) / s) + ox) * s + off + s / 2, cy = row * s + s / 2; const d = Math.hypot(x - cx, (y - cy) * 1.2) / (s * 0.55); best = Math.max(best, Math.max(0, 1 - Math.abs(d - 0.75) * 5)); } return best; };
  } else f = () => 0.5;
  const g = gray(n, f);
  const t = new THREE.CanvasTexture(g.c);
  t.wrapS = t.wrapT = THREE.RepeatWrapping; t.colorSpace = THREE.NoColorSpace; t.anisotropy = 8;
  return (cache[k] = t);
}
// colour detail to multiply over the body colour (tileable, near 0.75 grey on average)
export function detailTex(kind) {
  const k = 'd-' + kind;
  if (cache[k]) return cache[k];
  const n = 512;
  const g = gray(n, kind === 'fur' ? (u, v) => 0.6 + fbm(u * 90, v * 9, 0, 3) * 0.5 : kind === 'scales' ? (u, v) => 0.68 + tile(u, v, 6, 3) * 0.3 + tile(u, v, 40) * 0.1 : (u, v) => 0.74 + tile(u, v, 3, 9) * 0.22 + tile(u, v, 12) * 0.06);
  const t = new THREE.CanvasTexture(g.c);
  t.wrapS = t.wrapT = THREE.RepeatWrapping; t.colorSpace = THREE.SRGBColorSpace; t.anisotropy = 8;
  return (cache[k] = t);
}
