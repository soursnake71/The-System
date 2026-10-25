// Part 3: into the dragon's cave. A winding, descending tunnel of jagged rock (ochre, as on the
// 2600), with falling rocks, traps, poison darts, pits, bones and chained doors - and the dragon.
import * as THREE from 'three';
import { gfx, preset } from '../core/gfx.js';
import { triplanar } from '../world/triplanar.js';
import { prop } from '../core/assets.js';
import { fbm } from '../core/texgen.js';
import { mats, mesh, rockGeo } from '../actors/parts.js';
import { Dragon } from '../actors/dragon.js';
import { mulberry } from '../util.js';
import { sfx, ambience } from '../core/sound.js';
import { FireStream, floatText, shake, hitstop } from '../game/fx.js';
import * as R from '../rules.js';

const LEN = 340, LAIR_R = 30;
const LAIR_Z = -LEN - LAIR_R + 4;
const cx = (z) => { const k = THREE.MathUtils.clamp((-z - 10) / 30, 0, 1) * THREE.MathUtils.clamp((LEN + z) / 25, 0, 1); return k * (12 * Math.sin(z * 0.017) + 6 * Math.sin(z * 0.043 + 1)); };
const hw = (z) => 5.5 + 2.2 * Math.sin(z * 0.031 + 2) + 1.2 * Math.sin(z * 0.09);
const hh = (z) => 6.5 + 2 * Math.sin(z * 0.023) + Math.max(0, (-z - LEN + 30) / 30) * 8;
const floorY = (z) => Math.max(-LEN, Math.min(0, z)) * 0.07;
const DRAGON = { hp: 60, atk: 25, dmg: 9 };

export class Cave {
  constructor(game, state) {
    this.game = game;
    this.state = { done: [], doors: [], vision: false, dragonDead: false, ...state };
    this.colliders = []; this.interactables = []; this.events = []; this.darts = []; this.rocks = [];
    this.ambience = 'cave'; this.floor = 'cave';
  }

  async build(scene) {
    const g = this.game, p = g.stats;
    const lit = p.goods.LANTERN > 0;
    scene.background = new THREE.Color(0x020202);
    scene.fog = new THREE.FogExp2(0x050403, lit ? 0.028 : 0.05);
    // a dim, warm environment so metal (armour, gold) has something to reflect
    const envScene = new THREE.Scene();
    envScene.add(new THREE.Mesh(new THREE.SphereGeometry(50, 32, 16), new THREE.MeshBasicMaterial({ color: 0x1c140c, side: THREE.BackSide })));
    for (let i = 0; i < 7; i++) {
      const fl = new THREE.Mesh(new THREE.SphereGeometry(3, 12, 8), new THREE.MeshBasicMaterial({ color: new THREE.Color().setRGB(6, 3, 1.1) }));
      fl.position.set(Math.cos(i * 0.9) * 30, -6 + (i % 3) * 8, Math.sin(i * 0.9) * 30); envScene.add(fl);
    }
    scene.environment = gfx.pmrem.fromScene(envScene, 0.04).texture;
    scene.environmentIntensity = 0.15;
    this.hemi = new THREE.HemisphereLight(0x8a8a90, 0x201810, lit ? 0.12 : 0.06); scene.add(this.hemi);
    const rand = mulberry(p.seed ^ 0x5eed);
    const wallMat = triplanar('cliff_side', { scale: 6, color: 0xb8aa98, normalScale: 1.4 });
    const floorMat = triplanar('rocky_terrain_02', { scale: 3.5, color: 0x8a8070 });
    this.buildTunnel(scene, wallMat, floorMat);
    this.buildLair(scene, wallMat, floorMat, rand);
    // stalactites and stalagmites
    const cone = new THREE.ConeGeometry(0.5, 3, 9, 4);
    const cp = cone.attributes.position;
    for (let i = 0; i < cp.count; i++) cp.setX(i, cp.getX(i) * (1 + Math.sin(cp.getY(i) * 5) * 0.15));
    cone.computeVertexNormals();
    const spikes = new THREE.InstancedMesh(cone, wallMat, 220);
    const mx = new THREE.Matrix4(), q = new THREE.Quaternion(), sc = new THREE.Vector3(), ps = new THREE.Vector3();
    for (let i = 0; i < 220; i++) {
      const z = -rand() * LEN, up = rand() < 0.6, x = cx(z) + (rand() - 0.5) * hw(z) * (up ? 1.6 : 1.7);
      const s = 0.4 + rand() * 1.1;
      if (up) { ps.set(x, floorY(z) + this.ceilAt(x, z) - 1.2 * s, z); q.setFromAxisAngle(new THREE.Vector3(1, 0, 0), Math.PI); }
      else { if (Math.abs(x - cx(z)) < hw(z) * 0.65) continue; ps.set(x, floorY(z) + 1.2 * s, z); q.identity(); this.colliders.push({ type: 'circle', x, z, r: 0.5 * s }); }
      sc.set(s, s * (0.8 + rand()), s);
      spikes.setMatrixAt(i, mx.compose(ps, q, sc));
    }
    spikes.castShadow = spikes.receiveShadow = true;
    scene.add(spikes);
    // glowing crystals in the walls
    const crystalM = new THREE.MeshPhysicalMaterial({ color: 0x103040, emissive: 0x2a8aa8, emissiveIntensity: 1.3, roughness: 0.1, transmission: 0.4, thickness: 0.5 });
    const cg = new THREE.OctahedronGeometry(0.25, 0); cg.scale(0.6, 1.8, 0.6);
    const crystals = new THREE.InstancedMesh(cg, crystalM, 90);
    for (let i = 0; i < 90; i++) {
      const z = -rand() * (LEN - 8), side = rand() < 0.5 ? -1 : 1;
      ps.set(cx(z) + side * hw(z) * 0.98, floorY(z) + 0.2 + rand() * 1.5, z);
      q.setFromEuler(new THREE.Euler((rand() - 0.5) * 0.8, rand() * 3, side * (0.4 + rand() * 0.5))); sc.setScalar(0.6 + rand() * 1.2);
      crystals.setMatrixAt(i, mx.compose(ps, q, sc));
    }
    scene.add(crystals);
    // your light: a lantern if you bought one, else a torch
    this.light = new THREE.PointLight(lit ? 0xffd8a0 : 0xff9a50, lit ? 26 : 14, lit ? 34 : 24, 1.2);
    this.light.castShadow = preset().shadow >= 4096;
    this.light.shadow.mapSize.set(512, 512); this.light.shadow.bias = -0.002;
    scene.add(this.light);
    // a faint fill that travels with the camera, so you can see yourself against the torch
    this.fill = new THREE.PointLight(0xffc89a, 6, 10, 1.6);
    scene.add(this.fill);
    // events along the tunnel
    const kinds = ['BONES', 'ROCK', 'MONSTER', 'TRAP', 'DARTS', 'BONES', 'PIT', 'MONSTER', 'ROCK', 'TRAP', 'MONSTER'];
    let id = 0;
    const pend = [];
    for (let z = -28, i = 0; z > -LEN + 20; z -= 15 + rand() * 9, i++) {
      if (Math.abs(z + 120) < 10 || Math.abs(z + 245) < 10) continue;
      const kind = kinds[(i + Math.floor(rand() * 3)) % kinds.length];
      const x = cx(z) + (rand() - 0.5) * hw(z) * 0.9;
      const e = { id: id++, kind, x, z, y: floorY(z) };
      e.done = this.state.done.includes(e.id);
      pend.push(this.makeEvent(scene, e, rand));
      this.events.push(e);
    }
    for (const dz of [-120, -245]) {
      const e = { id: 1000 + dz, kind: 'DOOR', x: cx(dz), z: dz, y: floorY(dz) };
      e.open = this.state.doors.includes(e.id);
      pend.push(this.makeEvent(scene, e, rand));
      this.events.push(e);
    }
    // the dragon
    this.dragon = new Dragon();
    this.dragon.root.position.set(0, floorY(-LEN) - 0.2, LAIR_Z - 8);
    this.dragon.root.scale.setScalar(1.55);
    scene.add(this.dragon.root);
    this.boss = new DragonBoss(this, this.dragon);
    if (this.state.dragonDead) { this.boss.dead = true; this.dragon.state.dead = 1; }
    this.fire = new FireStream(scene);
    await Promise.all(pend);
  }

