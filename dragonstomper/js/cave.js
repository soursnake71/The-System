// Part 3: down into the dragon's cave. Jagged ochre walls as in the original, darkness, falling
// rocks, traps, poison darts, pits, bones, chained doors - and the dragon at the bottom.
import * as THREE from '../lib/three.module.min.js';
import { ROM } from '../data.js';
import { G, newScene, addCollider, removeCollider, moveHero, mulberry, save, floatText, shake, swing } from './engine.js';
import { Grid, meshGeometry, spriteMesh, voxMat } from './voxel.js';
import { battle, hurtFx, showPickup, article, useMenu, statusScreen } from './play.js';
import { Followers } from './followers.js';
import { sfx, ambience } from './audio.js';
import * as R from './rules.js';

const SP = ROM.part3.sprites;
const LEN = 380, LAIR_Z = -LEN - 26, LAIR_R = 22;
const cx = (z) => { const k = Math.min(1, Math.max(0, (-z - 8) / 30)) * Math.min(1, Math.max(0, (LEN + 10 + z) / 30)); return k * (9 * Math.sin(z * 0.021) + 5 * Math.sin(z * 0.053 + 1)); };
const hw = (z) => 6.8 + 2.4 * Math.sin(z * 0.037 + 2);
const floorY = (z) => Math.max(-LEN, Math.min(0, z)) * 0.06;
const DRAGON_HP = 60, DRAGON_ATK = 25, DRAGON_DMG = 9;

export class Cave {
  constructor(state = {}) {
    this.state = { done: [], doors: [], vision: false, ...state };
    this.ctx = 'cave';
    this.events = [];
    this.shots = [];
    this.feverT = 0;
  }

  build() {
    const lit = G.p.goods.LANTERN > 0;
    const s = newScene({ sky: 0x000000, fog: lit ? [10, 50] : [5, 28], sun: 0, ambient: 0.4, hemiGround: 0x101008 });
    this.addWalls(s);
    this.addFloor(s);
    this.addLair(s);
    const rand = mulberry(G.p.seed ^ 0x5eed);
    let id = 0;
    const kinds = ['BONES', 'ROCK', 'MONSTER', 'TRAP', 'DARTS', 'BONES', 'PIT', 'MONSTER', 'ROCK', 'TRAP'];
    for (let z = -26, i = 0; z > -LEN + 12; z -= 17 + rand() * 9, i++) {
      if (Math.abs(z + 130) < 10 || Math.abs(z + 270) < 10) continue; // the doors
      const kind = kinds[(i + Math.floor(rand() * 3)) % kinds.length];
      const x = cx(z) + (rand() - 0.5) * hw(z) * 1.1;
      const e = { id: id++, kind, x, z, y: floorY(z) };
      if (this.state.done.includes(e.id)) e.done = true;
      this.makeEvent(s, e, rand);
      this.events.push(e);
    }
    for (const dz of [-130, -270]) {
      const e = { id: 100 + dz, kind: 'DOOR', x: cx(dz), z: dz, y: floorY(dz) };
      e.open = this.state.doors.includes(e.id);
      this.makeEvent(s, e, rand);
      this.events.push(e);
    }
    // your torch
    this.light = new THREE.PointLight(0xffb060, lit ? 60 : 30, lit ? 36 : 18, 1.2);
    s.add(this.light);
    const t = SP.TORCH;
    this.torch = spriteMesh(t.rows, t.color, 'lathe', 0.5, { material: new THREE.MeshBasicMaterial({ vertexColors: true }) });
    this.followers = new Followers(0, s);
    this.dragonHp = DRAGON_HP;
    this.makeDragon(s);
    G.walkSpeed = 6.5;
  }

  addWalls(s) {
    const rand = mulberry(77);
    const pts = [];
    for (let z = 6; z > -LEN - 4; z -= 0.9) {
      for (const side of [-1, 1]) for (let k = 0; k < 3; k++) {
        const x = cx(z) + side * (hw(z) + 0.6 + k * 1.3 + rand() * 1.2);
        pts.push([x, z + rand() * 0.6, 3 + rand() * 7 + k * 3, rand()]);
      }
    }
    for (let a = 0; a < Math.PI * 2; a += 0.05) for (let k = 0; k < 3; k++) {
      if (Math.abs(Math.atan2(Math.sin(a), Math.cos(a)) - Math.PI / 2) < 0.28 && k < 3 && Math.sin(a) > 0.9) continue; // the way in
      const r = LAIR_R + 0.8 + k * 1.4 + rand();
      pts.push([Math.cos(a) * r, LAIR_Z + Math.sin(a) * r, 8 + rand() * 9 + k * 4, rand()]);
    }
    const geo = new THREE.BoxGeometry(1.6, 1, 1.6); geo.translate(0, 0.5, 0);
    const im = new THREE.InstancedMesh(geo, new THREE.MeshLambertMaterial({ color: 0xffffff }), pts.length);
    const mx = new THREE.Matrix4(), c = new THREE.Color();
    const cols = [0xbbbb35, 0xa2a22a, 0x86861d, 0xd2d240];
    pts.forEach(([x, z, h, r], i) => {
      mx.makeScale(1, h, 1); mx.setPosition(x, floorY(z) - 1, z);
      im.setMatrixAt(i, mx);
      im.setColorAt(i, c.set(cols[Math.floor(r * 4)]));
    });
    im.receiveShadow = true;
    s.add(im);
  }

