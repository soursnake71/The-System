// A river: dark, glossy water with two scrolling ripple normal maps, reflecting the sky.
import * as THREE from 'three';
import { waterNormal } from '../core/texgen.js';

export function water(width, length, { color = 0x16302f, opacity = 0.9 } = {}) {
  const n = waterNormal();
  const m = new THREE.MeshPhysicalMaterial({
    color, roughness: 0.04, metalness: 0, transparent: true, opacity, normalMap: n, normalScale: new THREE.Vector2(0.35, 0.35),
    clearcoat: 1, clearcoatRoughness: 0.08, envMapIntensity: 1.2,
  });
  const u = { uT: { value: 0 } };
  m.onBeforeCompile = (sh) => {
    sh.uniforms.uT = u.uT;
    sh.fragmentShader = sh.fragmentShader
      .replace('#include <common>', '#include <common>\nuniform float uT;')
      .replace('#include <normal_fragment_maps>', `
vec3 n1 = texture2D(normalMap, vNormalMapUv * 6.0 + vec2(uT * 0.03, uT * 0.11)).xyz * 2.0 - 1.0;
vec3 n2 = texture2D(normalMap, vNormalMapUv * 11.0 + vec2(-uT * 0.05, uT * 0.07)).xyz * 2.0 - 1.0;
vec3 mapN = normalize(vec3((n1.xy + n2.xy) * normalScale, n1.z * n2.z));
normal = normalize(tbn * mapN);`);
  };
  m.customProgramCacheKey = () => 'river';
  const geo = new THREE.PlaneGeometry(width, length, 1, 1);
  geo.rotateX(-Math.PI / 2);
  const mesh = new THREE.Mesh(geo, m);
  mesh.receiveShadow = true;
  mesh.userData.update = (dt) => { u.uT.value += dt; };
  return mesh;
}
