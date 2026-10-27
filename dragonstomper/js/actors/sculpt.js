// Sculpting creatures out of signed-distance primitives (smoothly blended capsules and
// ellipsoids), polygonising them with marching cubes into one seamless mesh, and skinning
// that mesh to a skeleton. Gives continuous, organic bodies instead of jointed mannequins.
import * as THREE from 'three';
import { lite } from '../core/gfx.js';
import { edgeTable, triTable } from 'three/addons/objects/MarchingCubes.js';
import { fbm } from '../core/texgen.js';

const V = THREE.Vector3;

export class Sculpt {
  constructor() { this.prims = []; this.subs = []; }
  // a capsule from a to b whose radius tapers r0 -> r1
  capsule(a, b, r0, r1 = r0, { bone, mat = 0, k = 0.04 } = {}) {
    this.prims.push({ type: 'cap', a: a.clone(), b: b.clone(), r0, r1, bone, mat, k });
    return this;
  }
  // an ellipsoid at c with radii r (Vector3), optionally rotated by quaternion q
  ellipsoid(c, r, { bone, mat = 0, k = 0.04, q = null } = {}) {
    this.prims.push({ type: 'ell', c: c.clone(), r: r.clone(), q: q ? q.clone().invert() : null, bone, mat, k });
    return this;
  }
  // a chain of tapered capsules through points (horns, tails, fingers)
  chain(points, r0, r1, opts) {
    for (let i = 0; i < points.length - 1; i++) {
      const t0 = i / (points.length - 1), t1 = (i + 1) / (points.length - 1);
      this.capsule(points[i], points[i + 1], THREE.MathUtils.lerp(r0, r1, t0), THREE.MathUtils.lerp(r0, r1, t1), { ...opts, bone: Array.isArray(opts.bone) ? opts.bone[i] : opts.bone });
    }
    return this;
  }
  // carve out (eye sockets, mouths)
  carve(c, r, { k = 0.01, q = null } = {}) { this.subs.push({ type: 'ell', c: c.clone(), r: r.clone(), q: q ? q.clone().invert() : null, k }); return this; }

  dist(p, pr) {
    if (pr.type === 'cap') {
      const ba = _ba.subVectors(pr.b, pr.a), pa = _pa.subVectors(p, pr.a);
      const l2 = ba.lengthSq();
      const t = l2 > 1e-12 ? THREE.MathUtils.clamp(pa.dot(ba) / l2, 0, 1) : 0;
      return pa.sub(ba.multiplyScalar(t)).length() - THREE.MathUtils.lerp(pr.r0, pr.r1, t);
    }
    const q = _pa.subVectors(p, pr.c);
    if (pr.q) q.applyQuaternion(pr.q);
    const k0 = Math.hypot(q.x / pr.r.x, q.y / pr.r.y, q.z / pr.r.z);
    const k1 = Math.hypot(q.x / (pr.r.x * pr.r.x), q.y / (pr.r.y * pr.r.y), q.z / (pr.r.z * pr.r.z));
    return k1 > 1e-6 ? k0 * (k0 - 1) / k1 : -Math.min(pr.r.x, pr.r.y, pr.r.z);
  }
  bounds(pr) {
    if (pr.type === 'cap') {
      const r = Math.max(pr.r0, pr.r1) + pr.k;
      return [new V().copy(pr.a).min(pr.b).subScalar(r), new V().copy(pr.a).max(pr.b).addScalar(r)];
    }
    const r = Math.max(pr.r.x, pr.r.y, pr.r.z) + pr.k;
    return [pr.c.clone().subScalar(r), pr.c.clone().addScalar(r)];
  }

