// Renderer, post-processing and quality settings.
// Physically based lighting with an HDR sky for image-based light, ACES tone mapping,
// ground-truth ambient occlusion, bloom and SMAA. Render resolution can go up to 4K
// (or supersample to 4K on a smaller screen).
import * as THREE from 'three';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { GTAOPass } from 'three/addons/postprocessing/GTAOPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { SMAAPass } from 'three/addons/postprocessing/SMAAPass.js';
import { ShaderPass } from 'three/addons/postprocessing/ShaderPass.js';
import { RGBELoader } from 'three/addons/loaders/RGBELoader.js';

export const PRESETS = {
  low: { shadow: 1024, ao: false, bloom: true, grass: 50000, shadowDist: 40, aniso: 2, treeDensity: 0.5 },
  medium: { shadow: 2048, ao: false, bloom: true, grass: 110000, shadowDist: 55, aniso: 4, treeDensity: 0.75 },
  high: { shadow: 4096, ao: true, bloom: true, grass: 220000, shadowDist: 70, aniso: 8, treeDensity: 1 },
  ultra: { shadow: 4096, ao: true, bloom: true, grass: 380000, shadowDist: 90, aniso: 16, treeDensity: 1.25 },
};
// render resolution: a multiple of the screen's own pixels, or a fixed 4K target
export const RESOLUTIONS = {
  '50': { label: '50%', scale: 0.5 }, '75': { label: '75%', scale: 0.75 }, native: { label: 'Native', scale: 1 },
  '4k': { label: '4K (3840x2160)', target: 3840 },
};

const loadSettings = () => { try { return JSON.parse(localStorage.getItem('ds-settings')) || {}; } catch (e) { return {}; } };
export const settings = {
  quality: 'high', resolution: 'native', fov: 70, sensitivity: 1, invertY: false, view: 'third', volume: 0.8, music: 0.6, motionBlur: false,
  ...loadSettings(),
};
export function saveSettings() { try { localStorage.setItem('ds-settings', JSON.stringify(settings)); } catch (e) {} }

export const gfx = { renderer: null, composer: null, camera: null, scene: null, sun: null, passes: {}, time: 0 };

export function initGfx(canvas) {
  const r = new THREE.WebGLRenderer({ canvas, antialias: false, powerPreference: 'high-performance', stencil: false });
  r.outputColorSpace = THREE.SRGBColorSpace;
  r.toneMapping = THREE.ACESFilmicToneMapping;
  r.toneMappingExposure = 0.85;
  r.shadowMap.enabled = true;
  r.shadowMap.type = THREE.PCFSoftShadowMap;
  gfx.renderer = r;
  gfx.camera = new THREE.PerspectiveCamera(settings.fov, 16 / 9, 0.05, 2500);
  gfx.pmrem = new THREE.PMREMGenerator(r);
  addEventListener('resize', () => resize());
  return r;
}

export const preset = () => PRESETS[settings.quality] || PRESETS.high;

// Build the post-processing chain for a scene
export function setScene(scene) {
  gfx.scene = scene;
  buildComposer();
  resize();
}

function buildComposer() {
  const r = gfx.renderer, p = preset();
  if (gfx.composer) gfx.composer.dispose();
  const size = renderSize();
  const rt = new THREE.WebGLRenderTarget(size.w, size.h, { type: THREE.HalfFloatType, samples: 0 });
  const c = new EffectComposer(r, rt);
  c.setPixelRatio(1);
  c.setSize(size.w, size.h);
  c.addPass(new RenderPass(gfx.scene, gfx.camera));
  gfx.passes = {};
  if (p.ao) {
    const ao = new GTAOPass(gfx.scene, gfx.camera, size.w, size.h);
    ao.blendIntensity = 0.85;
    ao.updateGtaoMaterial({ radius: 0.6, distanceExponent: 1.5, thickness: 2, scale: 1, samples: 12 });
    ao.updatePdMaterial({ lumaPhi: 10, depthPhi: 2, normalPhi: 3, radius: 6, rings: 2, samples: 12 });
    c.addPass(ao); gfx.passes.ao = ao;
  }
  if (p.bloom) {
    const b = new UnrealBloomPass(new THREE.Vector2(size.w, size.h), 0.35, 0.6, 0.92);
    c.addPass(b); gfx.passes.bloom = b;
  }
  // light shafts: march from each pixel toward the sun, gathering the bright sky that
  // shows between leaves and over ridges (works on the HDR image, before tone mapping)
  if (gfx.scene.userData.godRays) {
    const gr = new ShaderPass(GodRays);
    c.addPass(gr); gfx.passes.godRays = gr;
  }
  c.addPass(new OutputPass());
  const smaa = new SMAAPass(size.w, size.h);
  c.addPass(smaa);
  gfx.composer = c;
}

export function renderSize() {
  const w = innerWidth, h = innerHeight;
  const res = RESOLUTIONS[settings.resolution] || RESOLUTIONS.native;
  let scale = res.target ? res.target / w : (res.scale || 1) * Math.min(devicePixelRatio || 1, 2);
  // never go above 4K wide, so a big supersample can't eat all the video memory
  scale = Math.min(scale, 3840 / w);
  return { w: Math.max(320, Math.round(w * scale)), h: Math.max(180, Math.round(h * scale)), scale };
}

