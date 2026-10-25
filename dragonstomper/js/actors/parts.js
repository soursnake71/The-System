// Materials and geometry helpers for the characters.
import * as THREE from 'three';
import { steel, chainmail, cloth, skin, fur, scales } from '../core/texgen.js';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';

const memo = new Map();
const once = (k, f) => memo.has(k) ? memo.get(k) : (memo.set(k, f()), memo.get(k));

export const mats = {
  steel: (tint = 0xd0d2d8, rough = 0.3) => once('steel' + tint + rough, () => {
    const t = steel();
    return new THREE.MeshPhysicalMaterial({ color: tint, metalness: 1, roughness: rough, roughnessMap: t.rough, normalMap: t.normal, normalScale: new THREE.Vector2(0.35, 0.35), clearcoat: 0.25, clearcoatRoughness: 0.4 });
  }),
  polished: () => once('pol', () => new THREE.MeshPhysicalMaterial({ color: 0xe8eaf0, metalness: 1, roughness: 0.12, clearcoat: 0.6 })),
  gold: () => once('gold', () => { const t = steel(); return new THREE.MeshPhysicalMaterial({ color: 0xd8a94a, metalness: 1, roughness: 0.28, roughnessMap: t.rough, normalMap: t.normal, normalScale: new THREE.Vector2(0.2, 0.2) }); }),
  bronze: () => once('bronze', () => new THREE.MeshStandardMaterial({ color: 0x9a6a38, metalness: 1, roughness: 0.4 })),
  mail: (tint = 0x9a9ca2) => once('mail' + tint, () => {
    const t = chainmail();
    const n = t.normal.clone(); n.repeat.set(10, 10); n.needsUpdate = true;
    const a = t.ao.clone(); a.repeat.set(10, 10); a.needsUpdate = true;
    return new THREE.MeshStandardMaterial({ color: tint, metalness: 1, roughness: 0.45, normalMap: n, normalScale: new THREE.Vector2(1.2, 1.2), aoMap: a });
  }),
  leather: (color = 0x4a2e1a) => once('lea' + color, () => { const t = skin('#' + new THREE.Color(color).getHexString(), 0.15, 'lea'); return new THREE.MeshStandardMaterial({ map: t.map, normalMap: t.normal, normalScale: new THREE.Vector2(0.6, 0.6), roughness: 0.62, metalness: 0 }); }),
  cloth: (color = '#1d2f6e', emblem = null) => once('cl' + color + !!emblem, () => {
    const t = cloth(color, emblem);
    return new THREE.MeshPhysicalMaterial({ map: t.map, normalMap: t.normal, normalScale: new THREE.Vector2(0.5, 0.5), roughness: 0.85, sheen: 0.6, sheenRoughness: 0.6, sheenColor: new THREE.Color(color).lerp(new THREE.Color(0xffffff), 0.3), side: THREE.DoubleSide });
  }),
  skin: (color = '#9a6a58', mottle = 0.25, sheen = 0.35) => once('sk' + color + mottle, () => {
    const t = skin(color, mottle);
    return new THREE.MeshPhysicalMaterial({ map: t.map, normalMap: t.normal, normalScale: new THREE.Vector2(0.8, 0.8), roughness: 0.55, sheen, sheenRoughness: 0.5, sheenColor: new THREE.Color(color).lerp(new THREE.Color(0xffd0c0), 0.4) });
  }),
  fur: (color = '#5a4028') => once('fur' + color, () => {
    const t = fur(color);
    return new THREE.MeshPhysicalMaterial({ map: t.map, normalMap: t.normal, normalScale: new THREE.Vector2(1, 1), roughness: 0.9, sheen: 1, sheenRoughness: 0.35, sheenColor: new THREE.Color(color).lerp(new THREE.Color(0xffffff), 0.5) });
  }),
  scales: (base, belly) => once('sc' + base, () => {
    const t = scales(base, belly);
    return new THREE.MeshPhysicalMaterial({ map: t.map, normalMap: t.normal, normalScale: new THREE.Vector2(1, 1), roughness: 0.4, metalness: 0.05, clearcoat: 0.4, clearcoatRoughness: 0.3 });
  }),
  chitin: (color = 0x1a1a14, irid = 0.8) => once('chi' + color + irid, () => new THREE.MeshPhysicalMaterial({ color, roughness: 0.22, metalness: 0.1, clearcoat: 1, clearcoatRoughness: 0.15, iridescence: irid, iridescenceIOR: 1.6, iridescenceThicknessRange: [200, 600] })),
  bone: () => once('bone', () => new THREE.MeshStandardMaterial({ color: 0xe0d6b8, roughness: 0.6 })),
  horn: () => once('horn', () => new THREE.MeshPhysicalMaterial({ color: 0x2a221a, roughness: 0.35, clearcoat: 0.5 })),
  claw: () => once('claw', () => new THREE.MeshPhysicalMaterial({ color: 0x1a1612, roughness: 0.25, clearcoat: 0.8 })),
  dark: () => once('dark', () => new THREE.MeshStandardMaterial({ color: 0x050505, roughness: 1 })),
  wood: () => once('wood', () => new THREE.MeshStandardMaterial({ color: 0x5a3a22, roughness: 0.7 })),
  glow: (color = 0xffaa30, k = 6) => once('gl' + color + k, () => new THREE.MeshStandardMaterial({ color: 0x000000, emissive: color, emissiveIntensity: k })),
  jelly: (color = 0x48c070) => once('jel' + color, () => new THREE.MeshPhysicalMaterial({ color, roughness: 0.08, metalness: 0, transmission: 0.85, thickness: 1.2, ior: 1.33, attenuationColor: new THREE.Color(color), attenuationDistance: 0.6, clearcoat: 1 })),
};

