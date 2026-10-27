// Forests: procedural trees from EZ-Tree (MIT), turned into instanced PBR meshes with
// Poly Haven bark and wind in the leaves.
import * as THREE from 'three';
import { Tree } from '../../lib/ez-tree.es.js';
import { pbr } from '../core/assets.js';

const VARIANTS = {
  oak: { preset: 'Oak Medium', height: 13, bark: 'bark_willow', leafTint: 0xc8d8a8 },
  oakBig: { preset: 'Oak Large', height: 18, bark: 'bark_willow', leafTint: 0xc0d0a0 },
  pine: { preset: 'Pine Medium', height: 17, bark: 'bark_willow', leafTint: 0xb8c8a8 },
  pineBig: { preset: 'Pine Large', height: 24, bark: 'bark_willow', leafTint: 0xb0c0a0 },
  ash: { preset: 'Ash Medium', height: 14, bark: 'bark_willow', leafTint: 0xd0d8a0 },
  aspen: { preset: 'Aspen Medium', height: 15, bark: 'bark_willow', leafTint: 0xd8e0a0 },
  bush: { preset: 'Bush 2', height: 2.2, bark: 'bark_willow', leafTint: 0xb0c890 },
  dead: { preset: 'Oak Small', height: 9, bark: 'bark_willow', leafTint: 0x000000, noLeaves: true },
};

const built = {};
function buildVariant(name, seed) {
  const v = VARIANTS[name];
  const t = new Tree();
  t.loadPreset(v.preset);
  t.options.seed = seed;
  if (name === 'dead') t.options.leaves.count = 0;
  else { t.options.leaves.count = Math.round(t.options.leaves.count * 1.5); t.options.leaves.size *= 1.15; }
  t.generate();
  t.updateMatrixWorld(true);
  const box = new THREE.Box3().setFromObject(t.branchesMesh);
  const k = v.height / (box.max.y - box.min.y);
  const fix = (g) => { g = g.clone(); g.scale(k, k, k); g.translate(0, -box.min.y * k, 0); g.computeBoundingSphere(); return g; };
  const bark = fix(t.branchesMesh.geometry);
  const leaves = v.noLeaves || !t.leavesMesh.geometry.attributes.position.count ? null : fix(t.leavesMesh.geometry);
  const leafMap = t.leavesMesh.material.map;
  return { bark, leaves, leafMap, v, alphaTest: t.options.leaves.alphaTest };
}

export const windUniform = { value: 0 };
function swayMaterial(mat, strength) {
  mat.onBeforeCompile = (sh) => {
    sh.uniforms.uWind = windUniform;
    sh.vertexShader = sh.vertexShader
      .replace('#include <common>', '#include <common>\nuniform float uWind;')
      .replace('#include <begin_vertex>', `#include <begin_vertex>
#ifdef USE_INSTANCING
  vec3 ip = vec3(instanceMatrix[3][0], 0.0, instanceMatrix[3][2]);
#else
  vec3 ip = vec3(0.0);
#endif
  float hk = max(0.0, position.y) * ${strength.toFixed(4)};
  transformed.x += sin(uWind * 1.3 + ip.x * 0.07 + position.y * 0.15) * hk * hk;
  transformed.z += cos(uWind * 1.1 + ip.z * 0.09 + position.x * 0.4) * hk * hk * 0.6;`);
  };
  mat.customProgramCacheKey = () => 'sway' + strength;
  return mat;
}

// places: [{ x, z, y, type, scale, rot }]
export function forest(places, { seedBase = 1 } = {}) {
  const group = new THREE.Group();
  const byType = {};
  for (const p of places) (byType[p.type] = byType[p.type] || []).push(p);
  for (const [type, list] of Object.entries(byType)) {
    // two shapes per type so the forest isn't a field of clones
    for (let s = 0; s < 2; s++) {
      const sub = list.filter((_, i) => i % 2 === s);
      if (!sub.length) continue;
      const key = type + s;
      const b = built[key] || (built[key] = buildVariant(type, seedBase * 131 + s * 977 + type.length * 31));
      const mx = new THREE.Matrix4(), q = new THREE.Quaternion(), sc = new THREE.Vector3(), ps = new THREE.Vector3();
      const barkMat = swayMaterial(pbr(b.v.bark, { repeat: [1, 6], color: 0xd8d0c8 }), 0.0025);
      const bi = new THREE.InstancedMesh(b.bark, barkMat, sub.length);
      bi.castShadow = true; bi.receiveShadow = true;
      let li = null;
      if (b.leaves) {
        const leafMat = swayMaterial(new THREE.MeshStandardMaterial({
          map: b.leafMap, alphaTest: b.alphaTest, side: THREE.DoubleSide, color: b.v.leafTint, roughness: 0.85, metalness: 0,
        }), 0.012);
        li = new THREE.InstancedMesh(b.leaves, leafMat, sub.length);
        li.castShadow = true; li.receiveShadow = true;
      }
      sub.forEach((p, i) => {
        ps.set(p.x, p.y, p.z); q.setFromAxisAngle(THREE.Object3D.DEFAULT_UP, p.rot ?? Math.random() * 6.28);
        sc.setScalar(p.scale ?? 1);
        mx.compose(ps, q, sc);
        bi.setMatrixAt(i, mx); li?.setMatrixAt(i, mx);
      });
      bi.computeBoundingSphere(); li?.computeBoundingSphere();
      group.add(bi); if (li) group.add(li);
    }
  }
  return group;
}