export function resize() {
  const r = gfx.renderer;
  if (!r) return;
  const s = renderSize();
  r.setPixelRatio(1);
  r.setSize(s.w, s.h, false);
  r.domElement.style.width = innerWidth + 'px';
  r.domElement.style.height = innerHeight + 'px';
  gfx.camera.aspect = innerWidth / innerHeight;
  gfx.camera.fov = settings.fov;
  gfx.camera.updateProjectionMatrix();
  if (gfx.composer) {
    gfx.composer.setSize(s.w, s.h);
    for (const p of Object.values(gfx.passes)) p.setSize?.(s.w, s.h);
  }
  gfx.renderSize = s;
}

export function applyQuality() {
  const p = preset();
  if (gfx.sun) {
    gfx.sun.shadow.mapSize.set(p.shadow, p.shadow);
    gfx.sun.shadow.map?.dispose(); gfx.sun.shadow.map = null;
    const d = p.shadowDist;
    Object.assign(gfx.sun.shadow.camera, { left: -d, right: d, top: d, bottom: -d });
    gfx.sun.shadow.camera.updateProjectionMatrix();
  }
  if (gfx.scene) buildComposer();
  resize();
}

const GodRays = {
  uniforms: { tDiffuse: { value: null }, sunUv: { value: new THREE.Vector2(0.5, 0.5) }, strength: { value: 0 }, aspect: { value: 1 }, tint: { value: new THREE.Color(1, 0.92, 0.8) } },
  vertexShader: `varying vec2 vUv; void main() { vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }`,
  fragmentShader: `
    uniform sampler2D tDiffuse; uniform vec2 sunUv; uniform float strength; uniform float aspect; uniform vec3 tint; varying vec2 vUv;
    void main() {
      vec4 base = texture2D(tDiffuse, vUv);
      if (strength <= 0.001) { gl_FragColor = base; return; }
      vec2 d = (sunUv - vUv) / 48.0;
      vec2 uv = vUv; float decay = 1.0; vec3 acc = vec3(0.0);
      float jitter = fract(sin(dot(vUv, vec2(12.9898, 78.233))) * 43758.5453);
      uv += d * jitter;
      for (int i = 0; i < 48; i++) {
        uv += d;
        vec3 c = texture2D(tDiffuse, uv).rgb;
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        acc += c * smoothstep(2.5, 9.0, l) * decay;
        decay *= 0.965;
      }
      vec2 off = (vUv - sunUv) * vec2(aspect, 1.0);
      float fall = 1.0 / (1.0 + dot(off, off) * 9.0);
      gl_FragColor = vec4(base.rgb + acc / 48.0 * strength * fall * tint, base.a);
    }`,
};
const _sv = new THREE.Vector3();
function updateGodRays() {
  const gr = gfx.passes.godRays;
  if (!gr || !gfx.sun) return;
  const cam = gfx.camera, u = gr.uniforms;
  _sv.copy(gfx.sun.userData.dir).multiplyScalar(1000).add(cam.position).project(cam);
  const facing = cam.getWorldDirection(new THREE.Vector3()).dot(gfx.sun.userData.dir);
  u.sunUv.value.set(_sv.x * 0.5 + 0.5, _sv.y * 0.5 + 0.5);
  u.aspect.value = cam.aspect;
  const edge = Math.max(Math.abs(_sv.x), Math.abs(_sv.y));
  u.strength.value = facing <= 0 ? 0 : THREE.MathUtils.smoothstep(facing, 0.0, 0.5) * (1 - THREE.MathUtils.smoothstep(edge, 1.2, 2.2)) * (gfx.scene.userData.godRays || 0);
  if (gfx.scene.userData.sunTint) u.tint.value.set(gfx.scene.userData.sunTint);
}

export function render() {
  updateGodRays();
  if (gfx.composer) gfx.composer.render();
  else gfx.renderer.render(gfx.scene, gfx.camera);
}

// ---------- sky and sun ----------
const hdrCache = {};
export function loadHDR(url) {
  if (!hdrCache[url]) hdrCache[url] = new RGBELoader().loadAsync(url).then(t => {
    t.mapping = THREE.EquirectangularReflectionMapping;
    const env = gfx.pmrem.fromEquirectangular(t).texture;
    return { background: t, env };
  });
  return hdrCache[url];
}

// A directional sun whose shadow box follows the player
export function addSun(scene, { dir = new THREE.Vector3(-0.45, 0.8, 0.35), color = 0xfff1dc, intensity = 2.6 } = {}) {
  const p = preset();
  const sun = new THREE.DirectionalLight(color, intensity);
  sun.castShadow = true;
  sun.shadow.mapSize.set(p.shadow, p.shadow);
  const d = p.shadowDist;
  Object.assign(sun.shadow.camera, { left: -d, right: d, top: d, bottom: -d, near: 1, far: 400 });
  sun.shadow.bias = -0.0004;
  sun.shadow.normalBias = 0.03;
  sun.userData.dir = dir.clone().normalize();
  scene.add(sun, sun.target);
  gfx.sun = sun;
  return sun;
}

export function followSun(pos) {
  const s = gfx.sun;
  if (!s) return;
  // snap to shadow texels so shadows don't shimmer as you walk
  const d = preset().shadowDist * 2 / s.shadow.mapSize.x;
  const x = Math.round(pos.x / d) * d, z = Math.round(pos.z / d) * d;
  s.target.position.set(x, pos.y, z);
  s.position.set(x, pos.y, z).addScaledVector(s.userData.dir, 180);
}