  // Build a skinned geometry. bones: array of THREE.Bone; materials are picked by prim.mat.
  // noise: surface displacement amount (m) for knobbly/rocky skins.
  build({ cell = 0.018, noise = 0, noiseScale = 8 } = {}) {
    const min = new V(Infinity, Infinity, Infinity), max = new V(-Infinity, -Infinity, -Infinity);
    for (const pr of this.prims) { const [a, b] = this.bounds(pr); min.min(a); max.max(b); }
    min.subScalar(cell * 2); max.addScalar(cell * 2);
    const nx = Math.ceil((max.x - min.x) / cell) + 1, ny = Math.ceil((max.y - min.y) / cell) + 1, nz = Math.ceil((max.z - min.z) / cell) + 1;
    const N = nx * ny * nz;
    const field = new Float32Array(N).fill(1), owner = new Int16Array(N).fill(-1), ownD = new Float32Array(N).fill(1e9);
    const p = new V();
    const idx = (i, j, k) => (k * ny + j) * nx + i;
    // union each primitive into the field over its own box only
    this.prims.forEach((pr, pi) => {
      const [a, b] = this.bounds(pr);
      const i0 = Math.max(0, Math.floor((a.x - min.x) / cell)), i1 = Math.min(nx - 1, Math.ceil((b.x - min.x) / cell));
      const j0 = Math.max(0, Math.floor((a.y - min.y) / cell)), j1 = Math.min(ny - 1, Math.ceil((b.y - min.y) / cell));
      const k0 = Math.max(0, Math.floor((a.z - min.z) / cell)), k1 = Math.min(nz - 1, Math.ceil((b.z - min.z) / cell));
      const kk = pr.k;
      for (let k = k0; k <= k1; k++) for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
        p.set(min.x + i * cell, min.y + j * cell, min.z + k * cell);
        const d = this.dist(p, pr), n = idx(i, j, k), f = field[n];
        const h = Math.max(kk - Math.abs(d - f), 0) / kk;
        field[n] = Math.min(d, f) - h * h * kk * 0.25;
        if (d < ownD[n]) { ownD[n] = d; owner[n] = pi; }
      }
    });
    for (const s of this.subs) {
      const [a, b] = this.bounds(s);
      for (let k = Math.max(0, Math.floor((a.z - min.z) / cell)); k <= Math.min(nz - 1, Math.ceil((b.z - min.z) / cell)); k++)
        for (let j = Math.max(0, Math.floor((a.y - min.y) / cell)); j <= Math.min(ny - 1, Math.ceil((b.y - min.y) / cell)); j++)
          for (let i = Math.max(0, Math.floor((a.x - min.x) / cell)); i <= Math.min(nx - 1, Math.ceil((b.x - min.x) / cell)); i++) {
            p.set(min.x + i * cell, min.y + j * cell, min.z + k * cell);
            const d = -this.dist(p, s), n = idx(i, j, k), f = field[n];
            const h = Math.max(s.k - Math.abs(d - f), 0) / s.k;
            field[n] = Math.max(d, f) + h * h * s.k * 0.25;
          }
    }
    if (noise) for (let k = 0; k < nz; k++) for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) {
      const n = idx(i, j, k);
      if (Math.abs(field[n]) < noise * 3) field[n] += fbm((min.x + i * cell) * noiseScale, (min.y + j * cell) * noiseScale, (min.z + k * cell) * noiseScale, 3) * noise;
    }
    // marching cubes with shared edge vertices
    const pos = [], nor = [], mat = [], tris = [];
    const edgeVert = new Map();
    const corner = [[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0], [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1]];
    const edges = [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6], [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]];
    const F = (i, j, k) => field[idx(Math.min(nx - 1, Math.max(0, i)), Math.min(ny - 1, Math.max(0, j)), Math.min(nz - 1, Math.max(0, k)))];
    const grad = (i, j, k) => new V(F(i + 1, j, k) - F(i - 1, j, k), F(i, j + 1, k) - F(i, j - 1, k), F(i, j, k + 1) - F(i, j, k - 1));
    const cv = new Float32Array(8);
    for (let k = 0; k < nz - 1; k++) for (let j = 0; j < ny - 1; j++) for (let i = 0; i < nx - 1; i++) {
      let cube = 0;
      for (let c = 0; c < 8; c++) { const v = cv[c] = field[idx(i + corner[c][0], j + corner[c][1], k + corner[c][2])]; if (v < 0) cube |= 1 << c; }
      const em = edgeTable[cube];
      if (!em) continue;
      const vid = new Int32Array(12);
      for (let e = 0; e < 12; e++) {
        if (!(em & (1 << e))) continue;
        const [c0, c1] = edges[e];
        const a = corner[c0], b = corner[c1];
        const ia = idx(i + a[0], j + a[1], k + a[2]), ib = idx(i + b[0], j + b[1], k + b[2]);
        const key = ia < ib ? ia * 4 + (b[0] - a[0] + (b[1] - a[1]) * 2 + (b[2] - a[2]) * 3) : ib * 4 + (a[0] - b[0] + (a[1] - b[1]) * 2 + (a[2] - b[2]) * 3);
        let v = edgeVert.get(key);
        if (v === undefined) {
          const t = cv[c0] / (cv[c0] - cv[c1]);
          const gx = i + a[0] + (b[0] - a[0]) * t, gy = j + a[1] + (b[1] - a[1]) * t, gz = k + a[2] + (b[2] - a[2]) * t;
          pos.push(min.x + gx * cell, min.y + gy * cell, min.z + gz * cell);
          const g0 = grad(i + a[0], j + a[1], k + a[2]), g1 = grad(i + b[0], j + b[1], k + b[2]);
          const g = g0.lerp(g1, t).normalize();
          nor.push(g.x, g.y, g.z);
          const o = owner[t < 0.5 ? ia : ib];
          mat.push(o >= 0 ? this.prims[o].mat : 0);
          v = pos.length / 3 - 1;
          edgeVert.set(key, v);
        }
        vid[e] = v;
      }
      for (let t = cube * 16; triTable[t] !== -1; t += 3) tris.push(vid[triTable[t]], vid[triTable[t + 1]], vid[triTable[t + 2]]);
    }
    // group triangles by material (majority of corners)
    const byMat = new Map();
    for (let t = 0; t < tris.length; t += 3) {
      const m = [mat[tris[t]], mat[tris[t + 1]], mat[tris[t + 2]]];
      const mm = m[0] === m[1] || m[0] === m[2] ? m[0] : m[1];
      if (!byMat.has(mm)) byMat.set(mm, []);
      byMat.get(mm).push(tris[t], tris[t + 1], tris[t + 2]);
    }
    const index = [], geo = new THREE.BufferGeometry();
    for (const [m, list] of [...byMat.entries()].sort((a, b) => a[0] - b[0])) { geo.addGroup(index.length, list.length, m); for (let i = 0; i < list.length; i++) index.push(list[i]); }
    geo.setIndex(pos.length / 3 > 65535 ? new THREE.Uint32BufferAttribute(index, 1) : new THREE.Uint16BufferAttribute(index, 1));
    geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    geo.setAttribute('normal', new THREE.Float32BufferAttribute(nor, 3));
    this.skinWeights(geo);
    geo.computeBoundingSphere();
    return geo;
  }

  // weights: each vertex leans on the bones of the nearest few primitives
  skinWeights(geo) {
    const P = geo.attributes.position, n = P.count;
    const si = new Uint16Array(n * 4), sw = new Float32Array(n * 4);
    const p = new V();
    const bonePrims = this.prims.filter(pr => pr.bone != null);
    for (let v = 0; v < n; v++) {
      p.fromBufferAttribute(P, v);
      const acc = new Map();
      for (const pr of bonePrims) {
        const d = Math.max(0, this.dist(p, pr));
        const w = Math.exp(-d / 0.025) + 1e-6;
        acc.set(pr.bone, (acc.get(pr.bone) || 0) + w);
      }
      const top = [...acc.entries()].sort((a, b) => b[1] - a[1]).slice(0, 4);
      const sum = top.reduce((s, e) => s + e[1], 0) || 1;
      top.forEach(([b, w], i) => { si[v * 4 + i] = b; sw[v * 4 + i] = w / sum; });
    }
    geo.setAttribute('skinIndex', new THREE.Uint16BufferAttribute(si, 4));
    geo.setAttribute('skinWeight', new THREE.Float32BufferAttribute(sw, 4));
  }
}
const _ba = new V(), _pa = new V();