  ceilAt(x, z) { return hh(z) * Math.sqrt(Math.max(0, 1 - Math.pow((x - cx(z)) / (hw(z) + 1.5), 2))); }

  buildTunnel(scene, wallMat, floorMat) {
    // the arch: rings of vertices from the left floor edge over the top to the right
    const seg = 28, step = 1.2;
    const pos = [], idx = [], fpos = [], fidx = [];
    let rows = 0;
    for (let z = 8; z > -LEN - 8; z -= step, rows++) {
      const c = cx(z), w = hw(z), h = hh(z), y0 = floorY(z);
      for (let i = 0; i <= seg; i++) {
        const a = Math.PI * (i / seg);
        let x = c - Math.cos(a) * (w + 1.5), y = y0 + Math.sin(a) * h - 0.6;
        // jagged rock: push the wall in and out along its normal
        const nx = -Math.cos(a), ny = Math.sin(a);
        const n = fbm(x * 0.25, y * 0.25, z * 0.25, 4) * 2.2 + fbm(x * 0.9, y * 0.9, z * 0.9, 2) * 0.45;
        x += nx * n; y += ny * n * 0.6;
        pos.push(x, y, z);
      }
      for (let i = 0; i <= 8; i++) {
        const t = i / 8, x = c - (w + 1.6) + t * (w + 1.6) * 2;
        fpos.push(x, y0 + fbm(x * 0.4, z * 0.4, 1, 3) * 0.25 + Math.pow(Math.abs(t - 0.5) * 2, 6) * 1.2, z);
      }
    }
    for (let r = 0; r < rows - 1; r++) {
      for (let i = 0; i < seg; i++) { const a = r * (seg + 1) + i, b = a + seg + 1; idx.push(a, b, a + 1, a + 1, b, b + 1); }
      for (let i = 0; i < 8; i++) { const a = r * 9 + i, b = a + 9; fidx.push(a, a + 1, b, a + 1, b + 1, b); }
    }
    const mk = (p, ix, m) => {
      const geo = new THREE.BufferGeometry();
      geo.setAttribute('position', new THREE.Float32BufferAttribute(p, 3));
      geo.setIndex(ix); geo.computeVertexNormals();
      const me = new THREE.Mesh(geo, m); me.receiveShadow = true; me.castShadow = true;
      scene.add(me); return me;
    };
    mk(pos, idx, wallMat); mk(fpos, fidx, floorMat);
  }