// ---------- geometry ----------
// lathe from [radius, height] pairs, optionally squashed front-to-back
export function lathe(profile, { segs = 28, sx = 1, sz = 1, phiStart = 0, phiLength = Math.PI * 2 } = {}) {
  // LatheGeometry wants the profile bottom-up for outward normals
  if (profile[0][1] > profile[profile.length - 1][1]) profile = [...profile].reverse();
  const g = new THREE.LatheGeometry(profile.map(([r, y]) => new THREE.Vector2(r, y)), segs, phiStart, phiLength);
  g.scale(sx, 1, sz);
  g.computeVertexNormals();
  return g;
}
export function ellipsoid(rx, ry, rz, ws = 24, hs = 16) { const g = new THREE.SphereGeometry(1, ws, hs); g.scale(rx, ry, rz); return g; }
// a limb segment hanging down from its joint: capsule-ish lathe from r0 (top) to r1 (bottom)
export function segment(len, r0, r1, { bulge = 0.15, segs = 20, sz = 1 } = {}) {
  const pts = [];
  const n = 10;
  for (let i = 0; i <= n; i++) {
    const t = i / n, r = THREE.MathUtils.lerp(r0, r1, t) * (1 + bulge * Math.sin(t * Math.PI));
    pts.push([r * Math.sin(Math.max(0.15, Math.min(1, t * 6, (1 - t) * 6)) * Math.PI / 2) + 0.001, -t * len]);
  }
  pts.unshift([0.001, 0.02]); pts.push([0.001, -len - 0.02]);
  return lathe(pts, { segs, sz });
}
// a curved horn / tusk / claw: a tapered tube along a curve
export function hornGeo(len, r, curl = 0.6, segs = 12) {
  const pts = [];
  for (let i = 0; i <= 8; i++) { const t = i / 8; pts.push(new THREE.Vector3(0, t * len, Math.sin(t * curl * 2) * len * 0.35 * t)); }
  const curve = new THREE.CatmullRomCurve3(pts);
  const g = new THREE.TubeGeometry(curve, 16, r, segs, false);
  // taper towards the tip
  const p = g.attributes.position, c = new THREE.Vector3();
  for (let i = 0; i < p.count; i++) {
    const j = Math.floor(i / (segs + 1)) / 16;
    const on = curve.getPointAt(Math.min(1, j));
    c.set(p.getX(i), p.getY(i), p.getZ(i)).sub(on).multiplyScalar(1 - j * 0.95).add(on);
    p.setXYZ(i, c.x, c.y, c.z);
  }
  g.computeVertexNormals();
  return g;
}
// rough rock chunk
export function rockGeo(r, detail = 2, rough = 0.22, seed = 1) {
  const g = new THREE.IcosahedronGeometry(r, detail);
  const p = g.attributes.position, v = new THREE.Vector3();
  for (let i = 0; i < p.count; i++) {
    v.fromBufferAttribute(p, i);
    const k = 1 + rough * (Math.sin(v.x * 7 + seed) * Math.sin(v.y * 5 + seed * 2) * Math.sin(v.z * 6 + seed * 3) + 0.4 * Math.sin(v.x * 13 + v.z * 11));
    v.multiplyScalar(k); p.setXYZ(i, v.x, v.y, v.z);
  }
  g.computeVertexNormals();
  return g;
}
export function mesh(geo, material, x = 0, y = 0, z = 0) {
  const m = new THREE.Mesh(geo, material);
  m.position.set(x, y, z);
  m.castShadow = true; m.receiveShadow = true;
  return m;
}
export { mergeGeometries };

