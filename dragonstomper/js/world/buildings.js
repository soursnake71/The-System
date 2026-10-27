// Procedural medieval architecture built from Poly Haven materials, with UVs in metres so
// the textures keep a real-world scale. Each builder returns { group, colliders, door }.
import * as THREE from 'three';
import { pbr } from '../core/assets.js';
import { signTexture } from '../core/texgen.js';

// materials are shared; texture repeat = 1 / tile size (UVs are in metres)
const mats = {};
export function mat(name, tile = 2, opts = {}) {
  const key = name + tile + JSON.stringify(opts);
  return mats[key] || (mats[key] = pbr(name, { repeat: [1 / tile, 1 / tile], ...opts }));
}
export const M = {
  plaster: () => mat('plastered_wall_02', 2.5, { color: 0xf2e8d8 }),
  stone: () => mat('castle_wall_slates', 2.2),
  stoneDark: () => mat('castle_wall_slates', 2.2, { color: 0xa09890 }),
  cobble: () => mat('cobblestone_floor_08', 2.5),
  thatch: () => mat('thatch_roof_angled', 2.5),
  slate: () => mat('roof_slates_02', 2),
  wood: () => mat('wood_planks', 2, { color: 0xb09070 }),
  timber: () => mat('wood_planks', 1.5, { color: 0x4a3423 }),
  rock: () => mat('rock_wall_08', 4),
};
export const glass = new THREE.MeshPhysicalMaterial({ color: 0x0c1014, roughness: 0.08, metalness: 0, reflectivity: 0.6, clearcoat: 1 });
export const iron = new THREE.MeshStandardMaterial({ color: 0x2a2a2c, roughness: 0.45, metalness: 1 });

// a box whose UVs are in metres on every face
export function boxGeo(w, h, d) {
  const g = new THREE.BoxGeometry(w, h, d);
  const uv = g.attributes.uv;
  const dims = [[d, h], [d, h], [w, d], [w, d], [w, h], [w, h]];
  for (let f = 0; f < 6; f++) for (let k = 0; k < 4; k++) {
    const i = f * 4 + k;
    uv.setXY(i, uv.getX(i) * dims[f][0], uv.getY(i) * dims[f][1]);
  }
  return g;
}
export function box(w, h, d, material, x = 0, y = 0, z = 0, ry = 0) {
  const m = new THREE.Mesh(boxGeo(w, h, d), material);
  m.position.set(x, y + h / 2, z); m.rotation.y = ry;
  m.castShadow = m.receiveShadow = true;
  return m;
}
// a triangular gable end, apex up, facing +z, UVs in metres
function gableGeo(w, h) {
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute([-w / 2, 0, 0, w / 2, 0, 0, 0, h, 0], 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute([0, 0, w, 0, w / 2, h], 2));
  g.setAttribute('normal', new THREE.Float32BufferAttribute([0, 0, 1, 0, 0, 1, 0, 0, 1], 3));
  return g;
}
// a pitched roof along x: two thick slabs meeting at the ridge, plus gable walls
export function gableRoof(w, d, rise, roofMat, wallMat, overhang = 0.5, thick = 0.25) {
  const g = new THREE.Group();
  // the ridge is at z = 0, height `rise`; eaves hang `overhang` past the walls
  const half = d / 2 + overhang;
  const ang = Math.atan2(rise, d / 2);
  const eaveY = -overhang * Math.tan(ang);
  const slope = Math.hypot(half, rise - eaveY);
  for (const s of [-1, 1]) {
    const slab = new THREE.Mesh(boxGeo(w + overhang * 2, thick, slope), roofMat);
    slab.rotation.x = s * ang;
    slab.position.set(0, (rise + eaveY) / 2 + thick / 2, s * half / 2);
    slab.castShadow = slab.receiveShadow = true;
    g.add(slab);
  }
  // gable walls at both ends
  for (const s of [-1, 1]) {
    const t = new THREE.Mesh(gableGeo(d, rise), wallMat);
    t.rotation.y = s > 0 ? -Math.PI / 2 : Math.PI / 2;
    t.position.x = s * w / 2;
    t.castShadow = t.receiveShadow = true;
    g.add(t);
  }
  // ridge beam
  const ridge = new THREE.Mesh(boxGeo(w + overhang * 2 + 0.2, 0.3, 0.35), M.timber());
  ridge.position.y = rise + thick * 0.6;
  ridge.castShadow = true;
  g.add(ridge);
  return g;
}