  buildLair(scene, wallMat, floorMat, rand) {
    const y0 = floorY(-LEN);
    // a dome of rock
    const dome = new THREE.SphereGeometry(LAIR_R, 72, 36, 0, Math.PI * 2, 0, Math.PI * 0.55);
    const p = dome.attributes.position, v = new THREE.Vector3();
    for (let i = 0; i < p.count; i++) {
      v.fromBufferAttribute(p, i);
      const n = fbm(v.x * 0.08, v.y * 0.08, v.z * 0.08, 4) * 4 + fbm(v.x * 0.3, v.y * 0.3, v.z * 0.3, 2) * 0.8;
      v.multiplyScalar(1 + n / LAIR_R); v.y *= 0.62;
      p.setXYZ(i, v.x, v.y, v.z);
    }
    // turn it inside out
    const ix = dome.index.array; for (let i = 0; i < ix.length; i += 3) { const t = ix[i]; ix[i] = ix[i + 2]; ix[i + 2] = t; }
    dome.computeVertexNormals();
    const dm = new THREE.Mesh(dome, wallMat); dm.position.set(0, y0 - 2, LAIR_Z); dm.receiveShadow = true; scene.add(dm);
    const floor = new THREE.Mesh(new THREE.CircleGeometry(LAIR_R + 4, 64).rotateX(-Math.PI / 2), floorMat);
    floor.position.set(0, y0 - 0.05, LAIR_Z); floor.receiveShadow = true; scene.add(floor);
    // the hoard
    const coin = new THREE.CylinderGeometry(0.05, 0.05, 0.012, 14);
    const hoard = new THREE.InstancedMesh(coin, mats.gold(), 3000);
    const mx = new THREE.Matrix4(), q = new THREE.Quaternion(), e = new THREE.Euler();
    for (let i = 0; i < 3000; i++) {
      const a = rand() * Math.PI * 2, r = Math.pow(rand(), 0.7) * 7;
      const h = Math.max(0, 1.6 - r * 0.22) * (0.6 + rand() * 0.4);
      e.set(rand() * 1.5, rand() * 6, rand() * 1.5); q.setFromEuler(e);
      mx.compose(new THREE.Vector3(Math.cos(a) * r * 1.4, y0 + h * rand(), LAIR_Z - 18 + Math.sin(a) * r * 0.7), q, new THREE.Vector3(1, 1, 1));
      hoard.setMatrixAt(i, mx);
    }
    hoard.receiveShadow = true; scene.add(hoard);
    prop('treasure_chest', 1.4).then(m => { m.position.set(-4, y0, LAIR_Z - 19); m.rotation.y = 0.5; scene.add(m); });
    prop('treasure_chest', 1.2).then(m => { m.position.set(5, y0, LAIR_Z - 17); m.rotation.y = -0.7; scene.add(m); });
    // the stolen amulet glows on top of the pile
    this.amulet = new THREE.Group();
    this.amulet.add(mesh(new THREE.TorusGeometry(0.18, 0.03, 10, 32), mats.gold()));
    this.amulet.add(mesh(new THREE.OctahedronGeometry(0.11, 0), mats.glow(0x60ffe0, 5)));
    this.amulet.position.set(0, y0 + 1.9, LAIR_Z - 18);
    scene.add(this.amulet);
    this.amuletLight = new THREE.PointLight(0x60ffe0, 12, 12, 1.6); this.amuletLight.position.copy(this.amulet.position); scene.add(this.amuletLight);
    // the glow of the hoard and the dragon's own heat light the dome from below
    const glow = new THREE.PointLight(0xffc896, 140, 70, 1.2); glow.position.set(0, y0 + 12, LAIR_Z - 12); scene.add(glow);
    // braziers around the lair
    this.braziers = [];
    for (let i = 0; i < 4; i++) {
      const a = Math.PI * 0.25 + i * Math.PI * 0.5;
      const x = Math.cos(a) * (LAIR_R - 7), z = LAIR_Z + Math.sin(a) * (LAIR_R - 7);
      prop('stone_fire_pit', 1.6).then(m => { m.position.set(x, y0, z); scene.add(m); });
      const l = new THREE.PointLight(0xff8a3a, 150, 48, 1.3); l.position.set(x, y0 + 2.0, z); scene.add(l);
      this.braziers.push({ x, z, l });
      this.colliders.push({ type: 'circle', x, z, r: 1 });
    }
    // bones of those who came before
    for (let i = 0; i < 10; i++) {
      const a = rand() * Math.PI * 2, r = 8 + rand() * 16;
      this.bonePile(scene, Math.cos(a) * r, y0, LAIR_Z + Math.sin(a) * r, rand);
    }
  }
  bonePile(scene, x, y, z, rand) {
    const g = new THREE.Group();
    const bm = mats.bone();
    for (let i = 0; i < 6; i++) { const b = mesh(new THREE.CapsuleGeometry(0.035, 0.4 + rand() * 0.3, 4, 8), bm, (rand() - 0.5) * 0.8, 0.05, (rand() - 0.5) * 0.8); b.rotation.set(Math.PI / 2, rand() * 3, rand()); g.add(b); }
    const skull = mesh(new THREE.SphereGeometry(0.12, 16, 12), bm, 0, 0.11, 0); skull.scale.set(0.85, 1, 1.1); g.add(skull);
    g.add(mesh(new THREE.SphereGeometry(0.03, 8, 6), mats.dark(), 0.04, 0.13, 0.11), mesh(new THREE.SphereGeometry(0.03, 8, 6), mats.dark(), -0.04, 0.13, 0.11));
    for (let i = 0; i < 4; i++) { const rib = mesh(new THREE.TorusGeometry(0.15, 0.015, 6, 16, Math.PI), bm, 0.3, 0.05, -0.1 + i * 0.07); rib.rotation.x = Math.PI / 2; g.add(rib); }
    g.position.set(x, y, z); g.rotation.y = rand() * 6;
    scene.add(g);
    return g;
  }