// A longsword along +y from the grip (origin at the hand)
export function longsword(len = 0.95) {
  const g = new THREE.Group();
  const shape = new THREE.Shape();
  const w = 0.026;
  shape.moveTo(-w, 0); shape.lineTo(-w * 0.85, len * 0.85); shape.lineTo(0, len); shape.lineTo(w * 0.85, len * 0.85); shape.lineTo(w, 0); shape.lineTo(-w, 0);
  const blade = new THREE.ExtrudeGeometry(shape, { depth: 0.006, bevelEnabled: true, bevelThickness: 0.004, bevelSize: 0.012, bevelSegments: 1 });
  blade.translate(0, 0, -0.003);
  g.add(mesh(blade, mats.polished(), 0, 0.12, 0));
  g.add(mesh(new THREE.BoxGeometry(0.22, 0.025, 0.035), mats.steel(0x9a9aa0, 0.35), 0, 0.11, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.017, 0.016, 0.16, 10), mats.leather(0x2a1a10), 0, 0.02, 0));
  g.add(mesh(new THREE.SphereGeometry(0.03, 12, 8), mats.steel(0x9a9aa0, 0.35), 0, -0.07, 0));
  g.userData.tip = new THREE.Vector3(0, len + 0.12, 0);
  return g;
}
export function axe(len = 0.75, headSize = 1) {
  const g = new THREE.Group();
  g.add(mesh(new THREE.CylinderGeometry(0.018, 0.022, len, 10), mats.wood(), 0, len / 2 - 0.1, 0));
  const s = new THREE.Shape();
  s.moveTo(0, 0); s.quadraticCurveTo(0.1, 0.02, 0.16, 0.1); s.lineTo(0.16, -0.1); s.quadraticCurveTo(0.1, -0.02, 0, -0.04);
  const head = new THREE.ExtrudeGeometry(s, { depth: 0.012, bevelEnabled: true, bevelThickness: 0.006, bevelSize: 0.006, bevelSegments: 1 });
  head.scale(headSize, headSize, 1); head.translate(0.01, 0, -0.006);
  g.add(mesh(head, mats.steel(0xa0a0a4, 0.4), 0, len - 0.18, 0));
  return g;
}
export function halberd(len = 2.4) {
  const g = new THREE.Group();
  g.add(mesh(new THREE.CylinderGeometry(0.022, 0.026, len, 10), mats.wood(), 0, len / 2 - 0.6, 0));
  const s = new THREE.Shape();
  s.moveTo(0, 0); s.lineTo(0.28, 0.14); s.quadraticCurveTo(0.32, 0, 0.28, -0.16); s.lineTo(0, -0.08);
  const blade = new THREE.ExtrudeGeometry(s, { depth: 0.012, bevelEnabled: true, bevelThickness: 0.006, bevelSize: 0.006, bevelSegments: 1 });
  blade.translate(0.02, 0, -0.006);
  g.add(mesh(blade, mats.steel(0xb0b0b4, 0.3), 0, len - 0.85, 0));
  g.add(mesh(new THREE.ConeGeometry(0.035, 0.35, 4), mats.steel(0xb0b0b4, 0.3), 0, len - 0.43, 0));
  return g;
}
