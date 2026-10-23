// Warriors you enlist in the village walk beside you and fight in the cave.
import * as THREE from '../lib/three.module.min.js';
import { ROM } from '../data.js';
import { G } from './engine.js';
import { fromSprite, meshGeometry } from './voxel.js';

const geos = {};
export function warriorMesh(scale = 1.0) {
  const sp = ROM.part2.sprites.WARRIOR;
  let geo = geos[scale];
  if (!geo) { geo = geos[scale] = meshGeometry(fromSprite(sp.rows, sp.color, 'taper', { depth: 4, recess: false }), 0.3 * scale, 0.22 * scale, 0.3 * scale); geo.userData.keep = true; }
  const m = new THREE.Mesh(geo, new THREE.MeshLambertMaterial({ vertexColors: true }));
  m.castShadow = true;
  const g = new THREE.Group(); g.add(m);
  return g;
}

export class Followers {
  constructor(n, scene) {
    this.list = [];
    for (let i = 0; i < n; i++) this.add(scene, i);
  }
  add(scene, i = this.list.length) {
    const g = warriorMesh();
    g.position.copy(G.hero.position).add(new THREE.Vector3((i - 1) * 2, 0, 3));
    scene.add(g);
    const f = {
      g, hp: 15, alive: true, t: Math.random() * 5, lungeT: 0,
      lunge() { this.lungeT = 1; },
      die() {
        this.alive = false; G.p.warriors = Math.max(0, G.p.warriors - 1);
        G.fx.burst(this.g.position.clone().setY(1.5), ['#fcbc74', '#940000'], 30, { speed: 5, up: 6 });
        this.g.visible = false;
      },
    };
    this.list.push(f);
    return f;
  }
  get alive() { return this.list.filter(f => f.alive); }
  update(dt, ground = () => 0, target = null) {
    const h = G.hero;
    this.alive.forEach((f, i) => {
      f.t += dt;
      // flank the hero rather than walk between the hero and the camera
      const ang = h.rotation.y + [1.6, -1.6, 2.4][i % 3];
      const want = (target && target(f, i)) || new THREE.Vector3(h.position.x + Math.sin(ang) * 3, 0, h.position.z + Math.cos(ang) * 3);
      const d = want.clone().sub(f.g.position); d.y = 0;
      const dist = d.length();
      if (dist > 0.4) {
        const sp = Math.min(dist * 3, target ? 9 : 10);
        f.g.position.addScaledVector(d.normalize(), sp * dt);
        f.g.rotation.y = Math.atan2(d.x, d.z);
      }
      if (f.lungeT > 0) f.lungeT = Math.max(0, f.lungeT - dt * 3);
      f.g.position.y = ground(f.g.position.x, f.g.position.z) + (dist > 0.4 ? Math.abs(Math.sin(f.t * 10)) * 0.2 : 0) + Math.sin(f.lungeT * Math.PI) * 0.6;
    });
  }
}
