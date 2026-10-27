// 3D pickups for loot (items from the ROM's list, and gold), plus their inventory icons
// drawn from the original 8-pixel sprites.
import * as THREE from 'three';
import { lite } from '../core/gfx.js';
import { mats, mesh, lathe, axe } from '../actors/parts.js';
import { model } from '../core/assets.js';
import { ROM } from '../../data.js';
import { softDot } from '../core/texgen.js';

const glassM = () => lite(new THREE.MeshPhysicalMaterial({ color: 0xffffff, roughness: 0.05, transmission: 0.95, thickness: 0.05, ior: 1.5, clearcoat: 1 }));
const liquid = (c) => lite(new THREE.MeshPhysicalMaterial({ color: c, roughness: 0.1, emissive: c, emissiveIntensity: 0.6, transmission: 0.3, thickness: 0.05 }));

export function itemModel(name) {
  const g = new THREE.Group();
  switch (name) {
    case 'POTION': case 'ELIXIR': case 'MEDICINE': case 'VITAMINS': {
      const c = { POTION: 0xc02040, ELIXIR: 0xe0a020, MEDICINE: 0x30c060, VITAMINS: 0x8040c0 }[name];
      g.add(mesh(lathe([[0.001, 0], [0.06, 0.005], [0.075, 0.05], [0.07, 0.1], [0.03, 0.14], [0.022, 0.2], [0.026, 0.21]], { segs: 24 }), glassM()));
      g.add(mesh(lathe([[0.001, 0.008], [0.058, 0.012], [0.066, 0.05], [0.06, 0.09], [0.001, 0.09]], { segs: 20 }), liquid(c)));
      g.add(mesh(new THREE.CylinderGeometry(0.02, 0.018, 0.04, 10), mats.wood(), 0, 0.22, 0));
      break;
    }
    case 'KEY': {
      g.add(mesh(new THREE.TorusGeometry(0.04, 0.01, 8, 20), mats.steel(0x707070, 0.45), 0, 0.17, 0));
      g.add(mesh(new THREE.CylinderGeometry(0.009, 0.009, 0.16, 8), mats.steel(0x707070, 0.45), 0, 0.07, 0));
      g.add(mesh(new THREE.BoxGeometry(0.04, 0.03, 0.01), mats.steel(0x707070, 0.45), 0.02, 0.0, 0));
      g.rotation.z = 0.3;
      break;
    }
    case 'PAPER': {
      g.add(mesh(new THREE.CylinderGeometry(0.03, 0.03, 0.26, 16), new THREE.MeshStandardMaterial({ color: 0xe8dcb8, roughness: 0.9 }), 0, 0.03, 0)).rotation.z = Math.PI / 2;
      g.add(mesh(new THREE.TorusGeometry(0.032, 0.006, 6, 16), new THREE.MeshStandardMaterial({ color: 0x902020, roughness: 0.6 }), 0, 0.03, 0)).rotation.y = Math.PI / 2;
      break;
    }
    case 'CROSS': {
      const m = mats.steel(0xa0a0a8, 0.35);
      g.add(mesh(new THREE.BoxGeometry(0.03, 0.26, 0.02), m, 0, 0.13, 0), mesh(new THREE.BoxGeometry(0.16, 0.03, 0.02), m, 0, 0.18, 0));
      break;
    }
    case 'RING': case 'SAPPHIRE': case 'RUBY': {
      if (name === 'RING') g.add(mesh(new THREE.TorusGeometry(0.035, 0.008, 10, 24), mats.gold(), 0, 0.04, 0));
      const gem = lite(new THREE.MeshPhysicalMaterial({ color: name === 'SAPPHIRE' ? 0x2040ff : name === 'RUBY' ? 0xff1030 : 0x30ff90, roughness: 0, transmission: 0.8, thickness: 0.05, ior: 1.77, emissive: name === 'SAPPHIRE' ? 0x0a1060 : 0x500010, emissiveIntensity: 0.5 }));
      g.add(mesh(new THREE.OctahedronGeometry(name === 'RING' ? 0.016 : 0.045, 0), gem, 0, name === 'RING' ? 0.08 : 0.05, 0));
      break;
    }
    case 'STAFF': {
      g.add(mesh(new THREE.CylinderGeometry(0.014, 0.018, 1.1, 8), mats.wood(), 0, 0.55, 0));
      g.add(mesh(new THREE.SphereGeometry(0.045, 16, 12), mats.glow(0x60a0ff, 3), 0, 1.13, 0));
      g.rotation.z = 1.35;
      break;
    }
    case 'CHARM': {
      g.add(mesh(new THREE.TorusGeometry(0.05, 0.008, 8, 24), mats.gold(), 0, 0.05, 0));
      g.add(mesh(new THREE.SphereGeometry(0.02, 12, 10), mats.glow(0xff6080, 2), 0, 0.05, 0));
      g.rotation.x = -1.3;
      break;
    }
    case 'HANDAXE': { const a = axe(0.5, 1); a.rotation.z = 1.4; g.add(a); break; }
    case 'GOLD': {
      const coin = new THREE.CylinderGeometry(0.018, 0.018, 0.004, 16);
      const im = new THREE.InstancedMesh(coin, mats.gold(), 30);
      const mx = new THREE.Matrix4(), q = new THREE.Quaternion(), e = new THREE.Euler();
      for (let i = 0; i < 30; i++) {
        const a = Math.random() * 6.28, r = Math.random() * 0.1;
        e.set((Math.random() - 0.5) * 0.6, 0, (Math.random() - 0.5) * 0.6); q.setFromEuler(e);
        mx.compose(new THREE.Vector3(Math.cos(a) * r, 0.003 + (0.1 - r) * Math.random() * 0.4, Math.sin(a) * r), q, new THREE.Vector3(1, 1, 1));
        im.setMatrixAt(i, mx);
      }
      im.castShadow = true; g.add(im);
      break;
    }
    case 'SHIELD': case 'CHEST': {
      model(name === 'SHIELD' ? 'kite_shield' : 'treasure_chest').then(s => {
        const box = new THREE.Box3().setFromObject(s), dim = box.getSize(new THREE.Vector3());
        const k = (name === 'SHIELD' ? 0.7 : 0.6) / Math.max(dim.x, dim.y, dim.z);
        s.scale.setScalar(k); s.position.y = -box.min.y * k;
        if (name === 'SHIELD') s.rotation.x = -Math.PI / 2 + 0.2;
        g.add(s);
      });
      break;
    }
    default: g.add(mesh(new THREE.BoxGeometry(0.1, 0.1, 0.1), mats.gold(), 0, 0.05, 0));
  }
  return g;
}