  addFloor(s) {
    const pos = [], col = [], idx = [];
    const c = new THREE.Color();
    let n = 0;
    for (let z = 8; z > -LEN - 6; z -= 1.5) {
      const w = hw(z) + 5, x0 = cx(z), y = floorY(z);
      for (let k = 0; k <= 6; k++) {
        const x = x0 - w + (2 * w * k) / 6;
        pos.push(x, y + (k === 0 || k === 6 ? 0.6 : Math.sin(x * 2 + z) * 0.08), z);
        c.set(0x6f6f6f).multiplyScalar(0.8 + 0.25 * Math.sin(x * 3.1 + z * 1.7));
        col.push(c.r, c.g, c.b);
      }
      if (n) for (let k = 0; k < 6; k++) { const a = (n - 1) * 7 + k, b = n * 7 + k; idx.push(a, b, a + 1, a + 1, b, b + 1); }
      n++;
    }
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    geo.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
    geo.setIndex(idx); geo.computeVertexNormals();
    s.add(new THREE.Mesh(geo, new THREE.MeshLambertMaterial({ vertexColors: true, side: THREE.DoubleSide })));
  }

  addLair(s) {
    const y = floorY(-LEN);
    const floor = new THREE.Mesh(new THREE.CircleGeometry(LAIR_R + 3, 48), new THREE.MeshLambertMaterial({ color: 0x4a4a4a }));
    floor.rotation.x = -Math.PI / 2; floor.position.set(0, y + 0.02, LAIR_Z); s.add(floor);
    // the hoard
    const gold = new THREE.InstancedMesh(new THREE.BoxGeometry(0.5, 0.25, 0.5), new THREE.MeshLambertMaterial({ color: 0xfce070, emissive: 0x403000 }), 260);
    const mx = new THREE.Matrix4();
    for (let i = 0; i < 260; i++) {
      const a = Math.random() * Math.PI * 2, r = Math.random() * 6;
      const h = (6 - r) * 0.35 * Math.random();
      mx.makeRotationY(Math.random() * 3); mx.setPosition(Math.cos(a) * r * 1.4, y + h, LAIR_Z - 14 + Math.sin(a) * r * 0.6);
      gold.setMatrixAt(i, mx);
    }
    s.add(gold);
    const glow = new THREE.PointLight(0xffc040, 40, 30, 1.2); glow.position.set(0, y + 4, LAIR_Z - 13); s.add(glow);
    this.hoardLight = glow;
    const fire = new THREE.PointLight(0xff5020, 60, 60, 1); fire.position.set(0, y + 14, LAIR_Z + 4); s.add(fire);
    const fill = new THREE.PointLight(0xffc080, 40, 50, 1); fill.position.set(0, y + 10, LAIR_Z + 16); s.add(fill);
    // bones scattered about
    for (let i = 0; i < 8; i++) {
      const b = spriteMesh(SP.BONES.rows, SP.BONES.color, 'taper', 0.9, { depth: 3 });
      const a = Math.random() * Math.PI * 2, r = 8 + Math.random() * 12;
      b.position.set(Math.cos(a) * r, y, LAIR_Z + Math.sin(a) * r); b.rotation.set(-Math.PI / 2 * Math.random(), Math.random() * 6, 0);
      s.add(b);
    }
  }

  makeEvent(s, e, rand) {
    const y = e.y;
    switch (e.kind) {
      case 'BONES': {
        e.mesh = spriteMesh(SP.BONES.rows, SP.BONES.color, 'taper', 1.1, { depth: 3 });
        e.mesh.position.set(e.x, y, e.z); e.mesh.rotation.y = rand() * 6; s.add(e.mesh);
        const sk = spriteMesh(SP.SKULL.rows, SP.SKULL.color, 'lathe', 0.8);
        sk.position.set(e.x + 1.2, y, e.z + 0.5); s.add(sk);
        const r = rand();
        e.content = r < 0.35 ? { k: 'gold', n: 20 + Math.floor(rand() * 50) } : r < 0.6 ? { k: 'item', item: ['POTION', 'KEY', 'RING', 'CHARM', 'STAFF', 'CROSS'][Math.floor(rand() * 6)] } : r < 0.85 ? { k: 'monster', names: ['GHOUL'] } : { k: 'none' };
        e.col = addCollider(e.x, e.z, 1.4, e);
        break;
      }
      case 'ROCK': e.x = cx(e.z); break;
      case 'TRAP': {
        e.mesh = new THREE.Mesh(new THREE.BoxGeometry(2.4, 0.08, 2.4), new THREE.MeshLambertMaterial({ color: 0x5a5a5a, emissive: 0x000000 }));
        e.mesh.position.set(e.x, y + 0.05, e.z); s.add(e.mesh);
        break;
      }
      case 'DARTS': {
        const side = rand() < 0.5 ? -1 : 1;
        e.side = side;
        e.wx = cx(e.z) + side * (hw(e.z) + 0.2);
        e.mesh = new THREE.Mesh(new THREE.BoxGeometry(0.8, 0.8, 0.8), new THREE.MeshLambertMaterial({ color: 0x940000, emissive: 0x300000 }));
        e.mesh.position.set(e.wx, y + 1.3, e.z); s.add(e.mesh);
        e.t = rand() * 2;
        break;
      }
      case 'PIT': {
        e.r = 2.4;
        e.mesh = new THREE.Mesh(new THREE.CircleGeometry(e.r, 20), new THREE.MeshBasicMaterial({ color: 0x000000 }));
        e.mesh.rotation.x = -Math.PI / 2; e.mesh.position.set(e.x, y + 0.06, e.z); s.add(e.mesh);
        const rim = new THREE.Mesh(new THREE.RingGeometry(e.r, e.r + 0.35, 20), new THREE.MeshLambertMaterial({ color: 0x3a3a3a }));
        rim.rotation.x = -Math.PI / 2; rim.position.set(e.x, y + 0.07, e.z); s.add(rim);
        break;
      }
      case 'MONSTER':
        e.names = [['GHOUL'], ['SNAKE'], ['SPIDER', 'SPIDER', 'SPIDER'], ['SCORPION'], ['SLIME'], ['DEMON'], ['WARRIOR']][Math.floor(rand() * 7)];
        break;
      case 'DOOR': {
        const w = hw(e.z) + 1.5;
        e.mesh = new THREE.Group();
        const gs = SP && ROM.part2.sprites.GATE;
        for (let k = -1; k <= 1; k++) {
          const m = spriteMesh(gs.rows, '#8e8e8e', 'slab', 2.4, { depth: 1 });
          m.position.x = k * w * 0.66; m.scale.x = w * 0.66 / 2.9;
          e.mesh.add(m);
        }
        const chain = new THREE.Mesh(new THREE.TorusGeometry(0.5, 0.14, 6, 10), new THREE.MeshLambertMaterial({ color: 0xc0c0c0 }));
        chain.position.set(0, 1.6, 0.5); e.mesh.add(chain);
        e.mesh.position.set(e.x, y, e.z); s.add(e.mesh);
        if (e.open) e.mesh.position.y = y + 7;
        else e.col = addCollider(e.x, e.z + 0.5, 1.2, e);
        break;
      }
    }
  }