function windowAt(g, x, y, z, ry, w = 0.9, h = 1.1) {
  const win = new THREE.Group();
  const pane = new THREE.Mesh(new THREE.PlaneGeometry(w, h), glass);
  pane.position.z = 0.02; win.add(pane);
  const fr = M.timber();
  win.add(box(w + 0.2, 0.12, 0.18, fr, 0, -h / 2 - 0.12, 0.05), box(w + 0.2, 0.12, 0.18, fr, 0, h / 2, 0.05));
  win.add(box(0.12, h, 0.18, fr, -w / 2 - 0.04, -h / 2, 0.05), box(0.12, h, 0.18, fr, w / 2 + 0.04, -h / 2, 0.05));
  win.add(box(0.06, h, 0.06, fr, 0, -h / 2, 0.06));
  // open shutters
  for (const s of [-1, 1]) {
    const sh = box(w / 2, h, 0.05, M.wood(), 0, -h / 2, 0);
    sh.position.x = s * (w / 2 + w / 4 + 0.08); sh.position.z = 0.12; sh.rotation.y = s * 0.25;
    win.add(sh);
  }
  win.position.set(x, y, z); win.rotation.y = ry;
  g.add(win);
}

function doorAt(g, x, z, ry, w = 1.2, h = 2.2, mat = M.wood()) {
  const d = new THREE.Group();
  d.add(box(w, h, 0.12, mat, 0, 0, 0));
  d.add(box(w + 0.3, 0.2, 0.25, M.timber(), 0, h, 0.05));
  d.add(box(0.15, h, 0.25, M.timber(), -w / 2 - 0.07, 0, 0.05), box(0.15, h, 0.25, M.timber(), w / 2 + 0.07, 0, 0.05));
  for (const yy of [0.5, 1.6]) d.add(box(w * 0.9, 0.07, 0.04, iron, 0, yy, 0.08));
  const ring = new THREE.Mesh(new THREE.TorusGeometry(0.08, 0.015, 8, 16), iron);
  ring.position.set(w * 0.3, 1.1, 0.1); d.add(ring);
  d.position.set(x, 0, z); d.rotation.y = ry;
  g.add(d);
  return d;
}

// A timber-framed house. Front faces +z.
export function house({ w = 8, d = 6, h = 3.2, storeys = 1, roof = 'thatch', sign = null, chimney = true } = {}) {
  const g = new THREE.Group();
  const H = h * storeys;
  g.add(box(w + 0.3, 0.6, d + 0.3, M.stone(), 0, -0.3, 0));
  g.add(box(w, H, d, M.plaster(), 0, 0.3, 0));
  // timber frame
  const t = M.timber(), b = 0.22;
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) g.add(box(b, H, b, t, sx * (w / 2), 0.3, sz * (d / 2)));
  for (let s = 0; s <= storeys; s++) {
    const y = 0.3 + s * h - (s === storeys ? b : 0);
    g.add(box(w + b, b, b, t, 0, y, d / 2), box(w + b, b, b, t, 0, y, -d / 2), box(b, b, d + b, t, w / 2, y, 0), box(b, b, d + b, t, -w / 2, y, 0));
  }
  for (const zz of [d / 2, -d / 2]) for (let k = 1; k < 4; k++) g.add(box(b, H, b, t, -w / 2 + (w * k) / 4, 0.3, zz));
  // braces on the gable sides
  for (const sx of [-1, 1]) for (let s = 0; s < storeys; s++) {
    const br = box(b * 0.8, Math.hypot(d / 2, h) * 0.95, b * 0.8, t, sx * w / 2, 0, 0);
    br.position.set(sx * (w / 2 + 0.02), 0.3 + s * h + h / 2, -d / 4);
    br.rotation.x = Math.atan2(d / 2, h);
    g.add(br);
  }
  doorAt(g, -w / 4, d / 2 + 0.05, 0);
  for (let s = 0; s < storeys; s++) {
    windowAt(g, w / 4, 0.3 + s * h + 1.6, d / 2 + 0.03, 0);
    if (s > 0) windowAt(g, -w / 4, 0.3 + s * h + 1.6, d / 2 + 0.03, 0);
    windowAt(g, w / 4, 0.3 + s * h + 1.6, -d / 2 - 0.03, Math.PI);
    windowAt(g, -w / 4, 0.3 + s * h + 1.6, -d / 2 - 0.03, Math.PI);
  }
  const r = gableRoof(w, d, d * 0.55, roof === 'slate' ? M.slate() : M.thatch(), M.plaster(), 0.6, roof === 'slate' ? 0.15 : 0.45);
  r.position.y = 0.3 + H;
  g.add(r);
  if (chimney) g.add(box(0.9, H + d * 0.55 + 1.2, 0.9, M.stone(), w / 2 - 1, 0.3, -d / 4));
  if (sign) {
    const arm = box(1.6, 0.1, 0.1, iron, 0, 0, 0); arm.position.set(w / 4 + 1.3, 0.3 + h - 0.2, d / 2 + 0.75); arm.rotation.y = Math.PI / 2; g.add(arm);
    const board = new THREE.Mesh(new THREE.BoxGeometry(1.8, 0.6, 0.06), [M.timber(), M.timber(), M.timber(), M.timber(),
      new THREE.MeshStandardMaterial({ map: signTexture(sign), roughness: 0.7 }), new THREE.MeshStandardMaterial({ map: signTexture(sign), roughness: 0.7 })]);
    board.position.set(w / 4 + 1.3, 0.3 + h - 0.75, d / 2 + 1.45); board.rotation.y = Math.PI / 2;
    board.castShadow = true;
    g.add(board);
  }
  return { group: g, colliders: [{ type: 'box', x: 0, z: 0, hw: w / 2 + 0.2, hd: d / 2 + 0.2, rot: 0 }], door: new THREE.Vector3(-w / 4, 0, d / 2 + 1.3) };
}

