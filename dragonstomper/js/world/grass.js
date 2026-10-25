// GPU grass: one instanced draw of blades around the camera. Blades sit on a fixed world
// lattice (so they don't slide as you walk), take their height from the terrain's height
// texture, sway in the wind and fade out at the edge of the patch.
import * as THREE from 'three';
import { preset } from '../core/gfx.js';

export class Grass {
  constructor(terrain, { density = 150, base = 0x18250a, tip = 0x5d7024, heightMin = 0.12, heightMax = 0.4 } = {}) {
    const count = preset().grass;
    const size = Math.sqrt(count / density);
    // a blade: four segments tapering to a point
    const segs = 4, pos = [], idx = [];
    for (let i = 0; i <= segs; i++) {
      const h = i / segs, w = 0.013 * (1 - h * 0.8);
      if (i < segs) pos.push(-w, h, 0, w, h, 0); else pos.push(0, 1, 0);
    }
    for (let i = 0; i < segs - 1; i++) { const a = i * 2; idx.push(a, a + 1, a + 2, a + 1, a + 3, a + 2); }
    const t = (segs - 1) * 2; idx.push(t, t + 1, t + 2);
    const geo = new THREE.InstancedBufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    geo.setIndex(idx);
    const ofs = new Float32Array(count * 2), rnd = new Float32Array(count * 4);
    for (let i = 0; i < count; i++) {
      ofs[i * 2] = Math.random() * size; ofs[i * 2 + 1] = Math.random() * size;
      for (let k = 0; k < 4; k++) rnd[i * 4 + k] = Math.random();
    }
    geo.setAttribute('iOffset', new THREE.InstancedBufferAttribute(ofs, 2));
    geo.setAttribute('iRand', new THREE.InstancedBufferAttribute(rnd, 4));
    geo.instanceCount = count;
    this.uniforms = {
      uCenter: { value: new THREE.Vector2() }, uSize: { value: size }, uTime: { value: 0 },
      uHeight: { value: terrain.heightTex }, uRect: { value: terrain.heightTexRect },
      uBase: { value: new THREE.Color(base) }, uTip: { value: new THREE.Color(tip) },
      uH: { value: new THREE.Vector2(heightMin, heightMax) }, uPush: { value: new THREE.Vector3(0, -999, 0) },
    };
    const m = new THREE.MeshStandardMaterial({ roughness: 0.75, metalness: 0, side: THREE.DoubleSide });
    m.onBeforeCompile = (sh) => {
      Object.assign(sh.uniforms, this.uniforms);
      sh.vertexShader = sh.vertexShader
        .replace('#include <common>', `#include <common>
attribute vec2 iOffset; attribute vec4 iRand;
uniform vec2 uCenter; uniform float uSize; uniform float uTime; uniform sampler2D uHeight; uniform vec4 uRect; uniform vec2 uH; uniform vec3 uPush;
varying float vGH; varying float vGT;`)
        .replace('#include <beginnormal_vertex>', `
vec2 wp = uCenter + mod(iOffset - uCenter + uSize * 0.5, uSize) - uSize * 0.5;
vec2 tuv = (wp - uRect.xy) / uRect.zw;
vec4 hd = texture2D(uHeight, tuv);
float inside = step(0.0, tuv.x) * step(tuv.x, 1.0) * step(0.0, tuv.y) * step(tuv.y, 1.0);
float dist = length(wp - uCenter);
float fade = 1.0 - smoothstep(uSize * 0.3, uSize * 0.5, dist);
float keep = step(iRand.w, hd.g) * inside;
float bh = mix(uH.x, uH.y, iRand.x) * keep * mix(0.15, 1.0, fade);
float ang = iRand.y * 6.2831853;
vec2 dirv = vec2(cos(ang), sin(ang));
float wind = sin(uTime * 1.6 + wp.x * 0.31 + wp.y * 0.17) * 0.6 + sin(uTime * 3.7 + wp.x * 1.3 + wp.y * 0.9) * 0.25;
// walking through grass pushes it aside
vec2 away = wp - uPush.xz; float pd = length(away);
float push = (1.0 - smoothstep(0.2, 1.1, pd)) * step(abs(hd.r - uPush.y), 1.5);
vec3 objectNormal = normalize(vec3(dirv.y * 0.35, 1.0, -dirv.x * 0.35));`)
        .replace('#include <begin_vertex>', `
float h = position.y;
vec3 transformed = vec3(position.x * mix(0.7, 1.4, iRand.z), 0.0, 0.0);
transformed = vec3(transformed.x * dirv.y, h * bh, -transformed.x * dirv.x);
float lean = (iRand.z - 0.3) * 0.5;
vec2 bendDir = normalize(vec2(1.0, 0.35)) * (wind * 0.35 + 0.1) + dirv * lean + normalize(away + 1e-4) * push * 1.4;
transformed.xz += bendDir * h * h * bh;
transformed.y *= 1.0 - push * 0.5 * h;
transformed += vec3(wp.x, hd.r - 0.02, wp.y);
vGH = h; vGT = iRand.x;`);
      sh.fragmentShader = sh.fragmentShader
        .replace('#include <common>', '#include <common>\nuniform vec3 uBase; uniform vec3 uTip;\nvarying float vGH; varying float vGT;')
        .replace('#include <normal_fragment_begin>', `#include <normal_fragment_begin>
normal = normalize(vNormal); // both sides of a blade are lit like its front
`)
        .replace('#include <map_fragment>', `
diffuseColor.rgb *= mix(uBase, uTip * mix(0.8, 1.2, vGT), pow(vGH, 1.3));`)
        .replace('#include <emissivemap_fragment>', `#include <emissivemap_fragment>
totalEmissiveRadiance += uTip * 0.05 * vGH;`);
    };
    m.customProgramCacheKey = () => 'grass';
    this.mesh = new THREE.Mesh(geo, m);
    this.mesh.frustumCulled = false;
    this.mesh.receiveShadow = true;
  }
  update(dt, camPos, playerPos) {
    this.uniforms.uTime.value += dt;
    this.uniforms.uCenter.value.set(camPos.x, camPos.z);
    if (playerPos) this.uniforms.uPush.value.copy(playerPos);
  }
}
