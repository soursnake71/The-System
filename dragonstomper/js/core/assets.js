// Loading textures and models. All of the photographic material comes from Poly Haven (CC0).
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { gfx, preset } from './gfx.js';

const texLoader = new THREE.TextureLoader();
const cache = new Map();
const pending = new Set();
export const progress = { total: 0, done: 0 };

function track(p) {
  progress.total++;
  pending.add(p);
  p.finally(() => { progress.done++; pending.delete(p); });
  return p;
}
export const allLoaded = () => Promise.all([...pending]);

export function texture(url, { srgb = false, repeat = null } = {}) {
  const key = url + (srgb ? '|s' : '');
  let t = cache.get(key);
  if (!t) {
    let done;
    track(new Promise(res => { done = res; setTimeout(res, 30000); }));
    t = texLoader.load(url, done, undefined, done);
    t.wrapS = t.wrapT = THREE.RepeatWrapping;
    t.colorSpace = srgb ? THREE.SRGBColorSpace : THREE.NoColorSpace;
    t.anisotropy = Math.min(preset().aniso, gfx.renderer.capabilities.getMaxAnisotropy());
    cache.set(key, t);
  }
  if (repeat) { t = t.clone(); t.repeat.set(repeat[0], repeat[1]); t.needsUpdate = true; }
  return t;
}

// A Poly Haven material: <name>_diff.jpg, _nor.jpg, _arm.jpg (AO, roughness, metalness)
export function pbr(name, { repeat = [1, 1], color = 0xffffff, normalScale = 1, roughness = 1, metalness = 1, side = THREE.FrontSide, extra = {} } = {}) {
  const base = 'assets/tex/' + name;
  const map = texture(base + '_diff.jpg', { srgb: true, repeat });
  const normalMap = texture(base + '_nor.jpg', { repeat });
  const arm = texture(base + '_arm.jpg', { repeat });
  return new THREE.MeshStandardMaterial({
    map, normalMap, normalScale: new THREE.Vector2(normalScale, normalScale),
    aoMap: arm, roughnessMap: arm, metalnessMap: arm, roughness, metalness, color, side, ...extra,
  });
}
// raw textures of a set, for custom shaders
export function pbrMaps(name) {
  const base = 'assets/tex/' + name;
  return { map: texture(base + '_diff.jpg', { srgb: true }), normal: texture(base + '_nor.jpg'), arm: texture(base + '_arm.jpg') };
}

const gltf = new GLTFLoader();
const models = new Map();
export function model(name) {
  if (!models.has(name)) {
    models.set(name, track(gltf.loadAsync(`assets/models/${name}/${name}.gltf`).then(g => {
      g.scene.traverse(o => {
        if (o.isMesh) {
          o.castShadow = o.receiveShadow = true;
          for (const m of [].concat(o.material)) for (const k of ['map', 'normalMap', 'roughnessMap', 'metalnessMap', 'aoMap'])
            if (m[k]) m[k].anisotropy = Math.min(preset().aniso, gfx.renderer.capabilities.getMaxAnisotropy());
        }
      });
      return g.scene;
    }).catch(e => { console.warn('model missing', name, e.message); return new THREE.Group(); })));
  }
  return models.get(name).then(s => s.clone(true));
}
// A model scaled so its largest horizontal size is `size` metres, sitting on y = 0
export async function prop(name, size) {
  const s = await model(name);
  const box = new THREE.Box3().setFromObject(s);
  const dim = box.getSize(new THREE.Vector3());
  const k = size / Math.max(dim.x, dim.z);
  s.scale.setScalar(k);
  s.position.y = -box.min.y * k;
  const g = new THREE.Group(); g.add(s);
  return g;
}