// A stone church with a bell tower and spire, cross on top. Front (door) faces +z.
export function church({ w = 8, d = 14, h = 6 } = {}) {
  const g = new THREE.Group();
  g.add(box(w + 0.4, 0.8, d + 0.4, M.stoneDark(), 0, -0.4, 0));
  g.add(box(w, h, d, M.stone(), 0, 0.4, 0));
  const r = gableRoof(d, w, w * 0.6, M.slate(), M.stone(), 0.5, 0.18);
  r.rotation.y = Math.PI / 2; r.position.y = 0.4 + h; g.add(r);
  // tower at the front
  const tw = 4.2, th = h + 7;
  g.add(box(tw, th, tw, M.stone(), 0, 0.4, d / 2 + tw / 2 - 0.5));
  for (const s of [-1, 1]) for (const k of [-1, 1]) g.add(box(0.5, 0.6, 0.5, M.stone(), s * (tw / 2 - 0.25), 0.4 + th, d / 2 + tw / 2 - 0.5 + k * (tw / 2 - 0.25)));
  const spire = new THREE.Mesh(new THREE.ConeGeometry(tw * 0.72, 7, 4, 1), M.slate());
  spire.rotation.y = Math.PI / 4; spire.position.set(0, 0.4 + th + 3.5, d / 2 + tw / 2 - 0.5); spire.castShadow = true; g.add(spire);
  const cy = 0.4 + th + 7;
  g.add(box(0.14, 1.6, 0.14, iron, 0, cy, d / 2 + tw / 2 - 0.5), box(0.9, 0.14, 0.14, iron, 0, cy + 0.95, d / 2 + tw / 2 - 0.5));
  // belfry openings
  for (const k of [-1, 1]) {
    const o = box(1.1, 1.8, 0.3, new THREE.MeshStandardMaterial({ color: 0x050505, roughness: 1 }), 0, 0.4 + th - 2.6, d / 2 + tw / 2 - 0.5 + k * (tw / 2 + 0.01));
    g.add(o);
  }
  // arched door and narrow windows
  doorAt(g, 0, d / 2 + tw - 0.4, 0, 1.6, 2.8);
  for (let i = -2; i <= 2; i++) for (const s of [-1, 1]) {
    const win = new THREE.Mesh(new THREE.PlaneGeometry(0.7, 2.2), new THREE.MeshPhysicalMaterial({ color: 0x3a2a5a, emissive: 0x150a20, roughness: 0.15, metalness: 0.2 }));
    win.position.set(s * (w / 2 + 0.02), 0.4 + h * 0.55, i * 2.4); win.rotation.y = s * Math.PI / 2; g.add(win);
  }
  return { group: g, colliders: [{ type: 'box', x: 0, z: 0, hw: w / 2 + 0.3, hd: d / 2 + 0.3, rot: 0 }, { type: 'box', x: 0, z: d / 2 + tw / 2 - 0.5, hw: tw / 2 + 0.1, hd: tw / 2 + 0.1, rot: 0 }],
    door: new THREE.Vector3(0, 0, d / 2 + tw + 0.6) };
}