  // ---------- the dragon, built out of voxels ----------
  makeDragon(s) {
    const g = new THREE.Group();
    const GREEN = new THREE.Color(0x328432), DARK = new THREE.Color(0x1a661a), BELLY = new THREE.Color(0xd2d240), RED = new THREE.Color(0xfc5454), WHITE = new THREE.Color(0xececec), BLACK = new THREE.Color(0x111111);
    const blob = (rx, ry, rz, fill) => {
      const gr = new Grid(rx * 2 + 1, ry * 2 + 1, rz * 2 + 1);
      for (let x = 0; x < gr.w; x++) for (let y = 0; y < gr.h; y++) for (let z = 0; z < gr.d; z++) {
        const dx = (x - rx) / rx, dy = (y - ry) / ry, dz = (z - rz) / rz;
        if (dx * dx + dy * dy + dz * dz <= 1.05) gr.set(x, y, z, fill(x - rx, y - ry, z - rz));
      }
      return gr;
    };
    const mk = (gr, sc = 0.45) => { const m = new THREE.Mesh(meshGeometry(gr, sc, sc, sc, 0.08), this.dragonMat); m.castShadow = true; return m; };
    this.dragonMat = new THREE.MeshLambertMaterial({ vertexColors: true, emissive: 0x000000 });
    const body = mk(blob(6, 5, 8, (x, y, z) => (y < -2 ? BELLY : ((x + z) & 3) === 0 ? DARK : GREEN)));
    body.position.y = 2.6;
    g.add(body);
    // spikes on the back
    for (let i = -3; i <= 3; i++) {
      const sp = new THREE.Mesh(new THREE.ConeGeometry(0.45, 1.4, 4), new THREE.MeshLambertMaterial({ color: 0xd2d240 }));
      sp.position.set(0, 7.3 - Math.abs(i) * 0.25, i * 1.1); g.add(sp);
    }
    // neck and head, reaching towards +z
    const neck = new THREE.Group(); neck.position.set(0, 5, 3.2); g.add(neck);
    for (let i = 0; i < 4; i++) {
      const seg = mk(blob(2, 2, 2, (x, y) => (y < -1 ? BELLY : GREEN)), 0.42);
      seg.position.set(0, i * 0.9, i * 1.1); neck.add(seg);
    }
    const head = new THREE.Group(); head.position.set(0, 4.2, 5); neck.add(head);
    const hg = new Grid(9, 6, 10);
    for (let x = 0; x < 9; x++) for (let y = 0; y < 6; y++) for (let z = 0; z < 10; z++) {
      const snout = z > 5;
      if (snout && (x < 2 || x > 6 || y > 3)) continue;
      let c = GREEN;
      if (y === 0) c = BELLY;
      if (snout && y === 1 && (x === 2 || x === 6) && z % 2 === 0) c = WHITE;            // teeth
      if (!snout && y === 4 && (x === 1 || x === 7) && z === 5) c = RED;                 // eyes
      if (snout && y === 3 && (x === 3 || x === 5) && z === 9) c = BLACK;                // nostrils
      hg.set(x, y, z, c);
    }
    hg.set(1, 5, 1, WHITE); hg.set(7, 5, 1, WHITE); hg.set(1, 5, 0, WHITE); hg.set(7, 5, 0, WHITE);
    const headMesh = mk(hg, 0.5); headMesh.position.set(0, -1, -1); head.add(headMesh);
    // eyes glow
    const eyeMat = new THREE.MeshBasicMaterial({ color: 0xff3020 });
    for (const sx of [-1, 1]) { const e = new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.3, 0.3), eyeMat); e.position.set(sx * 1.55, 1.3, 1.6); head.add(e); }
    this.mouth = new THREE.PointLight(0xff6020, 0, 14, 1.5); this.mouth.position.set(0, 0.5, 4.5); head.add(this.mouth);
    // wings
    this.wings = [];
    for (const sx of [-1, 1]) {
      const pivot = new THREE.Group(); pivot.position.set(sx * 2.4, 6, 0);
      const wg = new Grid(14, 1, 10);
      for (let x = 0; x < 14; x++) for (let z = 0; z < 10; z++) {
        const edge = 10 - Math.floor(x * 0.55) - ((x % 4 === 3) ? 2 : 0);
        if (z < edge) wg.set(x, 0, z, (x % 4 === 0 || z === 0) ? DARK : new THREE.Color(0x48a048));
      }
      const wm = new THREE.Mesh(meshGeometry(wg, 0.55, 0.3, 0.55), this.dragonMat);
      wm.position.set(sx > 0 ? 3.8 : -3.8, 0, -1.5); if (sx < 0) wm.scale.x = -1;
      pivot.add(wm); g.add(pivot); this.wings.push({ pivot, sx });
    }
    // legs and tail
    for (const [lx, lz] of [[-2.6, 2.2], [2.6, 2.2], [-2.6, -2.6], [2.6, -2.6]]) {
      const leg = mk(blob(1, 2, 1, () => DARK), 0.6); leg.position.set(lx, 0, lz); g.add(leg);
    }
    this.tail = [];
    let prev = g;
    for (let i = 0; i < 6; i++) {
      const seg = new THREE.Group(); seg.position.set(0, i === 0 ? 2.4 : 0, i === 0 ? -3.6 : -1.2);
      const m = mk(blob(Math.max(1, 2 - (i >> 1)), Math.max(1, 2 - (i >> 1)), 1, () => GREEN), 0.45);
      seg.add(m); prev.add(seg); prev = seg; this.tail.push(seg);
    }
    g.position.set(0, floorY(-LEN), LAIR_Z - 6);
    s.add(g);
    this.dragon = g; this.neck = neck; this.head = head;
    this.dragonState = { mode: 'sleep', t: 0, stun: 0, blind: 0, hitFlash: 0 };
  }

  start(from) {
    G.hero.position.set(cx(-3), 0, -3); G.hero.rotation.y = Math.PI;
    if (from === 'resume' && this.state.pos) G.hero.position.set(this.state.pos[0], floorY(this.state.pos[1]), this.state.pos[1]);
    G.camOffset.set(0, 8.5, 9.5); G.camLook.set(0, 1, -3);
    for (let i = 0; i < G.p.warriors; i++) this.followers.add(G.scene);
    G.hero.userData.arm.add(this.torch);
    this.torch.position.set(0.2, 0.1, 0.6);
    this.light.position.set(G.hero.position.x, G.hero.position.y + 3.5, G.hero.position.z + 3);
    ambience('cave');
    if (this.state.vision) this.reveal();
  }
  leaving() { G.hero.userData.arm.remove(this.torch); ambience(null); }

  clamp = (x, z) => {
    z = Math.min(4, z);
    if (z > -LEN - 2) {
      const c = cx(z), w = hw(z) - 0.6;
      x = Math.max(c - w, Math.min(c + w, x));
    } else {
      const dx = x, dz = z - LAIR_Z, d = Math.hypot(dx, dz);
      if (d > LAIR_R - 1 && !(Math.abs(x) < hw(-LEN) && z > LAIR_Z + LAIR_R - 3)) { x *= (LAIR_R - 1) / d; z = LAIR_Z + dz * (LAIR_R - 1) / d; }
    }
    for (const e of this.events) if (e.kind === 'DOOR' && !e.open && z < e.z + 0.9 && G.hero.position.z >= e.z + 0.9) z = e.z + 0.9;
    return [x, z];
  };
  ground = (x, z) => floorY(z);

  update(dt) {
    const p = G.p, h = G.hero;
    const boss = h.position.z < -LEN + 4;
    if (boss !== this.boss) {
      this.boss = boss;
      if (boss) { G.camOffset.set(0, 16, 19); G.camLook.set(0, 4.5, -8); G.hemi.intensity = 0.8; G.scene.fog.near = 25; G.scene.fog.far = 90; }
      else { G.camOffset.set(0, 8.5, 9.5); G.camLook.set(0, 1, -3); G.hemi.intensity = 0.4; }
    }
    const b = moveHero(dt, { clamp: this.clamp, ground: this.ground, speed: this.boss ? 8 : 6.5 });
    this.followers.update(dt, this.ground, this.boss ? (f, i) => {
      if (this.dragonState.mode === 'dead') return null;
      const a = G.time * 0.5 + i * 2.1;
      return new THREE.Vector3(this.dragon.position.x + Math.sin(a) * 9, 0, this.dragon.position.z + Math.cos(a) * 9);
    } : null);
    // torch
    this.light.position.set(h.position.x, h.position.y + 3.5, h.position.z + 3);
    this.light.intensity = (p.goods.LANTERN ? 60 : 30) * (0.88 + Math.random() * 0.12);
    this.torch.rotation.y += dt * 5;
    this.state.pos = [h.position.x, h.position.z];

    // fever drains strength (POISON DART)
    if (p.fever) {
      this.feverT += dt;
      if (this.feverT > 6) { this.feverT = 0; p.str--; floatText(h.position.clone().setY(h.position.y + 3.5), 'FEVER -1', '#84fcd4'); G.ui.status(p, 'FEVER'); if (p.str < 0) { G.dead = true; return { say: ['YOU DIED', 'OF FEVER'] }; } }
    }
    this.updateShots(dt);
    let trigger = null;
    for (const e of this.events) {
      const r = this.updateEvent(e, dt);
      if (r && !trigger) trigger = r;
    }
    if (this.boss) { const r = this.updateDragon(dt); if (r) return r; }
    else G.ui.idle(['DESCENDING', 'INTO THE CAVE', '', 'FIRE = MENU']);
    if (trigger) return trigger;
    if (b && b.data) return { bump: b.data };
    return null;
  }

  updateEvent(e, dt) {
    const h = G.hero.position;
    const d = Math.hypot(h.x - e.x, h.z - e.z);
    switch (e.kind) {
      case 'ROCK':
        if (!e.done && !e.falling && Math.abs(h.z - e.z) < 2.5) {
          e.falling = 0.9; e.fx = h.x + G.hero.rotation.y * 0; e.fz = h.z - 1.5;
          const sh = new THREE.Mesh(new THREE.CircleGeometry(1.4, 16), new THREE.MeshBasicMaterial({ color: 0x000000, transparent: true, opacity: 0.6 }));
          sh.rotation.x = -Math.PI / 2; sh.position.set(e.fx, floorY(e.fz) + 0.1, e.fz); sh.scale.setScalar(0.1);
          G.scene.add(sh); e.shadow = sh;
          e.rock = spriteMesh(SP.ROCK.rows, '#86861d', 'lathe', 1.3);
          e.rock.position.set(e.fx, floorY(e.fz) + 18, e.fz); G.scene.add(e.rock);
          sfx('trap');
        }
        if (e.falling > 0) {
          e.falling -= dt;
          const k = 1 - Math.max(0, e.falling) / 0.9;
          e.shadow.scale.setScalar(0.1 + k);
          e.rock.position.y = floorY(e.fz) + 18 * (1 - k * k);
          if (e.falling <= 0) {
            e.done = true; e.falling = -1; this.state.done.push(e.id);
            sfx('boom'); shake(0.6);
            G.fx.burst(e.rock.position.clone().setY(floorY(e.fz) + 0.5), ['#86861d', '#6f6f6f'], 30, { speed: 6, up: 5 });
            G.scene.remove(e.shadow);
            addCollider(e.fx, e.fz, 1.4, { kind: 'ROCKPILE' });
            if (Math.hypot(h.x - e.fx, h.z - e.fz) < 1.9) {
              const dmg = R.randBelow(4) + 2; G.p.str -= dmg; hurtFx(dmg);
              return { say: ['ROCK', 'YOU WERE HIT', 'YOU TAKE ' + R.pad(dmg), 'UNITS DAMAGE'], check: true };
            }
          }
        }
        return null;
      case 'TRAP':
        e.mesh.material.emissive.setHex(this.state.vision && !e.done ? 0x601010 : 0);
        if (!e.done && d < 1.5) { e.done = true; this.state.done.push(e.id); return { trap: true }; }
        return null;
      case 'DARTS': {
        if (e.done) return null;
        e.t -= dt;
        if (e.t <= 0 && Math.abs(h.z - e.z) < 14) {
          e.t = 2.2;
          const dart = new THREE.Mesh(new THREE.BoxGeometry(0.7, 0.12, 0.12), new THREE.MeshBasicMaterial({ color: 0x84fcd4 }));
          dart.position.set(e.wx, e.y + 1.3, e.z);
          G.scene.add(dart);
          this.shots.push({ mesh: dart, vx: -e.side * 15, vz: 0, life: 1.6, r: 0.9, dart: e, dmg: 1 });
          sfx('arrow');
        }
        return null;
      }
      case 'PIT':
        if (this.state.vision) e.mesh.material.color.setHex(Math.sin(G.time * 6) > 0 ? 0x300000 : 0);
        if (d < e.r - 0.3) return { pit: e };
        return null;
      case 'MONSTER':
        if (!e.done && d < 6) { e.done = true; this.state.done.push(e.id); return { monster: e }; }
        return null;
      case 'DOOR':
        if (e.opening) { e.opening -= dt; e.mesh.position.y = e.y + 7 * (1 - Math.max(0, e.opening) / 1.2); }
        return null;
    }
    return null;
  }

  updateShots(dt) {
    const h = G.hero.position;
    for (let i = this.shots.length - 1; i >= 0; i--) {
      const s = this.shots[i];
      s.life -= dt;
      s.mesh.position.x += s.vx * dt; s.mesh.position.z += s.vz * dt; if (s.vy) s.mesh.position.y += s.vy * dt;
      if (s.spin) s.mesh.rotation.x += dt * 10;
      let hit = false;
      if (s.arrow) {
        const dd = this.dragon.position.clone(); dd.y = s.mesh.position.y;
        if (s.mesh.position.distanceTo(dd) < 5.5 && this.dragonState.mode !== 'dead') { hit = true; this.hurtDragon(s.dmg); }
      } else if (Math.hypot(s.mesh.position.x - h.x, s.mesh.position.z - h.z) < s.r && Math.abs(s.mesh.position.y - (h.y + 1.3)) < 2) {
        hit = true;
        this.pendingHit = s;
      } else if (s.fire) {
        for (const f of this.followers.alive) if (Math.hypot(s.mesh.position.x - f.g.position.x, s.mesh.position.z - f.g.position.z) < 1.4) {
          hit = true; f.hp -= R.randBelow(6) + 3; G.fx.burst(f.g.position.clone().setY(f.g.position.y + 1.5), ['#fc9090', '#fce070'], 15);
          if (f.hp <= 0) { f.die(); floatText(f.g.position.clone().setY(f.g.position.y + 3), 'A WARRIOR WAS KILLED', '#fc9090', 0.8); }
          break;
        }
      }
      if (hit || s.life <= 0) {
        if (s.fire) G.fx.burst(s.mesh.position, ['#fc9090', '#fce070', '#fc5454'], 12, { speed: 4, up: 3 });
        G.scene.remove(s.mesh); s.mesh.geometry.dispose(); this.shots.splice(i, 1);
      }
    }
  }

  // ---------- the dragon fight (real time) ----------
  updateDragon(dt) {
    const d = this.dragonState, g = this.dragon, h = G.hero.position;
    const p = G.p;
    d.t += dt;
    const anim = G.time;
    for (const w of this.wings) w.pivot.rotation.z = w.sx * (0.25 + Math.sin(anim * (d.mode === 'dead' ? 0 : 3)) * 0.35);
    this.tail.forEach((t, i) => t.rotation.y = Math.sin(anim * 2 + i * 0.6) * 0.25);
    if (d.hitFlash > 0) { d.hitFlash -= dt; this.dragonMat.emissive.setHex(d.hitFlash > 0 ? 0x664444 : 0); }
    document.getElementById('bossbar').style.display = d.mode === 'dead' ? 'none' : 'block';
    document.getElementById('bossfill').style.width = Math.max(0, this.dragonHp / DRAGON_HP * 100) + '%';
    if (d.mode === 'dead') {
      g.rotation.z = Math.min(1.4, g.rotation.z + dt * 0.8);
      g.position.y = Math.max(floorY(-LEN) - 2, g.position.y - dt * 0.8);
      return null;
    }
    if (d.mode === 'sleep') {
      if (h.z < LAIR_Z + LAIR_R - 2) { d.mode = 'wake'; d.t = 0; ambience('boss'); sfx('roar'); shake(1); return { say: ['THE DRAGON', 'AWAKES!', 'FIRE = ATTACK', 'BACK = MENU'] }; }
      this.neck.rotation.x = 0.5; return null;
    }
    // face the hero
    const dx = h.x - g.position.x, dz = h.z - g.position.z, dist = Math.hypot(dx, dz);
    const want = Math.atan2(dx, dz);
    let da = want - g.rotation.y; da = Math.atan2(Math.sin(da), Math.cos(da));
    if (d.stun > 0) { d.stun -= dt; this.neck.rotation.x = 0.6; G.ui.idle(['THE DRAGON', 'IS STUNNED']); return this.takeHit(); }
    if (d.blind > 0) d.blind -= dt;
    g.rotation.y += da * Math.min(1, dt * 2);
    this.neck.rotation.x = Math.sin(anim * 1.5) * 0.15 - (d.mode === 'breath' ? 0.35 : 0);
    G.ui.idle(['THE DRAGON', 'FIRE = ATTACK', 'BACK = MENU', p.goods.LONGBOW ? 'BOW FIRES FAR' : '']);

    if (d.mode === 'wake' && d.t > 1.5) { d.mode = 'idle'; d.t = 0; }
    if (d.mode === 'idle' && d.t > 1.6 - (this.dragonHp < DRAGON_HP / 2 ? 0.5 : 0)) {
      d.t = 0;
      if (dist < 8.5) d.mode = 'claw';
      else d.mode = R.randBelow(2) ? 'breath' : 'stalk';
    }
    if (d.mode === 'stalk') {
      if (dist > 7) { g.position.x += dx / dist * 3 * dt; g.position.z += dz / dist * 3 * dt; }
      const r = Math.hypot(g.position.x, g.position.z - LAIR_Z);
      if (r > LAIR_R - 6) { g.position.x *= (LAIR_R - 6) / r; g.position.z = LAIR_Z + (g.position.z - LAIR_Z) * (LAIR_R - 6) / r; }
      if (d.t > 1.8) { d.mode = 'idle'; d.t = 0; }
    }
    if (d.mode === 'breath') {
      this.mouth.intensity = Math.min(40, d.t * 50);
      if (d.t > 0.9 && !d.fired) {
        d.fired = true; sfx('fire'); sfx('roar');
        const head = new THREE.Vector3(); this.head.getWorldPosition(head);
        // aim at the hero, or anywhere if blinded by FLASH
        let tx = h.x, tz = h.z;
        if (d.blind > 0) { tx += (Math.random() - 0.5) * 20; tz += (Math.random() - 0.5) * 20; }
        let tgt = new THREE.Vector3(tx, h.y + 1.2, tz);
        const fl = this.followers.alive;
        if (fl.length && R.randBelow(2) === 0) { const f = fl[R.randBelow(fl.length - 1)]; tgt = f.g.position.clone().setY(f.g.position.y + 1.2); }
        for (let i = -2; i <= 2; i++) {
          const dir = tgt.clone().sub(head); dir.y = 0; dir.normalize();
          dir.applyAxisAngle(new THREE.Vector3(0, 1, 0), i * 0.14);
          const ball = new THREE.Mesh(new THREE.BoxGeometry(0.9, 0.9, 0.9), new THREE.MeshBasicMaterial({ color: i % 2 ? 0xfce070 : 0xfc5454 }));
          ball.position.copy(head);
          const fall = (tgt.y - head.y) / 1.2;
          G.scene.add(ball);
          this.shots.push({ mesh: ball, vx: dir.x * 15, vz: dir.z * 15, vy: fall, life: 2.2, r: 1.3, fire: true, spin: true, dmg: R.randBelow(5) + 3 });
        }
      }
      if (d.t > 1.4) { d.mode = 'idle'; d.t = 0; d.fired = false; this.mouth.intensity = 0; }
    }
    if (d.mode === 'claw') {
      const k = Math.sin(Math.min(1, d.t / 0.7) * Math.PI);
      this.neck.rotation.x = -0.6 * k;
      this.neck.position.z = 3.2 + k * 3;
      if (d.t > 0.45 && !d.clawed) {
        d.clawed = true; sfx('swing');
        if (dist < 10 && d.blind <= 0) {
          if (R.contest(p.dex, DRAGON_ATK)) {
            let dmg = R.randBelow(DRAGON_DMG);
            if (p.protect > 0) { p.protect--; floatText(h.clone().setY(h.y + 3), 'NO EFFECT', '#fff'); dmg = 0; }
            if (p.inv.SHIELD > 0 && dmg) dmg = Math.max(1, dmg - 1);
            if (p.goods.CHAIN > 0 && dmg) dmg = Math.max(1, dmg - 1);
            if (dmg) { p.str -= dmg; hurtFx(dmg); G.ui.status(p); }
            else if (p.protect === 0) floatText(h.clone().setY(h.y + 3), 'MISSED', '#aaa');
          } else floatText(h.clone().setY(h.y + 3), 'MISSED', '#aaa');
        }
      }
      if (d.t > 0.9) { d.mode = 'idle'; d.t = 0; d.clawed = false; this.neck.position.z = 3.2; }
    }
    // the warriors chip away
    for (const f of this.followers.alive) {
      if (f.g.position.distanceTo(g.position) < 11 && Math.random() < dt * 0.6) { f.lunge(); if (R.contest(DRAGON_ATK, 14)) this.hurtDragon(R.randBelow(3) + 1, f.g.position); }
    }
    if (p.str < 0) { G.dead = true; return { say: ['YOU ARE DEAD'] }; }
    if (this.dragonHp < 0) return this.dragonDies();
    return this.takeHit();
  }

  takeHit() {
    const s = this.pendingHit;
    if (!s) return null;
    this.pendingHit = null;
    const p = G.p;
    if (s.dart) {
      if (p.protect > 0) { p.protect--; floatText(G.hero.position.clone().setY(G.hero.position.y + 3), 'NO EFFECT', '#fff'); return null; }
      s.dart.done = true;
      p.fever = 1; p.str -= 1; hurtFx(1); G.ui.status(p, 'FEVER');
      return { say: ['POISON DART', 'YOU ARE SICK', 'WITH FEVER'], check: true };
    }
    let dmg = s.dmg;
    if (p.protect > 0) { p.protect--; floatText(G.hero.position.clone().setY(G.hero.position.y + 3), 'NO EFFECT', '#fff'); return null; }
    if (p.goods.RUBY > 0) dmg = Math.max(1, dmg >> 1);
    p.str -= dmg; hurtFx(dmg); G.ui.status(p);
    if (p.str < 0) { G.dead = true; return { say: ['YOU WERE BURNED', 'YOU ARE DEAD'] }; }
    return null;
  }

  hurtDragon(dmg, from) {
    if (this.dragonState.mode === 'dead' || dmg <= 0) return;
    this.dragonHp -= dmg;
    this.dragonState.hitFlash = 0.15;
    sfx('hit');
    const at = this.dragon.position.clone(); at.y += 6;
    floatText(at, '-' + dmg, '#fce070', 1.6);
    G.fx.burst(at.clone().setY(at.y - 2), ['#328432', '#d2d240', '#fff'], 8 + dmg * 3, { speed: 5, up: 5 });
  }

  dragonDies() {
    this.dragonState.mode = 'dead';
    sfx('roar'); sfx('boom'); shake(1.4);
    G.fx.burst(this.dragon.position.clone().setY(this.dragon.position.y + 4), ['#328432', '#d2d240', '#fce070', '#fff'], 160, { speed: 10, up: 10, size: 0.3, life: 2 });
    document.getElementById('bossbar').style.display = 'none';
    ambience(null);
    return { win: true };
  }

  // FIRE in the lair attacks
  async attack() {
    if (this.attackCool > G.time || this.dragonState.mode === 'dead') return;
    const p = G.p, h = G.hero.position, g = this.dragon.position;
    const dist = Math.hypot(h.x - g.x, h.z - g.z);
    G.hero.rotation.y = Math.atan2(g.x - h.x, g.z - h.z);
    if (dist < 8.5) {
      this.attackCool = G.time + 0.5;
      swing();
      p.bonus = p.inv.HANDAXE > 0 ? 2 : 0;
      if (R.contest(DRAGON_ATK, p.dex)) {
        const dmg = R.randBelow(p.weapon) + p.bonus;
        if (dmg) this.hurtDragon(dmg); else floatText(g.clone().setY(g.y + 6), 'MISS', '#aaa');
      } else floatText(g.clone().setY(g.y + 6), 'MISS', '#aaa');
    } else if (p.goods.LONGBOW) {
      this.attackCool = G.time + 0.7;
      sfx('arrow');
      const arrow = new THREE.Mesh(new THREE.BoxGeometry(0.12, 0.12, 1.4), new THREE.MeshBasicMaterial({ color: 0xececec }));
      arrow.position.set(h.x, h.y + 1.6, h.z);
      const dx = g.x - h.x, dz = g.z - h.z, l = Math.hypot(dx, dz);
      arrow.rotation.y = Math.atan2(dx, dz);
      G.scene.add(arrow);
      this.shots.push({ mesh: arrow, vx: dx / l * 30, vz: dz / l * 30, life: 1.5, arrow: true, dmg: R.contest(DRAGON_ATK, p.dex) ? R.randBelow(3) + 1 : 0 });
    } else {
      this.attackCool = G.time + 0.6;
      floatText(h.clone().setY(h.y + 3.4), 'TOO FAR AWAY', '#fff', 0.8);
    }
  }

  // ---------- events that need the message window ----------
  async interact(e) {
    if (e.trap) {
      sfx('trap');
      G.p.stuck = R.randBelow(3) + 2;
      await G.ui.say(['>>TRAP<<', 'YOU ARE CAUGHT']);
      while (G.p.stuck > 0) { await G.ui.say(['>>TRAP<<', 'STILL STUCK', '', 'PRESS FIRE']); G.p.stuck--; }
      await G.ui.say(['YOU ESCAPE']);
      return;
    }
    if (e.pit) {
      const pit = e.pit;
      sfx('trap');
      const h = G.hero.position;
      h.y -= 2;
      if (G.p.goods.ROPE > 0) await G.ui.say(['YOU FELL', 'INTO THE PIT', 'USING ROPE', 'YOU CLIMB OUT']);
      else {
        const d = R.randBelow(5) + 2; G.p.str -= d; hurtFx(d);
        await G.ui.say(['YOU FELL', 'INTO THE PIT', 'YOU TAKE ' + R.pad(d), 'UNITS DAMAGE']);
      }
      // climb out on the near side
      h.z = pit.z + pit.r + 1.2; h.x = pit.x; h.y = floorY(h.z);
      return this.checkDead();
    }
    if (e.monster) {
      const r = await battle(e.monster.names, new THREE.Vector3(e.monster.x, 0, e.monster.z), { allies: this.followers.alive, y: G.hero.position.y });
      return;
    }
    if (e.say) {
      await G.ui.say(e.say);
      if (e.check) return this.checkDead();
      return;
    }
    if (e.kind === 'ROCKPILE') { await G.ui.say(['ROCK']); return; }
    if (e.kind === 'BONES') return this.bones(e);
    if (e.kind === 'DOOR') return this.door(e);
  }

  async checkDead() { if (G.p.str < 0) { G.dead = true; await G.ui.say(['YOU ARE DEAD']); } }

  async bones(e) {
    if (e.done) { await G.ui.say(['BONES', 'NOBODY HERE']); return; }
    e.done = true; this.state.done.push(e.id);
    const c = e.content, pos = new THREE.Vector3(e.x, e.y, e.z);
    if (c.k === 'gold') { G.p.gold += c.n; sfx('coin'); await G.ui.say(['BONES', 'YOU FIND', c.n + ' GOLD']); }
    else if (c.k === 'item') { G.p.inv[c.item]++; sfx('item'); showPickup(c.item, pos); await G.ui.say(['BONES', 'YOU FIND', article(c.item)]); }
    else if (c.k === 'monster') await battle(c.names, pos, { objR: 1.4, allies: this.followers.alive, intro: ['BONES', 'THE BONES RISE', 'A GHOUL'], y: G.hero.position.y });
    else await G.ui.say(['BONES', 'NOBODY HERE']);
    G.ui.status(G.p);
  }

  async door(e) {
    if (e.open) return;
    const h = G.hero.position; h.z = e.z + 2;
    await G.ui.say(['A CHAINED DOOR', 'BLOCKS THE WAY']);
    for (;;) {
      const p = G.p;
      const c = await G.ui.dir4({ up: p.spells.UNLOCK ? 'UNLOCK' : (p.inv.KEY ? 'USE KEY' : null), right: 'BASH', left: 'USE', down: 'LEAVE' }, 'DOOR');
      if (!c || c === 'down') return;
      if (c === 'left') { await useMenu('cave'); if (e.open) return; continue; }
      if (c === 'up') {
        if (p.spells.UNLOCK) { p.spells.UNLOCK--; sfx('magic'); } else { p.inv.KEY--; }
        return this.openDoor(e);
      }
      if (c === 'right') {
        swing(); sfx('hit'); shake(0.3);
        const men = this.followers.alive.length;
        if (R.randBelow(9) < 3 + men * 2) return this.openDoor(e);
        p.str -= 1; hurtFx(1); G.ui.status(p);
        await G.ui.say(['THE DOOR HOLDS', 'YOU TAKE 01', 'UNITS DAMAGE']);
        if (p.str < 0) return this.checkDead();
      }
    }
  }

  async openDoor(e) {
    e.open = true; e.opening = 1.2; this.state.doors.push(e.id);
    if (e.col) removeCollider(e.col);
    sfx('door');
    await G.ui.say(['THE DOOR', 'OPENS']);
    save({ area: this.state });
  }

  reveal() {
    this.state.vision = true;
    for (const e of this.events) if (e.kind === 'TRAP' || e.kind === 'PIT') {
      const m = new THREE.Mesh(new THREE.RingGeometry(1.6, 1.9, 4), new THREE.MeshBasicMaterial({ color: 0xfc5454 }));
      m.rotation.x = -Math.PI / 2; m.position.set(e.x, e.y + 0.12, e.z); G.scene.add(m);
    }
  }

  // spells the cave handles itself
  onSpell = async (name, foe) => {
    const p = G.p;
    if (name === 'VISION') { p.spells.VISION--; sfx('magic'); this.reveal(); await G.ui.say(['YOU SEE', 'THE TRAPS']); return { turn: true }; }
    if (name === 'UNLOCK') {
      const door = this.events.find(e => e.kind === 'DOOR' && !e.open && Math.abs(G.hero.position.z - e.z) < 5);
      if (!door) return null;
      p.spells.UNLOCK--; sfx('magic'); await this.openDoor(door); return { turn: true };
    }
    if (this.boss && !foe && this.dragonState.mode !== 'dead' && this.dragonState.mode !== 'sleep') {
      const d = this.dragonState;
      if (['BLAST', 'FLASH', 'STUN'].includes(name)) {
        p.spells[name]--; sfx('magic');
        if (!p.goods.SAPPHIRE && R.randBelow(2) === 0) { await G.ui.say(['DRAGON MAGIC', 'CANCELS IT']); return { turn: true }; }
        if (name === 'BLAST') { const dmg = R.randBelow(12) + 6; sfx('boom'); shake(0.6); this.hurtDragon(dmg); await G.ui.say(['BLAST', 'FOR ' + R.pad(dmg) + ' UNITS']); }
        if (name === 'FLASH') { d.blind = 8; G.fx.burst(this.dragon.position.clone().setY(8), '#fff', 60, { speed: 12, gravity: 0, size: 0.12 }); await G.ui.say(['THE DRAGON', 'IS BLINDED']); }
        if (name === 'STUN') { d.stun = 6; await G.ui.say(['THE DRAGON', 'IS STUNNED']); }
        if (this.dragonHp < 0) this.winPending = true;
        return { turn: true };
      }
    }
    return null;
  };

  onFight = async () => {
    const near = this.events.find(e => (e.kind === 'BONES' || e.kind === 'DOOR') && Math.hypot(e.x - G.hero.position.x, e.z - G.hero.position.z) < 3.5);
    if (near) return this.interact(near);
    await G.ui.say(['OH NO', 'NOBODY HERE']);
  };
}
