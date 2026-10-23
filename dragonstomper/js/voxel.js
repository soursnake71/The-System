// Turns the original 8-pixel-wide Atari sprite bitmaps into voxel meshes.
// Modes:
//   'lathe'  - spin the silhouette around its vertical axis (trees, potions, towers)
//   'taper'  - extrude, each row as deep as it is wide relative to the widest row; gaps inside a
//              row become recesses instead of holes (buildings, creatures, signs)
//   'slab'   - flat extrusion of fixed depth (flat things: keys, scrolls, gates)
import * as THREE from '../lib/three.module.min.js';

const FACES = [
  { n: [1, 0, 0], v: [[1, 0, 0], [1, 1, 0], [1, 1, 1], [1, 0, 1]], shade: 0.82 },
  { n: [-1, 0, 0], v: [[0, 0, 1], [0, 1, 1], [0, 1, 0], [0, 0, 0]], shade: 0.82 },
  { n: [0, 1, 0], v: [[0, 1, 1], [1, 1, 1], [1, 1, 0], [0, 1, 0]], shade: 1.0 },
  { n: [0, -1, 0], v: [[0, 0, 0], [1, 0, 0], [1, 0, 1], [0, 0, 1]], shade: 0.55 },
  { n: [0, 0, 1], v: [[1, 0, 1], [1, 1, 1], [0, 1, 1], [0, 0, 1]], shade: 0.92 },
  { n: [0, 0, -1], v: [[0, 0, 0], [0, 1, 0], [1, 1, 0], [1, 0, 0]], shade: 0.7 },
];

// A grid of colours; null = empty.
export class Grid {
  constructor(w, h, d) { this.w = w; this.h = h; this.d = d; this.c = new Array(w * h * d).fill(null); }
  i(x, y, z) { return (z * this.h + y) * this.w + x; }
  get(x, y, z) { return x < 0 || y < 0 || z < 0 || x >= this.w || y >= this.h || z >= this.d ? null : this.c[this.i(x, y, z)]; }
  set(x, y, z, col) { if (x >= 0 && y >= 0 && z >= 0 && x < this.w && y < this.h && z < this.d) this.c[this.i(x, y, z)] = col; }
}

const bit = (row, x) => (row >> (7 - x)) & 1;

// rows are top-down; returns a Grid with y up
export function fromSprite(rows, color, mode = 'taper', opts = {}) {
  const H = rows.length, W = 8;
  const col = new THREE.Color(color);
  const accent = opts.accent ? new THREE.Color(opts.accent) : null;
  const rowCol = (y) => (opts.rowColors && opts.rowColors[y] != null) ? new THREE.Color(opts.rowColors[y]) : col;
  if (mode === 'lathe') {
    const R = opts.radius || 8; // grid is R x R in plan
    const g = new Grid(R, H, R);
    const cx = (R - 1) / 2;
    for (let ry = 0; ry < H; ry++) {
      const row = rows[ry], y = H - 1 - ry, c = rowCol(ry);
      for (let x = 0; x < R; x++) for (let z = 0; z < R; z++) {
        const r = Math.hypot(x - cx, z - cx) * (8 / R);
        const l = Math.round(3.5 - r), rr = Math.round(3.5 + r);
        const on = (l >= 0 && bit(row, l)) || (rr <= 7 && bit(row, rr));
        if (on && r <= 4.1) g.set(x, y, z, c);
      }
    }
    return g;
  }
  const D = opts.depth || (mode === 'slab' ? 2 : 6);
  const g = new Grid(W, H, D);
  let maxW = 1;
  const span = rows.map(r => { let a = 8, b = -1; for (let x = 0; x < 8; x++) if (bit(r, x)) { a = Math.min(a, x); b = Math.max(b, x); } return [a, b]; });
  for (const [a, b] of span) if (b >= a) maxW = Math.max(maxW, b - a + 1);
  for (let ry = 0; ry < H; ry++) {
    const row = rows[ry], y = H - 1 - ry, [a, b] = span[ry], c = rowCol(ry);
    if (b < a) continue;
    let depth = D;
    if (mode === 'taper') depth = Math.max(1, Math.round(D * Math.min(1, (b - a + 1) / maxW)));
    const z0 = Math.floor((D - depth) / 2);
    for (let x = 0; x < 8; x++) {
      const on = bit(row, x);
      const inside = x > a && x < b;
      for (let z = z0; z < z0 + depth; z++) {
        if (on) g.set(x, y, z, c);
        else if (mode === 'taper' && inside && opts.recess !== false && z < z0 + depth - 1 && depth > 2) g.set(x, y, z, accent || c.clone().multiplyScalar(0.35));
      }
    }
  }
  return g;
}

