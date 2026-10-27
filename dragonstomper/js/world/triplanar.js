// A PBR material that projects its textures from three directions using world position,
// for surfaces with no sensible UVs (cave walls, cliffs, boulders).
import * as THREE from 'three';
import { pbrMaps } from '../core/assets.js';

export function triplanar(name, { scale = 4, color = 0xffffff, side = THREE.FrontSide, roughness = 1, normalScale = 1.2 } = {}) {
  const T = pbrMaps(name);
  const m = new THREE.MeshStandardMaterial({ color, roughness, metalness: 0, side });
  const uniforms = { tMap: { value: T.map }, tNor: { value: T.normal }, tArm: { value: T.arm }, tScale: { value: scale }, tNS: { value: normalScale } };
  m.onBeforeCompile = (sh) => {
    Object.assign(sh.uniforms, uniforms);
    sh.vertexShader = sh.vertexShader
      .replace('#include <common>', '#include <common>\nvarying vec3 vTP; varying vec3 vTN;')
      .replace('#include <worldpos_vertex>', '#include <worldpos_vertex>\nvTP = (modelMatrix * vec4(transformed, 1.0)).xyz;\nvTN = normalize(mat3(modelMatrix) * objectNormal);');
    sh.fragmentShader = sh.fragmentShader
      .replace('#include <common>', `#include <common>
uniform sampler2D tMap, tNor, tArm; uniform float tScale, tNS;
varying vec3 vTP; varying vec3 vTN;
vec3 tw;
vec4 tri(sampler2D t) { vec3 p = vTP / tScale; return texture2D(t, p.zy) * tw.x + texture2D(t, p.xz) * tw.y + texture2D(t, p.xy) * tw.z; }`)
      .replace('#include <map_fragment>', `
tw = pow(abs(normalize(vTN)), vec3(4.0)); tw /= (tw.x + tw.y + tw.z);
diffuseColor.rgb *= tri(tMap).rgb;`)
      .replace('#include <roughnessmap_fragment>', `float roughnessFactor = roughness; { vec4 a = tri(tArm); roughnessFactor *= a.g; diffuseColor.rgb *= mix(1.0, a.r, 0.8); }`)
      .replace('#include <normal_fragment_maps>', `
{
  vec3 wn = normalize(vTN);
  vec3 p = vTP / tScale;
  // whiteout-blended triplanar normals, in world space, then to view space
  vec3 nx = texture2D(tNor, p.zy).xyz * 2.0 - 1.0, ny = texture2D(tNor, p.xz).xyz * 2.0 - 1.0, nz = texture2D(tNor, p.xy).xyz * 2.0 - 1.0;
  nx.xy *= tNS; ny.xy *= tNS; nz.xy *= tNS;
  nx = vec3(nx.xy + wn.zy, abs(nx.z) * wn.x);
  ny = vec3(ny.xy + wn.xz, abs(ny.z) * wn.y);
  nz = vec3(nz.xy + wn.xy, abs(nz.z) * wn.z);
  vec3 w = normalize(nx.zyx * tw.x + ny.xzy * tw.y + nz.xyz * tw.z);
  normal = normalize((viewMatrix * vec4(w, 0.0)).xyz);
  #ifdef DOUBLE_SIDED
    normal *= faceDirection;
  #endif
}`);
  };
  m.customProgramCacheKey = () => 'tri-' + name;
  return m;
}