  async makeEvent(scene, e, rand) {
    const g = this.game;
    switch (e.kind) {
      case 'BONES': {
        this.bonePile(scene, e.x, e.y, e.z, rand);
        const r = rand();
        e.content = r < 0.35 ? { k: 'gold', n: 20 + Math.floor(rand() * 50) } : r < 0.6 ? { k: 'item', item: ['POTION', 'KEY', 'RING', 'CHARM', 'STAFF', 'CROSS'][Math.floor(rand() * 6)] } : r < 0.85 ? { k: 'monster', names: ['GHOUL'] } : { k: 'none' };
        e.inter = { pos: new THREE.Vector3(e.x, e.y, e.z), radius: 2.5, label: e.done ? null : 'Search the bones', off: e.done, action: () => this.searchBones(e) };
        this.interactables.push(e.inter);
        break;
      }
      case 'TRAP': {
        e.plate = mesh(new THREE.BoxGeometry(1.8, 0.08, 1.8), triplanar('rock_wall_08', { scale: 2, color: 0x8a8070 }), e.x, e.y + 0.02, e.z);
        scene.add(e.plate);
        e.glow = new THREE.Mesh(new THREE.RingGeometry(1.1, 1.4, 4).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: 0xff3020, transparent: true, opacity: 0.8 }));
        e.glow.position.set(e.x, e.y + 0.1, e.z); e.glow.rotation.y = Math.PI / 4; e.glow.visible = this.state.vision && !e.done; scene.add(e.glow);
        break;
      }
      case 'PIT': {
        e.r = 1.8;
        const pit = new THREE.Mesh(new THREE.CircleGeometry(e.r, 32).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: 0x000000 }));
        pit.position.set(e.x, e.y + 0.09, e.z); scene.add(pit);
        const rim = new THREE.Mesh(new THREE.RingGeometry(e.r, e.r + 0.5, 32).rotateX(-Math.PI / 2), new THREE.MeshStandardMaterial({ color: 0x1a1612, roughness: 1, transparent: true, opacity: 0.7 }));
        rim.position.set(e.x, e.y + 0.08, e.z); scene.add(rim);
        e.glow = new THREE.Mesh(new THREE.RingGeometry(e.r + 0.1, e.r + 0.3, 32).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: 0xff3020 }));
        e.glow.position.set(e.x, e.y + 0.12, e.z); e.glow.visible = this.state.vision; scene.add(e.glow);
        break;
      }
      case 'DARTS': {
        e.side = rand() < 0.5 ? -1 : 1;
        e.wx = cx(e.z) + e.side * (hw(e.z) - 0.2);
        const hole = mesh(new THREE.CylinderGeometry(0.12, 0.12, 0.4, 12), mats.dark(), e.wx, e.y + 1.3, e.z); hole.rotation.z = Math.PI / 2; scene.add(hole);
        e.t = rand() * 2;
        break;
      }
      case 'ROCK': break;
      case 'MONSTER':
        e.names = [['GHOUL'], ['SNAKE'], ['SPIDER', 'SPIDER', 'SPIDER'], ['SCORPION'], ['SLIME'], ['DEMON'], ['WARRIOR'], ['GOLEM']][Math.floor(rand() * 8)];
        break;
      case 'DOOR': {
        const w = hw(e.z) + 1.4;
        e.group = new THREE.Group();
        const gate = await prop('large_iron_gate', w * 2).catch(() => null);
        if (gate) e.group.add(gate);
        // chains across it
        const link = new THREE.TorusGeometry(0.09, 0.025, 6, 12);
        for (const s of [-1, 1]) for (let i = 0; i < 18; i++) {
          const l = mesh(link, mats.steel(0x6a6a6a, 0.5), s * (w * 0.5 - i * w / 18) , 1.4 + Math.sin(i / 17 * Math.PI) * -0.4 + (s > 0 ? 0 : 0.3), 0.3);
          l.rotation.set(0, i % 2 ? Math.PI / 2 : 0, 0.5); e.group.add(l);
        }
        e.group.add(mesh(new THREE.BoxGeometry(0.3, 0.35, 0.15), mats.steel(0x5a4a3a, 0.5), 0, 1.4, 0.35));
        e.group.position.set(e.x, e.y, e.z);
        scene.add(e.group);
        if (e.open) e.group.position.y = e.y + 8;
        else { e.col = { type: 'box', x: e.x, z: e.z, hw: w, hd: 0.5, rot: 0 }; this.colliders.push(e.col); }
        e.inter = { pos: new THREE.Vector3(e.x, e.y, e.z + 1.5), radius: 3.5, label: e.open ? null : 'Examine the chained door', off: e.open, action: () => this.door(e) };
        this.interactables.push(e.inter);
        break;
      }
    }
  }

  start(from) {
    const pl = this.game.player;
    pl.pos.set(cx(-4), floorY(-4), -4); pl.yaw = Math.PI;
    if (from === 'resume' && this.state.pos) pl.pos.set(this.state.pos[0], 0, this.state.pos[1]);
    pl.pos.y = this.groundAt(pl.pos.x, pl.pos.z);
    const t = this.game.heroActor;
    // a burning torch in the left hand if there is no lantern
    this.torch = new THREE.Group();
    const flameSp = new THREE.Sprite(new THREE.SpriteMaterial({ color: 0xffb060, blending: THREE.AdditiveBlending, depthWrite: false, transparent: true }));
    import('../core/texgen.js').then(m => { flameSp.material.map = m.flame(); flameSp.material.needsUpdate = true; });
    flameSp.scale.set(0.25, 0.4, 1); flameSp.position.y = 0.5; this.torch.add(flameSp); this.flameSp = flameSp;
    this.torch.add(mesh(new THREE.CylinderGeometry(0.025, 0.03, 0.5, 8), mats.wood(), 0, 0.22, 0));
    if (!this.game.stats.goods.LANTERN) { t.L.ha.add(this.torch); this.torch.position.set(0, -0.08, 0.05); this.torch.rotation.x = 0.4; }
    else prop('wooden_lantern_01', 0.22).then(m => { m.position.set(0, -0.35, 0.05); t.L.ha.add(m); this.lanternModel = m; });
  }
  groundAt = (x, z) => {
    if (z < -LEN + 6) return floorY(-LEN);
    return floorY(z) + fbm(x * 0.4, z * 0.4, 1, 3) * 0.25;
  };
  ceiling = (x, z) => z < -LEN + 6 ? floorY(-LEN) + 16 : floorY(z) + this.ceilAt(x, z);
  clamp = (x, z, who) => {
    z = Math.min(6, z);
    if (z > -LEN + 2) {
      const c = cx(z), w = hw(z) - 0.4;
      x = THREE.MathUtils.clamp(x, c - w, c + w);
    } else {
      const dx = x, dz = z - LAIR_Z, d = Math.hypot(dx, dz), m = LAIR_R - 3;
      if (d > m && !(z > LAIR_Z + LAIR_R - 8 && Math.abs(x) < hw(-LEN))) { x = dx / d * m; z = LAIR_Z + dz / d * m; }
    }
    return [x, z];
  };

  update(dt) {
    const g = this.game, pl = g.player, p = g.stats;
    // your light swings with you
    const hand = (g.heroActor.L.ha).getWorldPosition(new THREE.Vector3());
    // held out ahead of you, so the flame doesn't blow out your own helmet
    this.light.position.copy(hand).addScaledVector(pl.forward(), 0.35).add(new THREE.Vector3(0, 0.6, 0));
    this.fill.position.copy(g.cam.cam.position).lerp(pl.pos, 0.35).y += 0.6;
    this.light.intensity = (p.goods.LANTERN ? 26 : 14) * (0.9 + Math.random() * 0.1 + Math.sin(g.time * 13) * 0.03);
    if (this.flameSp) { this.flameSp.scale.set(0.22 + Math.random() * 0.06, 0.38 + Math.random() * 0.1, 1); if (Math.random() < 0.2) this.fx.burst(this.flameSp.getWorldPosition(new THREE.Vector3()), 'ember', 1); }
    this.state.pos = [pl.pos.x, pl.pos.z];
    for (const b of this.braziers) { b.l.intensity = 150 * (0.85 + Math.random() * 0.15); if (Math.random() < 0.3) this.fx.burst(new THREE.Vector3(b.x, floorY(-LEN) + 1, b.z), 'ember', 1); if (Math.random() < 0.15) this.fx.burst(new THREE.Vector3(b.x, floorY(-LEN) + 1.1, b.z), 'fire', 1); }
    this.amulet.rotation.y += dt; this.amuletLight.intensity = 10 + Math.sin(g.time * 3) * 3;
    for (const e of this.events) this.updateEvent(e, dt);
    this.updateDarts(dt);
    this.updateRocks(dt);
    this.fire.update(dt);
    // the lair
    const inLair = pl.pos.z < -LEN + 4;
    if (inLair !== this.inLair) { this.inLair = inLair; ambience(inLair ? 'lair' : 'cave'); }
    this.boss.update(dt);
    // the lair is lit by braziers: let some bounce light in, and pull the camera back for the fight
    const lairK = THREE.MathUtils.clamp((-LEN + 10 - pl.pos.z) / 14, 0, 1);
    this.hemi.intensity = (p.goods.LANTERN ? 0.12 : 0.06) + lairK * 0.35;
    g.scene.environmentIntensity = 0.15 + lairK * 0.5;
    const want = this.boss.awake && !this.boss.dead ? 3.2 : 0;
    g.cam.extraDist += (want - g.cam.extraDist) * Math.min(1, dt * 1.5);
    // pick up the amulet once the dragon is dead
    if (this.boss.dead && !this.won && pl.pos.distanceTo(this.amulet.position) < 3) { this.won = true; this.amulet.visible = false; g.onVictory?.(); }
  }
  bossTargets() { return this.boss && !this.boss.dead && this.boss.awake ? [this.boss] : []; }

  updateEvent(e, dt) {
    const g = this.game, pl = g.player;
    const d = Math.hypot(pl.pos.x - e.x, pl.pos.z - e.z);
    switch (e.kind) {
      case 'TRAP':
        if (!e.done && d < 1.1 && pl.onGround) {
          e.done = true; this.state.done.push(e.id); e.glow.visible = false;
          e.plate.position.y -= 0.06; sfx('trap'); shake.add(0.5);
          g.stats.stuck = R.randBelow(3) + 3;
          g.hud.say(['>>TRAP<<', 'YOU ARE CAUGHT', 'PRESS SPACE TO STRUGGLE'], 4);
        }
        break;
      case 'PIT':
        if (d < e.r - 0.3 && !pl.falling) {
          pl.falling = true; sfx('rock'); shake.add(0.9);
          const p = g.stats;
          if (p.goods.ROPE > 0) g.hud.say(['YOU FELL INTO THE PIT', 'USING ROPE', 'YOU CLIMB OUT'], 4);
          else { const dmg = R.randBelow(5) + 2; p.str -= dmg; g.playerHurt(dmg); g.hud.say(['YOU FELL INTO THE PIT', 'YOU TAKE ' + R.pad(dmg) + ' UNITS DAMAGE'], 4); }
          pl.pos.set(e.x, e.y, e.z + e.r + 1.2); pl.vel.set(0, 0, 0);
          setTimeout(() => { pl.falling = false; }, 600);
        }
        break;
      case 'ROCK':
        if (!e.done && Math.abs(pl.pos.z - e.z) < 3) { e.done = true; this.state.done.push(e.id); this.dropRock(pl.pos.clone().add(new THREE.Vector3(pl.vel.x * 0.5, 0, pl.vel.z * 0.5 - 0.6))); }
        break;
      case 'DARTS':
        if (e.done) break;
        e.t -= dt;
        if (e.t <= 0 && Math.abs(pl.pos.z - e.z) < 16) {
          e.t = 2.4;
          const dart = mesh(new THREE.CylinderGeometry(0.01, 0.01, 0.4, 6), mats.steel(0x606060, 0.4), e.wx, e.y + 1.3, e.z);
          dart.rotation.z = Math.PI / 2;
          this.game.scene.add(dart);
          this.darts.push({ m: dart, v: -e.side * 16, life: 1.5, e });
          sfx('arrow', { vol: 0.6 });
        }
        break;
      case 'MONSTER':
        if (!e.done && d < 14) { e.done = true; this.state.done.push(e.id); g.spawn(e.names, new THREE.Vector3(e.x, e.y, e.z - 3)); g.hud.say(['OH NO', e.names.length > 1 ? e.names[0] + 'S' : (/^[AEIOU]/.test(e.names[0]) ? 'AN ' : 'A ') + e.names[0]]); }
        break;
      case 'DOOR':
        if (e.opening > 0) { e.opening -= dt; e.group.position.y = e.y + 8 * (1 - Math.max(0, e.opening) / 1.6); }
        break;
    }
  }
  updateDarts(dt) {
    const g = this.game, pl = g.player, p = g.stats;
    for (let i = this.darts.length - 1; i >= 0; i--) {
      const d = this.darts[i];
      d.life -= dt; d.m.position.x += d.v * dt;
      const hit = Math.hypot(d.m.position.x - pl.pos.x, d.m.position.z - pl.pos.z) < 0.6 && Math.abs(d.m.position.y - (pl.pos.y + 1.3)) < 1;
      if (hit && pl.iframes <= 0) {
        if (pl.blocking) { sfx('block'); this.fx.burst(d.m.position, 'spark', 10); }
        else if (p.protect > 0) { p.protect--; floatText(pl.pos.clone().setY(pl.pos.y + 2.2), 'NO EFFECT', 'miss'); }
        else { d.e.done = true; p.fever = 1; p.str -= 1; g.playerHurt(1); g.hud.say(['POISON DART', 'YOU ARE SICK WITH FEVER'], 4); }
      }
      if (hit || d.life <= 0) { d.m.parent?.remove(d.m); this.darts.splice(i, 1); }
    }
  }
  dropRock(at) {
    const g = this.game;
    sfx('rock', { vol: 0.5 });
    const shadow = new THREE.Mesh(new THREE.CircleGeometry(1.3, 24).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: 0x000000, transparent: true, opacity: 0.5, depthWrite: false }));
    at.y = this.groundAt(at.x, at.z);
    shadow.position.copy(at).setY(at.y + 0.05); shadow.scale.setScalar(0.1);
    const rock = mesh(rockGeo(0.9, 2, 0.3, Math.random() * 10), triplanar('cliff_side', { scale: 2.5, color: 0xb8aa98 }), at.x, at.y + 14, at.z);
    g.scene.add(shadow, rock);
    this.rocks.push({ rock, shadow, at, t: 0 });
  }
  updateRocks(dt) {
    const g = this.game, pl = g.player, p = g.stats;
    for (let i = this.rocks.length - 1; i >= 0; i--) {
      const r = this.rocks[i];
      r.t += dt;
      const k = Math.min(1, r.t / 1.0);
      r.shadow.scale.setScalar(0.1 + k);
      r.rock.position.y = r.at.y + 14 * (1 - k * k) + 0.6;
      r.rock.rotation.x += dt * 3;
      if (k >= 1) {
        this.rocks.splice(i, 1); r.shadow.parent?.remove(r.shadow);
        sfx('boom'); shake.add(0.9);
        this.fx.burst(r.at.clone().setY(r.at.y + 0.5), 'dust', 40); this.fx.burst(r.at.clone().setY(r.at.y + 0.5), 'spark', 10);
        this.colliders.push({ type: 'circle', x: r.at.x, z: r.at.z, r: 1 });
        if (Math.hypot(pl.pos.x - r.at.x, pl.pos.z - r.at.z) < 1.8 && pl.iframes <= 0) {
          const dmg = R.randBelow(4) + 2; p.str -= dmg; g.playerHurt(dmg);
          g.hud.say(['ROCK', 'YOU WERE HIT', 'YOU TAKE ' + R.pad(dmg) + ' UNITS DAMAGE'], 3.5);
        }
      }
    }
  }

  async searchBones(e) {
    const g = this.game;
    if (e.done) { g.hud.say(['BONES', 'NOBODY HERE']); return; }
    e.done = true; this.state.done.push(e.id); e.inter.off = true;
    const c = e.content, at = new THREE.Vector3(e.x + 0.8, e.y, e.z + 0.6);
    if (c.k === 'gold') { g.drop('GOLD', at, c.n); g.hud.say(['BONES', 'SOMETHING GLINTS']); }
    else if (c.k === 'item') { g.drop(c.item, at); g.hud.say(['BONES', 'SOMETHING IS HERE']); }
    else if (c.k === 'monster') { g.hud.say(['BONES', 'THE BONES RISE', 'A GHOUL'], 3.5); g.spawn(['GHOUL'], new THREE.Vector3(e.x, e.y, e.z)); shake.add(0.4); }
    else g.hud.say(['BONES', 'NOBODY HERE']);
  }
  async door(e) {
    const g = this.game, p = g.stats;
    if (e.open) return;
    const men = g.allies.filter(a => a.alive).length;
    const v = await g.hud.dialog({ title: 'A Chained Door', text: 'Heavy chains and a rusted padlock bar the way.', options: [
      { label: 'Cast UNLOCK', sub: p.spells.UNLOCK ? `You have ${p.spells.UNLOCK}` : 'You have no UNLOCK spell', value: 'spell', off: !p.spells.UNLOCK },
      { label: 'Use a KEY', sub: p.inv.KEY ? `You have ${p.inv.KEY}` : 'You have no key', value: 'key', off: !p.inv.KEY },
      { label: 'Bash it', sub: men ? `${men} warrior${men > 1 ? 's' : ''} will help` : 'It may hurt', value: 'bash' },
      { label: 'Leave', value: null }] });
    if (!v) return;
    if (v === 'spell') { p.spells.UNLOCK--; sfx('magic'); return this.openDoor(e); }
    if (v === 'key') { p.inv.KEY--; return this.openDoor(e); }
    sfx('clang'); shake.add(0.4);
    if (R.randBelow(9) < 3 + men * 2) return this.openDoor(e);
    p.str -= 1; g.playerHurt(1); g.hud.say(['THE DOOR HOLDS', 'YOU TAKE 01 UNITS DAMAGE']);
  }
  openDoor(e) {
    e.open = true; e.opening = 1.6; this.state.doors.push(e.id);
    if (e.col) e.col.off = true;
    e.inter.off = true;
    sfx('chain'); sfx('door');
    this.game.hud.say(['THE DOOR', 'OPENS']);
  }
  async onKey() {
    const e = this.events.find(e => e.kind === 'DOOR' && !e.open && Math.hypot(this.game.player.pos.x - e.x, this.game.player.pos.z - e.z) < 5);
    if (!e) return false;
    this.game.stats.inv.KEY--; this.openDoor(e); return true;
  }
  async onSpell(name) {
    const g = this.game, p = g.stats;
    if (name === 'VISION') {
      p.spells.VISION--; sfx('magic');
      this.state.vision = true;
      for (const e of this.events) if (e.glow && !e.done) e.glow.visible = true;
      g.hud.say(['YOU SEE', 'THE TRAPS']);
      return true;
    }
    if (name === 'UNLOCK') {
      const e = this.events.find(e => e.kind === 'DOOR' && !e.open && Math.hypot(g.player.pos.x - e.x, g.player.pos.z - e.z) < 6);
      if (!e) return false;
      p.spells.UNLOCK--; sfx('magic'); this.openDoor(e); return true;
    }
    if (this.boss.awake && !this.boss.dead && ['BLAST', 'FLASH', 'STUN'].includes(name)) return this.boss.spell(name);
    return false;
  }
  arrow(from, dir) {
    if (!this.boss.awake || this.boss.dead) return false;
    const to = this.boss.pos.clone().setY(this.boss.pos.y + 2.5).sub(from);
    if (dir.angleTo(to) < 0.25 && to.length() < 45) this.boss.arrowHit();
    return true;
  }
  dispose() {}
}