// A round wattle-and-daub hut with a conical thatched roof. Door faces +z.
export function hut({ r = 3, h = 2.4 } = {}) {
  const g = new THREE.Group();
  const wallGeo = new THREE.CylinderGeometry(r, r * 1.04, h, 28, 1, true);
  const uv = wallGeo.attributes.uv; for (let i = 0; i < uv.count; i++) uv.setXY(i, uv.getX(i) * Math.PI * 2 * r, uv.getY(i) * h);
  const wall = new THREE.Mesh(wallGeo, mat('plastered_wall_02', 2, { color: 0xc8b08a, side: THREE.DoubleSide }));
  wall.position.y = h / 2; wall.castShadow = wall.receiveShadow = true; g.add(wall);
  const roofGeo = new THREE.ConeGeometry(r + 0.9, r * 1.2, 28, 4, true);
  const ruv = roofGeo.attributes.uv; for (let i = 0; i < ruv.count; i++) ruv.setXY(i, ruv.getX(i) * Math.PI * 2 * (r + 0.9), ruv.getY(i) * r * 1.6);
  const roof = new THREE.Mesh(roofGeo, mat('thatch_roof_angled', 2, { side: THREE.DoubleSide }));
  roof.position.y = h + r * 0.6 - 0.15; roof.castShadow = roof.receiveShadow = true; g.add(roof);
  doorAt(g, 0, r + 0.02, 0, 1, 1.9);
  return { group: g, colliders: [{ type: 'circle', x: 0, z: 0, r: r + 0.2 }], door: new THREE.Vector3(0, 0, r + 1.2) };
}

// A ruined temple: steps, broken columns, a fallen lintel
export function temple({ w = 10, d = 8 } = {}, rand = Math.random) {
  const g = new THREE.Group();
  g.add(box(w + 2, 0.4, d + 2, M.stoneDark(), 0, 0, 0), box(w + 1, 0.4, d + 1, M.stoneDark(), 0, 0.4, 0), box(w, 0.4, d, M.stone(), 0, 0.8, 0));
  const colMat = M.stone();
  const cols = [];
  for (let i = 0; i < 4; i++) for (const s of [-1, 1]) cols.push([-w / 2 + 1 + i * (w - 2) / 3, s * (d / 2 - 1)]);
  for (const [x, z] of cols) {
    const ch = rand() < 0.35 ? 1 + rand() * 2 : 5;
    const c = new THREE.Mesh(new THREE.CylinderGeometry(0.45, 0.5, ch, 20), colMat);
    c.position.set(x, 1.2 + ch / 2, z); c.castShadow = c.receiveShadow = true; g.add(c);
    g.add(box(1.2, 0.3, 1.2, colMat, x, 1.2, z));
    if (ch > 4) g.add(box(1.2, 0.35, 1.2, colMat, x, 1.2 + ch, z));
  }
  const lintel = box(w - 1, 0.7, 1.2, colMat, 0, 6.5, -(d / 2 - 1)); g.add(lintel);
  const fallen = box(0.9, 0.9, 4.5, colMat, w / 2 + 2, -0.05, 1, 0.6); g.add(fallen);
  // dark doorway into the inner shrine
  g.add(box(3, 3.4, 3, M.stone(), 0, 1.2, -1.5));
  g.add(box(1.4, 2.2, 0.1, new THREE.MeshStandardMaterial({ color: 0x030303, roughness: 1 }), 0, 1.2, 0.02));
  return { group: g, colliders: [{ type: 'box', x: 0, z: -1.5, hw: 1.7, hd: 1.7, rot: 0 }, ...cols.map(([x, z]) => ({ type: 'circle', x, z, r: 0.6 }))],
    door: new THREE.Vector3(0, 0, 1.4) };
}

// A castle gatehouse: two round towers and a wall with an arch. Faces +z; the arch is at x = 0.
export function gatehouse({ span = 7 } = {}) {
  const g = new THREE.Group(), cols = [];
  const s = M.stone();
  for (const sx of [-1, 1]) {
    const tw = new THREE.Mesh(new THREE.CylinderGeometry(3, 3.3, 14, 32), s);
    const uv = tw.geometry.attributes.uv; for (let i = 0; i < uv.count; i++) uv.setXY(i, uv.getX(i) * 20, uv.getY(i) * 14);
    tw.position.set(sx * (span / 2 + 2.8), 7, 0); tw.castShadow = tw.receiveShadow = true; g.add(tw);
    for (let a = 0; a < 12; a++) {
      const ang = a / 12 * Math.PI * 2;
      g.add(box(1, 1, 0.8, s, sx * (span / 2 + 2.8) + Math.cos(ang) * 2.7, 14, Math.sin(ang) * 2.7, -ang));
    }
    const cap = new THREE.Mesh(new THREE.ConeGeometry(3.4, 4, 32), M.slate()); cap.position.set(sx * (span / 2 + 2.8), 17, 0); cap.castShadow = true; g.add(cap);
    cols.push({ type: 'circle', x: sx * (span / 2 + 2.8), z: 0, r: 3.4 });
    // curtain wall running off to each side
    g.add(box(30, 9, 2.4, s, sx * (span / 2 + 2.8 + 16), 0, -0.5));
    cols.push({ type: 'box', x: sx * (span / 2 + 2.8 + 16), z: -0.5, hw: 15, hd: 1.4, rot: 0 });
  }
  g.add(box(span, 4, 2.6, s, 0, 6, 0));
  for (let i = 0; i < 4; i++) g.add(box(0.9, 1, 0.9, s, -span / 2 + 0.6 + i * (span - 1.2) / 3, 10, 1));
  return { group: g, colliders: cols, arch: new THREE.Vector3(0, 0, 0) };
}