// A pickup on the ground: slowly turning, with a soft glow beneath it
export class Pickup {
  constructor(scene, name, pos, amount = 1) {
    this.name = name; this.amount = amount; this.taken = false;
    this.group = new THREE.Group();
    this.model = itemModel(name);
    this.model.scale.setScalar(name === 'GOLD' ? 1.6 : 1.8);
    this.group.add(this.model);
    // a soft glow instead of a real light (changing the light count would recompile every shader)
    this.light = new THREE.Sprite(new THREE.SpriteMaterial({ map: softDot(), color: name === 'GOLD' ? 0xffc040 : 0xfff0c0, blending: THREE.AdditiveBlending, depthWrite: false, transparent: true, opacity: 0.5 }));
    this.light.scale.setScalar(0.9); this.light.position.y = 0.3; this.group.add(this.light);
    this.group.position.copy(pos);
    this.t = Math.random() * 6;
    scene.add(this.group);
  }
  update(dt) {
    this.t += dt;
    if (this.name !== 'GOLD' && this.name !== 'CHEST') { this.model.rotation.y += dt; this.model.position.y = 0.25 + Math.sin(this.t * 2) * 0.05; }
    this.light.material.opacity = 0.35 + Math.sin(this.t * 3) * 0.15;
  }
  remove() { this.group.parent?.remove(this.group); this.taken = true; }
}

// Inventory icons: the original 8-pixel sprite, drawn big
const ICON_ALIASES = { ELIXIR: 'POTION', MEDICINE: 'POTION', VITAMINS: 'POTION', SAPPHIRE: 'RING', RUBY: 'RING' };
export function iconCanvas(name, size = 56) {
  const it = ROM.part1.items[name] || ROM.part1.items[ICON_ALIASES[name]];
  const c = document.createElement('canvas'); c.width = c.height = size;
  const ctx = c.getContext('2d');
  if (!it) return c;
  const rows = it.rows, px = Math.floor(size / Math.max(10, rows.length + 2));
  const ox = Math.floor((size - 8 * px) / 2), oy = Math.floor((size - rows.length * px) / 2);
  const col = { ELIXIR: '#e8b030', MEDICINE: '#40c060', VITAMINS: '#9050d0', SAPPHIRE: '#4060ff', RUBY: '#ff3040' }[name] || it.color;
  ctx.fillStyle = col === '#000000' ? '#888' : col;
  rows.forEach((r, y) => { for (let x = 0; x < 8; x++) if ((r >> (7 - x)) & 1) ctx.fillRect(ox + x * px, oy + y * px, px, px); });
  return c;
}