// Build a BufferGeometry from a grid. Each voxel is sx * sy * sz; origin at bottom centre.
export function meshGeometry(g, sx = 0.3, sy = 0.2, sz = 0.3, jitter = 0.06) {
  const pos = [], nor = [], colr = [];
  const ox = g.w / 2, oz = g.d / 2;
  let seed = 1;
  const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
  for (let z = 0; z < g.d; z++) for (let y = 0; y < g.h; y++) for (let x = 0; x < g.w; x++) {
    const c = g.get(x, y, z);
    if (!c) continue;
    const j = 1 + (rnd() - 0.5) * jitter * 2;
    for (const f of FACES) {
      if (g.get(x + f.n[0], y + f.n[1], z + f.n[2])) continue;
      const s = f.shade * j;
      const quad = f.v.map(v => [(x + v[0] - ox) * sx, (y + v[1]) * sy, (z + v[2] - oz) * sz]);
      for (const k of [0, 1, 2, 0, 2, 3]) {
        pos.push(...quad[k]); nor.push(...f.n);
        colr.push(Math.min(1, c.r * s), Math.min(1, c.g * s), Math.min(1, c.b * s));
      }
    }
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  geo.setAttribute('normal', new THREE.Float32BufferAttribute(nor, 3));
  geo.setAttribute('color', new THREE.Float32BufferAttribute(colr, 3));
  geo.computeBoundingSphere();
  geo.computeBoundingBox();
  return geo;
}

export const voxMat = new THREE.MeshLambertMaterial({ vertexColors: true });

export function spriteMesh(rows, color, mode, scale = 1, opts = {}) {
  const g = fromSprite(rows, color, mode, opts);
  const s = scale;
  const geo = meshGeometry(g, 0.3 * s * (opts.sx || 1), 0.2 * s * (opts.sy || 1), 0.3 * s * (opts.sz || 1));
  const m = new THREE.Mesh(geo, opts.material || voxMat);
  m.castShadow = true; m.receiveShadow = true;
  return m;
}

// Hand-authored pictures in the same 8-bit style: strings of chars mapped to colours.
export function fromArt(lines, palette, depthMap) {
  const H = lines.length, W = Math.max(...lines.map(l => l.length));
  const D = depthMap ? Math.max(...Object.values(depthMap)) : 3;
  const g = new Grid(W, H, D);
  for (let ry = 0; ry < H; ry++) for (let x = 0; x < W; x++) {
    const ch = lines[ry][x];
    if (!ch || ch === '.' || ch === ' ') continue;
    const c = new THREE.Color(palette[ch]);
    const d = depthMap && depthMap[ch] != null ? depthMap[ch] : D;
    const z0 = Math.floor((D - d) / 2);
    for (let z = z0; z < z0 + d; z++) g.set(x, H - 1 - ry, z, c);
  }
  return g;
}

export function artMesh(lines, palette, depthMap, sx = 0.2, sy = 0.2, sz = 0.2) {
  const m = new THREE.Mesh(meshGeometry(fromArt(lines, palette, depthMap), sx, sy, sz), voxMat);
  m.castShadow = true; m.receiveShadow = true;
  return m;
}