// A plank bridge with rails over a river running along x. Centred, deck along z.
export function bridge({ len = 16, w = 4, rise = 1.2 } = {}) {
  const g = new THREE.Group();
  const n = Math.round(len / 0.35);
  for (let i = 0; i < n; i++) {
    const t = i / (n - 1), z = -len / 2 + t * len;
    const y = Math.sin(t * Math.PI) * rise;
    const p = box(w, 0.12, 0.32, M.wood(), (Math.random() - 0.5) * 0.06, y + 0.4, z);
    p.rotation.x = Math.cos(t * Math.PI) * rise * Math.PI / len * 0.9;
    g.add(p);
  }
  for (const s of [-1, 1]) {
    g.add(box(0.3, 0.3, len, M.timber(), s * (w / 2 - 0.1), 0.15, 0));
    for (let i = 0; i <= 6; i++) {
      const t = i / 6, z = -len / 2 + t * len, y = Math.sin(t * Math.PI) * rise;
      g.add(box(0.2, 1.1, 0.2, M.timber(), s * (w / 2), y + 0.4, z));
    }
    const rail = box(0.15, 0.15, len, M.timber(), s * w / 2, 0, 0);
    rail.position.y = rise + 1.4;
    g.add(rail);
  }
  for (const z of [-len / 2 + 1, len / 2 - 1]) g.add(box(w + 1, 1.6, 1.6, M.stoneDark(), 0, -1.2, z));
  return { group: g, deck: (z) => Math.sin(THREE.MathUtils.clamp((z + len / 2) / len, 0, 1) * Math.PI) * rise + 0.52 };
}

// A palisade of sharpened logs along a polyline
export function palisade(points, height = 3.6) {
  const geo = new THREE.CylinderGeometry(0.22, 0.25, height, 10);
  geo.translate(0, height / 2, 0);
  const tip = new THREE.ConeGeometry(0.22, 0.5, 10); tip.translate(0, height + 0.25, 0);
  const segs = [];
  for (let i = 0; i < points.length - 1; i++) {
    const a = points[i], b = points[i + 1], L = Math.hypot(b.x - a.x, b.z - a.z), n = Math.ceil(L / 0.46);
    for (let k = 0; k < n; k++) { const t = k / n; segs.push([a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t, a.y ?? 0]); }
  }
  const material = mat('bark_willow', 1.2, { color: 0x9a8a78 });
  const logs = new THREE.InstancedMesh(geo, material, segs.length), tips = new THREE.InstancedMesh(tip, material, segs.length);
  const mx = new THREE.Matrix4();
  segs.forEach(([x, z, y], i) => { const s = 0.9 + Math.random() * 0.25; mx.makeScale(1, s, 1); mx.setPosition(x, y - 0.3, z); logs.setMatrixAt(i, mx); tips.setMatrixAt(i, mx); });
  logs.castShadow = tips.castShadow = true; logs.receiveShadow = true;
  const g = new THREE.Group(); g.add(logs, tips);
  return g;
}

// place a builder result into the world at (x, y, z) rotated by ry; returns world colliders
export function place(scene, built, x, y, z, ry = 0) {
  built.group.position.set(x, y, z);
  built.group.rotation.y = ry;
  scene.add(built.group);
  const c = Math.cos(ry), s = Math.sin(ry);
  const tr = (px, pz) => [x + px * c + pz * s, z - px * s + pz * c];
  const cols = (built.colliders || []).map(k => {
    const [wx, wz] = tr(k.x, k.z);
    return { ...k, x: wx, z: wz, rot: (k.rot || 0) + ry };
  });
  if (built.door) { const [dx, dz] = tr(built.door.x, built.door.z); built.doorWorld = new THREE.Vector3(dx, y, dz); }
  return cols;
}