// Skin material: PBR with fine detail bump-mapped from a triplanar height texture sampled in the
// rest pose (so the pores, scales or fur stay put as the body moves).
export function skinMaterial({ color = 0xffffff, map = null, height, scale = 12, bump = 1, roughness = 0.6, sheen = 0, sheenColor = 0xffffff, clearcoat = 0, metalness = 0, emissive = 0x000000, emissiveIntensity = 1, transmission = 0, thickness = 0 } = {}) {
  const m = lite(new THREE.MeshPhysicalMaterial({ color, roughness, metalness, sheen, sheenColor: new THREE.Color(sheenColor), sheenRoughness: 0.5, clearcoat, clearcoatRoughness: 0.3, emissive, emissiveIntensity, transmission, thickness }));
  const u = { hTex: { value: height }, aTex: { value: map }, tScale: { value: scale }, tBump: { value: bump } };
  m.onBeforeCompile = (sh) => {
    Object.assign(sh.uniforms, u);
    sh.vertexShader = sh.vertexShader
      .replace('#include <common>', '#include <common>\nvarying vec3 vRest; varying vec3 vRestN;')
      .replace('#include <begin_vertex>', '#include <begin_vertex>\nvRest = position; vRestN = normal;');
    sh.fragmentShader = sh.fragmentShader
      .replace('#include <common>', `#include <common>
uniform sampler2D hTex; uniform sampler2D aTex; uniform float tScale; uniform float tBump;
varying vec3 vRest; varying vec3 vRestN;
vec3 triW() { vec3 w = pow(abs(normalize(vRestN)), vec3(4.0)); return w / (w.x + w.y + w.z); }
vec4 triAt(sampler2D t, vec3 q) { vec3 p = q * tScale, w = triW(); return texture2D(t, p.zy) * w.x + texture2D(t, p.xz) * w.y + texture2D(t, p.xy) * w.z; }
vec4 triS(sampler2D t) { return triAt(t, vRest); }
vec3 perturbArb(vec3 surf_pos, vec3 surf_norm, vec2 dHdxy) {
  vec3 vSigmaX = dFdx(surf_pos), vSigmaY = dFdy(surf_pos), vN = surf_norm;
  vec3 R1 = cross(vSigmaY, vN), R2 = cross(vN, vSigmaX);
  float fDet = dot(vSigmaX, R1);
  vec3 vGrad = sign(fDet) * (dHdxy.x * R1 + dHdxy.y * R2);
  return normalize(abs(fDet) * surf_norm - vGrad);
}`)
      .replace('#include <map_fragment>', `#include <map_fragment>
${map ? 'diffuseColor.rgb *= triS(aTex).rgb * 1.35;' : ''}`)
      .replace('#include <normal_fragment_maps>', `
{
  // finite differences of the height one pixel over (smooth, unlike dFdx of the height itself)
  float hgt = triS(hTex).r;
  vec2 dHdxy = vec2(triAt(hTex, vRest + dFdx(vRest)).r - hgt, triAt(hTex, vRest + dFdy(vRest)).r - hgt) * tBump;
  normal = perturbArb(-vViewPosition, normal, dHdxy);
}`);
  };
  m.customProgramCacheKey = () => 'skin' + (map ? 'm' : '');
  return m;
}
