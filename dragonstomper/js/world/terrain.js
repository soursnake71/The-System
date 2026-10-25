// A heightfield terrain with a splatting PBR shader: grass, muddy path and rock (on steep
// slopes) from Poly Haven scans, blended per pixel with their normal and roughness maps.
import * as THREE from 'three';
import { pbrMaps } from '../core/assets.js';

export class Terrain {
  // heightFn(x, z) -> metres, pathFn(x, z) -> 0..1 dirt amount, grassFn(x, z) -> 0..1 grass density
  constructor({ x0, x1, z0, z1, res = 2, heightFn, pathFn = () => 0, grassFn = () => 1, layers = {} }) {
    Object.assign(this, { x0, x1, z0, z1, heightFn, pathFn, grassFn });
    const w = x1 - x0, d = z1 - z0;
    const sx = Math.round(w / res), sz = Math.round(d / res);
    const geo = new THREE.PlaneGeometry(w, d, sx, sz);
    geo.rotateX(-Math.PI / 2);
    geo.translate(x0 + w / 2, 0, z0 + d / 2);
    const pos = geo.attributes.position;
    const splat = new Float32Array(pos.count * 2);
    for (let i = 0; i < pos.count; i++) {
      const x = pos.getX(i), z = pos.getZ(i);
      pos.setY(i, heightFn(x, z));
      splat[i * 2] = pathFn(x, z);
      splat[i * 2 + 1] = grassFn(x, z);
    }
    geo.setAttribute('splat', new THREE.BufferAttribute(splat, 2));
    geo.computeVertexNormals();
    geo.computeBoundingSphere();
    this.mesh = new THREE.Mesh(geo, terrainMaterial(layers));
    this.mesh.receiveShadow = true;
    this.mesh.castShadow = false;
    this.buildHeightTexture();
  }
  height(x, z) { return this.heightFn(x, z); }
  normal(x, z) {
    const e = 0.5, h = this.heightFn;
    return new THREE.Vector3(h(x - e, z) - h(x + e, z), 2 * e, h(x, z - e) - h(x, z + e)).normalize();
  }
  // height + grass density at 1 m, read by the grass shader
  buildHeightTexture() {
    const w = Math.ceil(this.x1 - this.x0), d = Math.ceil(this.z1 - this.z0);
    const data = new Float32Array(w * d * 4);
    for (let j = 0; j < d; j++) for (let i = 0; i < w; i++) {
      const x = this.x0 + i + 0.5, z = this.z0 + j + 0.5, k = (j * w + i) * 4;
      const n = this.normal(x, z);
      data[k] = this.heightFn(x, z);
      data[k + 1] = Math.max(0, this.grassFn(x, z) * (1 - this.pathFn(x, z) * 1.4)) * THREE.MathUtils.smoothstep(n.y, 0.8, 0.92);
      data[k + 2] = n.x; data[k + 3] = n.z;
    }
    const t = new THREE.DataTexture(data, w, d, THREE.RGBAFormat, THREE.FloatType);
    t.minFilter = t.magFilter = THREE.LinearFilter;
    t.needsUpdate = true;
    this.heightTex = t;
    this.heightTexRect = new THREE.Vector4(this.x0, this.z0, w, d);
  }
}