// The dragon's mind: wakes when you enter the lair, then bites, claws, sweeps its tail,
// roars and breathes fire. Its bites use the ROM's contest() against your DEX.
class DragonBoss {
  constructor(cave, dragon) {
    this.cave = cave; this.d = dragon; this.game = cave.game;
    this.hp = DRAGON.hp; this.awake = false; this.dead = false;
    this.radius = 3.2; this.mode = 'sleep'; this.t = 0; this.cool = 2; this.stun = 0; this.blind = 0;
    this.fireT = 0;
  }
  get pos() { return this.d.root.position; }
  get alive() { return !this.dead; }
  get stats() { return { atk: DRAGON.atk, dmg: DRAGON.dmg, hpLeft: this.hp, hp: DRAGON.hp, name: 'DRAGON' }; }
  update(dt) {
    const g = this.game, pl = g.player, d = this.d, s = d.state;
    s.hit = Math.max(0, s.hit - dt * 3);
    if (this.dead) { this.deadT = (this.deadT || 0) + dt; s.dead = Math.min(1, this.deadT * 0.5); d.update(dt); g.hud.boss(null); return; }
    d.lookTarget = pl.pos.clone().setY(pl.pos.y + 1.5);
    if (!this.awake) {
      d.update(dt * 0.3);
      if (pl.pos.z < -LEN - 6) {
        this.awake = true; this.mode = 'roar'; this.t = 0; s.attack = 0; s.attackKind = 'roar';
        sfx('roar'); shake.add(1.2);
        g.hud.say(['THE DRAGON', 'AWAKES!'], 3);
      }
      return;
    }
    g.hud.boss('THE DRAGON', this.hp / DRAGON.hp);
    this.t += dt;
    if (this.stun > 0) { this.stun -= dt; s.attack = -1; s.speed = 0; d.update(dt * 0.3); return; }
    this.blind = Math.max(0, this.blind - dt);
    const to = pl.pos.clone().sub(this.pos); to.y = 0;
    const dist = to.length();
    const want = Math.atan2(to.x, to.z);
    let da = want - d.root.rotation.y; da = Math.atan2(Math.sin(da), Math.cos(da));
    const facing = Math.abs(da) < 0.6;
    if (this.mode === 'idle' || this.mode === 'walk') {
      d.root.rotation.y += THREE.MathUtils.clamp(da, -1.2 * dt, 1.2 * dt);
      this.cool -= dt;
      if (dist > 7 && this.cool > 0) { this.mode = 'walk'; const f = new THREE.Vector3(Math.sin(d.root.rotation.y), 0, Math.cos(d.root.rotation.y)); this.pos.addScaledVector(f, 2.6 * dt); s.speed = 2.6; }
      else { this.mode = 'idle'; s.speed = 0; }
      // stay in the lair
      const r = Math.hypot(this.pos.x, this.pos.z - LAIR_Z);
      if (r > LAIR_R - 9) { this.pos.x *= (LAIR_R - 9) / r; this.pos.z = LAIR_Z + (this.pos.z - LAIR_Z) * (LAIR_R - 9) / r; }
      if (this.cool <= 0) {
        this.t = 0;
        if (Math.abs(da) > 2.2 && dist < 9) this.mode = 'tail';
        else if (dist < 6.5 && facing) this.mode = Math.random() < 0.55 ? 'bite' : 'claw';
        else if (facing && dist < 22) this.mode = 'breath';
        else this.mode = 'walk';
        if (this.mode !== 'walk') { s.attack = 0; s.attackKind = this.mode === 'tail' ? 'roar' : this.mode; this.struck = false; if (this.mode !== 'tail') sfx('growl', { pitch: 0.5 }); }
        this.cool = 1.4 + Math.random() * 1.2 - (this.hp < DRAGON.hp / 2 ? 0.5 : 0);
      }
    }
    if (['bite', 'claw', 'breath', 'roar', 'tail'].includes(this.mode)) {
      const dur = { bite: 1.3, claw: 1.4, breath: 3.0, roar: 2.4, tail: 1.6 }[this.mode];
      s.attack = Math.min(1, this.t / dur);
      if (this.mode !== 'breath') d.root.rotation.y += THREE.MathUtils.clamp(da, -0.6 * dt, 0.6 * dt);
      if (this.mode === 'tail') d.root.rotation.y += dt * 4.2 * (this.t < dur / 2 ? 1 : 0);
      if ((this.mode === 'bite' && s.attack > 0.45) || (this.mode === 'claw' && s.attack > 0.42) || (this.mode === 'tail' && s.attack > 0.3)) if (!this.struck) { this.struck = true; this.melee(this.mode); }
      if (this.mode === 'breath' && s.attack > 0.38 && s.attack < 0.85) this.breathe(dt);
      if (this.mode === 'breath' && s.attack > 0.38 && !this.fireSound) { this.fireSound = true; sfx('fire', { dur: 1.4 }); sfx('roar'); }
      if (this.t >= dur) { this.mode = 'idle'; s.attack = -1; this.fireSound = false; d.fireLight.intensity = 0; }
    }
    d.update(dt);
    if (this.mode !== 'walk') s.speed = 0;
  }
  melee(kind) {
    const g = this.game, pl = g.player, p = g.stats;
    const to = pl.pos.clone().sub(this.pos); to.y = 0;
    const reach = kind === 'tail' ? 9 : kind === 'claw' ? 6.5 : 7.2;
    const fwd = new THREE.Vector3(Math.sin(this.d.root.rotation.y), 0, Math.cos(this.d.root.rotation.y));
    // allies near the jaws can be caught too
    for (const a of g.allies) if (a.alive && a.pos.distanceTo(this.pos) < reach && (kind === 'tail' || fwd.angleTo(a.pos.clone().sub(this.pos).setY(0)) < 0.9) && Math.random() < 0.5) { a.hp -= R.randBelow(8) + 2; a.actor.state.hit = 1; if (a.hp <= 0) { a.die(); g.hud.say(['A WARRIOR', 'WAS KILLED']); } }
    if (to.length() > reach || (kind !== 'tail' && fwd.angleTo(to) > 0.9)) { sfx('miss'); return; }
    if (pl.iframes > 0) { floatText(pl.pos.clone().setY(pl.pos.y + 2.2), 'DODGED', 'miss'); return; }
    if (this.blind > 0 && Math.random() < 0.7) { floatText(pl.pos.clone().setY(pl.pos.y + 2.2), 'BLINDED', 'miss'); return; }
    if (pl.blocking && kind !== 'tail') {
      pl.stamina -= 45; pl.staminaDelay = 1; sfx('block'); shake.add(0.6);
      g.fx.burst(pl.pos.clone().setY(pl.pos.y + 1.3), 'spark', 30);
      if (pl.stamina > 0) { floatText(pl.pos.clone().setY(pl.pos.y + 2.2), 'BLOCKED', 'miss'); return; }
      pl.stamina = 0;
    }
    if (!R.contest(p.dex, DRAGON.atk)) { floatText(pl.pos.clone().setY(pl.pos.y + 2.2), 'MISSED', 'miss'); return; }
    let dmg = R.randBelow(DRAGON.dmg) + (kind === 'tail' ? 1 : 0);
    if (p.protect > 0) { p.protect--; floatText(pl.pos.clone().setY(pl.pos.y + 2.2), 'NO EFFECT', 'miss'); return; }
    if (p.inv.SHIELD > 0 && dmg) dmg = Math.max(1, dmg - 1);
    if (p.goods.CHAIN > 0 && dmg) dmg = Math.max(1, dmg - 1);
    if (!dmg) { floatText(pl.pos.clone().setY(pl.pos.y + 2.2), 'MISSED', 'miss'); return; }
    p.str -= dmg; g.playerHurt(dmg);
    // knocked back
    pl.vel.addScaledVector(to.normalize(), 7);
  }
  breathe(dt) {
    const g = this.game, pl = g.player, p = g.stats, d = this.d;
    const mouth = d.mouthWorld();
    const yaw = d.root.rotation.y + (d.neck.reduce((a, b) => a + b.rotation.y, 0));
    let dir = new THREE.Vector3(Math.sin(yaw), -0.28, Math.cos(yaw)).normalize();
    if (this.blind > 0) dir.applyAxisAngle(new THREE.Vector3(0, 1, 0), Math.sin(g.time * 3) * 0.8);
    this.cave.fire.emit(mouth, dir, 160, dt);
    d.fireLight.intensity = 90 + Math.random() * 40;
    shake.add(dt * 0.6);
    // the cone: if you stand in it, you burn
    this.fireT -= dt;
    const to = pl.pos.clone().setY(pl.pos.y + 1).sub(mouth);
    const inCone = to.length() < 20 && dir.angleTo(to) < 0.3 && pl.iframes <= 0;
    for (const a of g.allies) if (a.alive && dir.angleTo(a.pos.clone().setY(a.pos.y + 1).sub(mouth)) < 0.3 && a.pos.distanceTo(mouth) < 20 && Math.random() < dt * 2) { a.hp -= 3; if (a.hp <= 0) { a.die(); g.hud.say(['A WARRIOR', 'WAS KILLED']); } }
    if (inCone && this.fireT <= 0) {
      this.fireT = 0.35;
      let dmg = R.randBelow(3) + 1;
      if (pl.blocking) { pl.stamina -= 12; if (pl.stamina > 0) dmg = Math.max(0, dmg - 1); }
      if (p.goods.RUBY > 0) dmg = Math.max(0, dmg >> 1);
      if (p.protect > 0) { p.protect--; dmg = 0; floatText(pl.pos.clone().setY(pl.pos.y + 2.2), 'NO EFFECT', 'miss'); }
      if (dmg) { p.str -= dmg; g.playerHurt(dmg); }
    }
  }
  // your sword
  onHitByPlayer(combo) {
    const g = this.game, p = g.stats, pl = g.player;
    p.bonus = p.inv.HANDAXE > 0 ? 2 : 0;
    if (!R.contest(DRAGON.atk, p.dex)) { floatText(this.pos.clone().setY(this.pos.y + 3.5), 'MISS', 'miss'); sfx('hitShell'); return; }
    const dmg = R.randBelow(p.weapon) + p.bonus + (combo === 2 ? 1 : 0);
    if (!dmg) { floatText(this.pos.clone().setY(this.pos.y + 3.5), 'MISS', 'miss'); return; }
    this.hurt(dmg, pl.pos.clone().setY(pl.pos.y + 1.4).lerp(this.pos.clone().setY(this.pos.y + 2.5), 0.5));
  }
  onHitByAlly(a) { if (R.contest(DRAGON.atk, 14)) this.hurt(R.randBelow(3) + 1, a.pos.clone().setY(a.pos.y + 1.5).lerp(this.pos.clone().setY(this.pos.y + 2), 0.4)); }
  arrowHit() { if (R.contest(DRAGON.atk, this.game.stats.dex)) this.hurt(R.randBelow(3) + 1, this.pos.clone().setY(this.pos.y + 3)); else floatText(this.pos.clone().setY(this.pos.y + 4), 'MISS', 'miss'); }
  hurt(dmg, at) {
    const g = this.game;
    this.hp -= dmg; this.d.state.hit = 1;
    g.fx.burst(at, 'blood', 16 + dmg * 3); g.fx.burst(at, 'spark', 6);
    sfx('hitFlesh'); shake.add(0.2); hitstop.add(0.04);
    floatText(at.clone().setY(at.y + 0.8), String(dmg), 'dmg');
    if (this.hp < 0) this.die();
  }
  spell(name) {
    const g = this.game, p = g.stats;
    p.spells[name]--; sfx('magic');
    if (!p.goods.SAPPHIRE && R.randBelow(2) === 0) { g.hud.say(['DRAGON MAGIC', 'CANCELS IT']); g.fx.burst(this.pos.clone().setY(this.pos.y + 3), 'magic', 30); return true; }
    if (name === 'BLAST') { const dmg = R.randBelow(12) + 6; g.fx.burst(this.d.mouthWorld(), 'fire', 50); sfx('boom'); shake.add(0.6); this.hurt(dmg, this.pos.clone().setY(this.pos.y + 3)); g.hud.say(['BLAST', 'FOR ' + R.pad(dmg) + ' UNITS']); }
    if (name === 'FLASH') { this.blind = 8; g.fx.burst(this.d.mouthWorld(), 'magic', 80); g.hud.say(['THE DRAGON', 'IS BLINDED']); }
    if (name === 'STUN') { this.stun = 5; this.mode = 'idle'; this.d.state.attack = -1; g.hud.say(['THE DRAGON', 'IS STUNNED']); }
    return true;
  }
  die() {
    const g = this.game;
    this.dead = true; this.cave.state.dragonDead = true;
    this.d.state.attack = -1; this.d.fireLight.intensity = 0;
    sfx('roar'); sfx('boom'); shake.add(1.5);
    g.fx.burst(this.pos.clone().setY(this.pos.y + 3), 'blood', 120); g.fx.burst(this.pos.clone().setY(this.pos.y + 2), 'dust', 60);
    g.stats.kills++;
    g.hud.say(['YOU KILLED', 'THE DRAGON'], 4);
    setTimeout(() => g.hud.say(['THE AMULET', 'GLOWS ON THE HOARD'], 4), 2500);
  }
}