export function terrainMaterial({ grass = 'rocky_terrain_02', dirt = 'forest_ground_04', rock = 'rock_face_03', grassScale = 4, dirtScale = 2.5, rockScale = 5, tint = 0xffffff } = {}) {
  const G = pbrMaps(grass), D = pbrMaps(dirt), R = pbrMaps(rock);
  const m = new THREE.MeshStandardMaterial({ color: tint, roughness: 1, metalness: 0 });
  const uniforms = {
    gMap: { value: G.map }, gNor: { value: G.normal }, gArm: { value: G.arm },
    dMap: { value: D.map }, dNor: { value: D.normal }, dArm: { value: D.arm },
    rMap: { value: R.map }, rNor: { value: R.normal }, rArm: { value: R.arm },
    scales: { value: new THREE.Vector3(grassScale, dirtScale, rockScale) },
  };
  m.onBeforeCompile = (sh) => {
    Object.assign(sh.uniforms, uniforms);
    sh.vertexShader = sh.vertexShader
      .replace('#include <common>', '#include <common>\nattribute vec2 splat;\nvarying vec2 vSplat;\nvarying vec3 vWPos;\nvarying vec3 vWNorm;')
      .replace('#include <worldpos_vertex>', '#include <worldpos_vertex>\nvSplat = splat;\nvWPos = (modelMatrix * vec4(transformed, 1.0)).xyz;\nvWNorm = normalize(mat3(modelMatrix) * objectNormal);');
    sh.fragmentShader = sh.fragmentShader
      .replace('#include <common>', `#include <common>
uniform sampler2D gMap, gNor, gArm, dMap, dNor, dArm, rMap, rNor, rArm;
uniform vec3 scales;
varying vec2 vSplat; varying vec3 vWPos; varying vec3 vWNorm;
vec3 wts; vec3 tri;
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * .1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y); }
// sample a rock map from three directions so cliffs don't smear
vec4 triRock(sampler2D t) {
  vec3 p = vWPos / scales.z;
  return texture2D(t, p.zy) * tri.x + texture2D(t, p.xz) * tri.y + texture2D(t, p.xy) * tri.z;
}
// break up tiling: blend the grass with a rotated, rescaled copy of itself
vec4 antiTile(sampler2D t, vec2 uv) {
  float n = vnoise(uv * 0.21);
  vec2 uv2 = mat2(0.8, -0.6, 0.6, 0.8) * uv * 0.71 + 0.37;
  return mix(texture2D(t, uv), texture2D(t, uv2), smoothstep(0.35, 0.65, n));
}`)
      .replace('#include <map_fragment>', `
{
  vec3 an = abs(normalize(vWNorm)); tri = pow(an, vec3(4.0)); tri /= (tri.x + tri.y + tri.z);
  float slope = 1.0 - normalize(vWNorm).y;
  float macro = vnoise(vWPos.xz * 0.05) * 0.6 + vnoise(vWPos.xz * 0.17) * 0.4;
  float rockW = smoothstep(0.18, 0.32, slope + (macro - 0.5) * 0.1);
  vec4 dTex = antiTile(dMap, vWPos.xz / scales.y);
  float dirtW = clamp(vSplat.x + (dTex.r - 0.4) * 0.6, 0.0, 1.0) * (1.0 - rockW);
  dirtW = max(dirtW, (1.0 - smoothstep(0.0, 0.25, vSplat.y)) * (1.0 - rockW) * 0.85);
  float grassW = max(0.0, 1.0 - rockW - dirtW);
  wts = vec3(grassW, dirtW, rockW);
  vec4 gTex = antiTile(gMap, vWPos.xz / scales.x);
  gTex.rgb *= mix(vec3(0.85, 0.92, 0.75), vec3(1.08, 1.02, 0.85), macro);
  vec4 rTex = triRock(rMap);
  vec3 c = gTex.rgb * wts.x + dTex.rgb * wts.y + rTex.rgb * wts.z;
  diffuseColor.rgb *= c;
}`)
      .replace('#include <roughnessmap_fragment>', `
float roughnessFactor = roughness;
{
  vec4 ga = antiTile(gArm, vWPos.xz / scales.x), da = antiTile(dArm, vWPos.xz / scales.y), ra = triRock(rArm);
  vec4 arm = ga * wts.x + da * wts.y + ra * wts.z;
  roughnessFactor *= arm.g;
  diffuseColor.rgb *= mix(1.0, arm.r, 0.7);
}`)
      .replace('#include <normal_fragment_maps>', `
{
  vec3 nG = antiTile(gNor, vWPos.xz / scales.x).xyz * 2.0 - 1.0;
  vec3 nD = antiTile(dNor, vWPos.xz / scales.y).xyz * 2.0 - 1.0;
  vec3 nR = triRock(rNor).xyz * 2.0 - 1.0;
  vec3 nT = normalize(nG * wts.x + nD * wts.y + nR * wts.z * 1.4);
  vec3 T = normalize((viewMatrix * vec4(1.0, 0.0, 0.0, 0.0)).xyz);
  T = normalize(T - normal * dot(normal, T));
  vec3 B = cross(T, normal);
  normal = normalize(T * nT.x + B * nT.y + normal * nT.z);
}`);
  };
  m.customProgramCacheKey = () => 'terrain-splat';
  return m;
}
