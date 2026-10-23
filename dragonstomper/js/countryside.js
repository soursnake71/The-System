// Part 1: the Enchanted Countryside. A valley of rows of scenery, like the original's scrolling
// strip, with something (or nothing) hidden behind every tree, hut and rock. The bridge guard
// at the far end wants your ID.
import * as THREE from '../lib/three.module.min.js';
import { ROM } from '../data.js';
import { G, newScene, addCollider, removeCollider, moveHero, mulberry, save, floatText } from './engine.js';
import { fromSprite, meshGeometry, voxMat, spriteMesh } from './voxel.js';
import { battle, monsterMesh, showPickup, article, wait, hurtFx } from './play.js';
import { sfx } from './audio.js';
import * as R from './rules.js';

const S = ROM.part1.scenery;
const ROWS = 44, GAP = 12, Z0 = -16, HALF = 22;
const END = Z0 - ROWS * GAP;           // last row
const RIVER = END - 16, BRIDGE_W = 3.2;

const KINDS = {
  PINE: { mode: 'lathe', scale: 2.4, r: 1.5, name: 'TREE', w: 16 },
  OAK: { mode: 'lathe', scale: 2.4, r: 1.7, name: 'TREE', w: 13 },
  GRASS: { mode: 'taper', scale: 1.6, r: 1.2, name: 'GRASS', w: 9, soft: true, depth: 6 },
  BUSH: { flat: true, scale: 1.6, r: 3.2, name: 'LAKE', w: 5, color: '#2d57b0' },
  SWAMP: { flat: true, scale: 1.8, r: 3.2, name: 'SWAMP', w: 5, soft: true, color: '#185080' },
  HUT: { mode: 'taper', scale: 2.6, r: 2.6, name: 'HUT', w: 7 },
  CHURCH: { mode: 'taper', scale: 3.0, r: 3, name: 'CHURCH', w: 0 },
  TEMPLE: { mode: 'taper', scale: 2.8, r: 2.8, name: 'TEMPLE', w: 4 },
  RUIN: { mode: 'taper', scale: 2.6, r: 2.6, name: 'CASTLE', w: 3, color: '#6f6f6f' },
  SKULLROCK: { mode: 'taper', scale: 1.7, r: 1.5, name: 'ROCK', w: 5, color: '#8e8e8e' },
  BOULDER: { mode: 'lathe', scale: 1.7, r: 1.5, name: 'ROCK', w: 5, color: '#6f6f6f' },
  BRAMBLE: { mode: 'taper', scale: 1.9, r: 1.6, name: 'TREE', w: 4, depth: 5 },
};
const TIERS = [
  ['MONKEY', 'BUG', 'BEETLE', 'DEMON', 'SPIDER', 'MONKEY'],
  ['SNAKE', 'SLIME', 'GOLEM', 'GHOUL', 'MANIAC', 'BEETLE'],
  ['WARRIOR', 'SCORPION', 'GHOUL', 'SNAKE', 'MANIAC', 'SLIME'],
];
const LOOT = ['POTION', 'POTION', 'KEY', 'KEY', 'KEY', 'CHEST', 'CHEST', 'CROSS', 'RING', 'STAFF', 'CHARM', 'SHIELD', 'HANDAXE', 'PAPER'];

export class Countryside {
  constructor(state = {}) {
    this.state = { searched: [], guardGone: false, ...state };
    this.ctx = 'field';
    this.objects = [];
  }

  build() {
    const s = newScene({ sky: 0x84c8fc, fog: [70, 200], hemiGround: 0x48480f });
    this.addGround(s);
    const rand = mulberry(G.p.seed);
    const pickW = (list) => { let t = list.reduce((a, k) => a + KINDS[k].w, 0) * rand(); for (const k of list) { t -= KINDS[k].w; if (t <= 0) return k; } return list[0]; };
    const geos = {};
    const geoFor = (k) => {
      if (geos[k]) return geos[k];
      const kd = KINDS[k], sp = S[k];
      const sc = kd.scale;
      geos[k] = meshGeometry(fromSprite(sp.rows, kd.color || sp.color, kd.mode, { depth: kd.depth || 6 }), 0.3 * sc, 0.22 * sc, 0.3 * sc);
      return geos[k];
    };
    let id = 0, churches = 0, maniacs = 0;
    for (let row = 0; row < ROWS; row++) {
      const z = Z0 - row * GAP + (rand() - 0.5) * 3;
      const t = row / ROWS;
      let k = pickW(Object.keys(KINDS).filter(x => KINDS[x].w));
      if ((row === 9 || row === 24 || row === 38) && churches < 3) { k = 'CHURCH'; churches++; }
      // NUSIZ-style copies: one, two far apart or three close
      const n = k === 'CHURCH' ? 1 : [1, 2, 2, 3, 3][Math.floor(rand() * 5)];
      const xs = n === 1 ? [(rand() - 0.5) * 24] : n === 2 ? [-9 - rand() * 6, 9 + rand() * 6] : [-13, 0, 13].map(v => v + (rand() - 0.5) * 3);
      for (const x of xs) {
        if (Math.abs(x) < 2.5 && row < 2) continue;
        const kd = KINDS[k];
        const obj = { id: id++, kind: k, name: kd.name, x, z, t };
        obj.content = this.roll(obj, rand, t);
        if (obj.content.k === 'monster' && obj.content.names[0] === 'MANIAC') maniacs++;
        if (this.state.searched.includes(obj.id)) obj.searched = true;
        let mesh;
        if (kd.flat) mesh = this.flatPool(S[k].rows, kd.color, kd.scale);
        else { mesh = new THREE.Mesh(geoFor(k), voxMat); mesh.castShadow = mesh.receiveShadow = true; }
        mesh.position.set(x, 0, z);
        mesh.rotation.y = kd.mode === 'lathe' ? rand() * 6 : (rand() - 0.5) * 0.3;
        s.add(mesh);
        obj.mesh = mesh;
        if (obj.searched) this.dim(obj);
        const c = addCollider(x, z, kd.r, obj);
        c.soft = !!kd.soft;
        obj.col = c;
        this.objects.push(obj);
      }
    }
    // make sure an ID can be had: the MANIAC always drops his PAPER
    if (maniacs < 2) {
      const mid = this.objects.filter(o => o.t > 0.3 && ['monster', 'none', 'gold'].includes(o.content.k) && !['CHURCH'].includes(o.kind));
      for (let i = 0; i < 2 - maniacs && mid.length; i++) mid.splice(Math.floor(rand() * mid.length), 1)[0].content = { k: 'monster', names: ['MANIAC', 'MANIAC', 'MANIAC'] };
    }
    this.addForest(s, rand);
    this.addBridge(s);
    G.walkSpeed = 7;
  }

  roll(obj, rand, t) {
    const tier = TIERS[Math.min(2, Math.floor(t * 3))];
    const monster = () => {
      const n = tier[Math.floor(rand() * tier.length)];
      return { k: 'monster', names: R.MONSTERS[n].group ? [n, n, n] : [n] };
    };
    const item = () => ({ k: 'item', item: LOOT[Math.floor(rand() * LOOT.length)] });
    const gold = () => ({ k: 'gold', n: 10 + Math.floor(rand() * (20 + 60 * t)) });
    const r = rand();
    switch (obj.kind) {
      case 'CHURCH': return { k: 'church' };
      case 'HUT': case 'TEMPLE': case 'RUIN':
        if (r < 0.35) return { ...monster(), then: item().item };
        if (r < 0.55) return { k: 'locked', item: item().item, gold: 50 + Math.floor(rand() * 80) };
        if (r < 0.8) return item();
        return gold();
      case 'SKULLROCK': case 'BOULDER':
        if (r < 0.35) return { k: 'trap' };
        if (r < 0.6) return { k: 'pit' };
        if (r < 0.8) return gold();
        return { k: 'none' };
      case 'SWAMP': return r < 0.5 ? { k: 'sick' } : r < 0.75 ? { k: 'monster', names: ['SLIME'] } : { k: 'none' };
      case 'BUSH': return r < 0.4 ? { k: 'monster', names: ['SNAKE'] } : r < 0.7 ? item() : { k: 'none' };
      case 'GRASS': return r < 0.45 ? { k: 'monster', names: [rand() < 0.5 ? 'SNAKE' : 'SCORPION'] } : r < 0.6 ? gold() : { k: 'none' };
      default:
        if (r < 0.42) return monster();
        if (r < 0.58) return item();
        if (r < 0.74) return gold();
        return { k: 'none' };
    }
  }

  // lakes and swamps: the sprite read as a map seen from above
  flatPool(rows, color, sc) {
    const g = new THREE.Group();
    const geo = new THREE.BoxGeometry(0.62 * sc, 0.12, 0.42 * sc);
    const mat = new THREE.MeshLambertMaterial({ color, emissive: new THREE.Color(color).multiplyScalar(0.25) });
    rows.forEach((row, ry) => { for (let x = 0; x < 8; x++) if ((row >> (7 - x)) & 1) {
      const m = new THREE.Mesh(geo, mat);
      m.position.set((x - 3.5) * 0.62 * sc, 0.04, (ry - rows.length / 2) * 0.42 * sc);
      m.receiveShadow = true;
      g.add(m);
    } });
    return g;
  }

  addGround(s) {
    const geo = new THREE.PlaneGeometry(170, Math.abs(RIVER) + 120, 85, 220);
    geo.rotateX(-Math.PI / 2);
    geo.translate(0, 0, RIVER / 2 - 10);
    const pos = geo.attributes.position, col = [];
    const c = new THREE.Color();
    for (let i = 0; i < pos.count; i++) {
      const x = pos.getX(i), z = pos.getZ(i);
      const out = Math.max(0, Math.abs(x) - HALF - 2);
      const n = Math.sin(x * 0.37 + z * 0.11) * Math.cos(z * 0.23 - x * 0.05);
      pos.setY(i, out * 0.35 * (1 + 0.4 * n) + (out > 0 ? Math.sin(z * 0.15) * out * 0.08 : 0));
      const v = 0.5 + 0.5 * Math.sin(x * 1.3) * Math.cos(z * 1.7);
      c.set(0x505916).lerp(new THREE.Color(0x6d762b), v * 0.6 + n * 0.15);
      if (out > 0) c.lerp(new THREE.Color(0x355f18), Math.min(1, out / 8));
      col.push(c.r, c.g, c.b);
    }
    geo.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
    geo.computeVertexNormals();
    const m = new THREE.Mesh(geo, new THREE.MeshLambertMaterial({ vertexColors: true }));
    m.receiveShadow = true;
    s.add(m);
    // tufts of grass, instanced
    const tuft = new THREE.ConeGeometry(0.12, 0.5, 3); tuft.translate(0, 0.25, 0);
    const im = new THREE.InstancedMesh(tuft, new THREE.MeshLambertMaterial({ color: 0x87b754 }), 1400);
    const mx = new THREE.Matrix4();
    for (let i = 0; i < 1400; i++) {
      mx.makeRotationY(Math.random() * 6); mx.setPosition((Math.random() - 0.5) * 2 * HALF, 0, Z0 + 14 - Math.random() * (Math.abs(END) + 10));
      im.setMatrixAt(i, mx);
    }
    s.add(im);
  }

  addForest(s, rand) {
    const geo = meshGeometry(fromSprite(S.PINE.rows, S.PINE.color, 'lathe'), 0.3 * 2.6, 0.22 * 2.6, 0.3 * 2.6);
    const N = 170;
    const im = new THREE.InstancedMesh(geo, voxMat, N);
    im.castShadow = true;
    const mx = new THREE.Matrix4(), q = new THREE.Quaternion(), sc = new THREE.Vector3(), p = new THREE.Vector3();
    for (let i = 0; i < N; i++) {
      const side = i % 2 ? 1 : -1;
      const x = side * (HALF + 5 + rand() * 30), z = Z0 + 30 - rand() * (Math.abs(RIVER) + 40);
      const out = Math.abs(x) - HALF - 2;
      p.set(x, out * 0.35, z); q.setFromAxisAngle(new THREE.Vector3(0, 1, 0), rand() * 6); sc.setScalar(0.8 + rand() * 0.6);
      im.setMatrixAt(i, mx.compose(p, q, sc));
    }
    s.add(im);
  }

  addBridge(s) {
    // river
    const water = new THREE.Mesh(new THREE.PlaneGeometry(170, 12), new THREE.MeshLambertMaterial({ color: 0x2d57b0, emissive: 0x0a1a40 }));
    water.rotation.x = -Math.PI / 2; water.position.set(0, 0.08, RIVER);
    s.add(water); this.water = water;
    // a wooden bridge
    const wood = new THREE.MeshLambertMaterial({ color: 0x866a26 });
    for (let i = 0; i < 9; i++) {
      const plank = new THREE.Mesh(new THREE.BoxGeometry(BRIDGE_W * 2, 0.3, 1.2), wood);
      plank.position.set(0, 0.45 + Math.sin(i / 8 * Math.PI) * 0.6, RIVER + 6 - i * 1.5);
      plank.castShadow = plank.receiveShadow = true; s.add(plank);
    }
    for (const sx of [-1, 1]) for (let i = 0; i < 4; i++) {
      const post = new THREE.Mesh(new THREE.BoxGeometry(0.4, 2, 0.4), wood);
      post.position.set(sx * BRIDGE_W, 1.2, RIVER + 6 - i * 4); post.castShadow = true; s.add(post);
    }
    // the castle gate beyond: the gold castle sprite, big
    const castle = spriteMesh(S.CASTLE.rows, S.CASTLE.color, 'taper', 4.2, { depth: 8 });
    castle.position.set(0, 0, RIVER - 22); s.add(castle);
    for (const sx of [-1, 1]) {
      const t = spriteMesh(S.TEMPLE.rows, '#c66c3a', 'taper', 3.2, { depth: 7 });
      t.position.set(sx * 13, 0, RIVER - 18); s.add(t);
    }
    // the guard
    this.guard = monsterMesh('GUARD', 1.7);
    this.guard.position.set(this.state.guardGone ? -6 : 0, 0, RIVER + 7.5);
    s.add(this.guard);
    if (!this.state.guardGone) this.guardCol = addCollider(0, RIVER + 7.5, 2.4, { kind: 'GUARD', guard: true });
  }

  clamp = (x, z) => {
    x = Math.max(-HALF - 1, Math.min(HALF + 1, x));
    z = Math.min(Z0 + 12, z);
    // the river: only the bridge crosses it
    if (z < RIVER + 6.5 && z > RIVER - 6.5 && Math.abs(x) > BRIDGE_W - 0.8) {
      if (Math.abs(x) < BRIDGE_W + 0.5 && z < RIVER + 6) x = Math.sign(x) * (BRIDGE_W - 0.8);
      else z = RIVER + 6.5;
    }
    return [x, Math.max(RIVER - 30, z)];
  };

  ground = (x, z) => (z < RIVER + 6.5 && z > RIVER - 6.5) ? 0.6 + Math.sin(((RIVER + 6) - z) / 12 * Math.PI) * 0.6 : 0;

  start(from) {
    const h = G.hero;
    h.position.set(0, 0, Z0 + 8);
    if (from === 'resume' && this.state.pos) h.position.set(this.state.pos[0], 0, this.state.pos[1]);
    h.rotation.y = Math.PI;
    G.camOffset.set(0, 12, 14); G.camLook.set(0, 0.8, -2);
  }

  update(dt) {
    const p = G.p;
    let speed = 7;
    // swamps slow you down
    for (const o of this.objects) if (o.kind === 'SWAMP' && Math.hypot(o.x - G.hero.position.x, o.z - G.hero.position.z) < 3.6) speed = 3;
    const bumped = moveHero(dt, { speed, clamp: this.clamp, ground: this.ground });
    this.water.position.y = 0.08 + Math.sin(G.time * 2) * 0.03;
    // guard idles
    const gu = this.guard.userData;
    gu.t += dt;
    this.guard.position.y = Math.abs(Math.sin(gu.t * 2)) * 0.15;
    this.guard.rotation.y = Math.atan2(G.hero.position.x - this.guard.position.x, G.hero.position.z - this.guard.position.z);
    if (G.hero.position.z < RIVER - 12) return { goto: 2 };
    this.state.pos = [G.hero.position.x, G.hero.position.z];
    G.ui.idle(['THE ENCHANTED', 'COUNTRYSIDE', '', 'FIRE = MENU']);
    if (bumped) return { bump: bumped.data };
    return null;
  }

  async interact(o) {
    if (o.guard) return this.guardTalk();
    if (o.searched && o.content.k !== 'church') {
      await G.ui.say([o.name, 'OH NO', 'NOBODY HERE']);
      return;
    }
    const pos = new THREE.Vector3(o.x, 0, o.z);
    const c = o.content;
    const done = () => { o.searched = true; this.state.searched.push(o.id); this.dim(o); save({ area: this.state }); };
    switch (c.k) {
      case 'none': await G.ui.say([o.name, 'OH NO', 'NOBODY HERE']); done(); break;
      case 'gold': G.p.gold += c.n; sfx('coin'); floatText(pos.clone().setY(3), '+' + c.n + ' GOLD', '#fce070'); await G.ui.say([o.name, 'YOU FIND', c.n + ' GOLD']); done(); break;
      case 'item': G.p.inv[c.item]++; sfx('item'); showPickup(c.item, pos); await G.ui.say([o.name, 'YOU FIND', article(c.item)]); done(); break;
      case 'monster': {
        const r = await battle(c.names, pos, { objR: KINDS[o.kind].r, intro: [o.name, 'OH NO', c.names.length > 1 ? c.names[0] + 'S' : article(c.names[0])] });
        if (r === 'won') {
          if (c.then) { G.p.inv[c.then]++; sfx('item'); showPickup(c.then, pos); await G.ui.say(['INSIDE THE ' + o.name, 'YOU FIND', article(c.then)]); }
          done();
        } else if (r === 'fled') this.pushBack(o);
        break;
      }
      case 'locked':
        if (G.p.inv.KEY > 0) {
          G.p.inv.KEY--; sfx('door');
          await G.ui.say([o.name, 'YOU OPEN', 'THE DOOR']);
          G.p.gold += c.gold; G.p.inv[c.item]++; sfx('item'); showPickup(c.item, pos);
          await G.ui.say(['YOU FIND', c.gold + ' GOLD', 'AND ' + article(c.item)]);
          done();
        } else { await G.ui.say([o.name, 'THE DOOR', 'IS LOCKED']); this.pushBack(o); }
        break;
      case 'trap': await this.trap(); done(); break;
      case 'pit': {
        sfx('trap');
        G.hero.position.y = -1.2;
        const d = R.randBelow(4) + 1; G.p.str -= d; hurtFx(d);
        await G.ui.say(['PIT', 'YOU FELL IN', 'YOU TAKE ' + R.pad(d), 'UNITS DAMAGE']);
        G.hero.position.y = 0;
        if (G.p.str < 0) { G.dead = true; await G.ui.say(['YOU ARE DEAD']); }
        done(); break;
      }
      case 'sick': {
        const d = R.randBelow(2) + 1; G.p.str -= d; hurtFx(d);
        await G.ui.say(['SWAMP', 'YOU GET SICK', 'YOU TAKE ' + R.pad(d), 'UNITS DAMAGE']);
        if (G.p.str < 0) { G.dead = true; await G.ui.say(['YOU ARE DEAD']); }
        done(); break;
      }
      case 'church': await this.church(); this.pushBack(o); break;
    }
    G.ui.status(G.p);
  }

  async trap() {
    sfx('trap');
    G.p.stuck = R.randBelow(3) + 2;
    await G.ui.say(['>>TRAP<<', 'YOU ARE CAUGHT']);
    while (G.p.stuck > 0) {
      await G.ui.say(['>>TRAP<<', 'STILL STUCK', '', 'PRESS FIRE']);
      G.p.stuck--;
      G.hero.userData.swing = 1;
    }
    await G.ui.say(['YOU ESCAPE']);
  }

  async church() {
    await G.ui.say(['YOU ARE', 'IN CHURCH']);
    for (;;) {
      const c = await G.ui.dir4({ up: 'PRAY', right: 'DONATE 15', left: 'DONATE 200', down: 'LEAVE' }, 'CHURCH');
      if (!c || c === 'down') return;
      const msg = R.church(G.p, { up: 0, right: 1, left: 2 }[c]);
      if (msg[0] === 'YOU GAIN') { sfx('magic'); G.fx.burst(G.hero.position.clone().setY(2), ['#fff', '#fce070'], 30, { speed: 2, up: 7, gravity: 3, size: 0.12 }); }
      G.ui.status(G.p);
      await G.ui.say(msg);
    }
  }

  async guardTalk() {
    this.pushBack({ x: 0, z: RIVER + 7.5 });
    await G.ui.say(['BRIDGE', 'THE GUARD', 'WANTS ID']);
    const c = await G.ui.dir4({ up: 'LEAVE', right: 'FIGHT', left: 'USE', down: 'PAY 250' }, 'THE GUARD');
    if (c === 'left') { const { useMenu } = await import('./play.js'); await useMenu('field'); return; }
    if (c === 'down') {
      if (G.p.gold >= 250) { G.p.gold -= 250; sfx('coin'); await G.ui.say(['THE GUARD', 'TAKES 250 GOLD']); return this.letPass(); }
      await G.ui.say(['YOU CANNOT', 'DO THAT']); return;
    }
    if (c === 'right') {
      this.guard.visible = false;
      const r = await battle(['GUARD'], this.guard.position.clone(), { intro: ['THE GUARD', 'FIGHTS'], scale: 1.7, objR: 0, dist: 6 });
      if (r === 'won') { this.state.guardGone = true; removeCollider(this.guardCol); this.guardCol = null; await G.ui.say(['THE BRIDGE', 'IS YOURS']); save({ area: this.state }); }
      else this.guard.visible = true;
    }
  }

  async onPaper() {
    if (!this.guardCol || Math.hypot(G.hero.position.x, G.hero.position.z - (RIVER + 7.5)) > 9) {
      await G.ui.say(['IT IS AN ID', 'SHOW IT TO', 'THE GUARD']);
      return { turn: false };
    }
    G.p.inv.PAPER--;
    await G.ui.say(['THE GUARD', 'READS YOUR ID']);
    await this.letPass();
    return { turn: false };
  }

  async letPass() {
    sfx('part');
    this.state.guardGone = true;
    removeCollider(this.guardCol); this.guardCol = null;
    const t0 = G.time;
    G.anims.push(() => { const k = Math.min(1, (G.time - t0) / 1.2); this.guard.position.x = -6 * k; return k < 1; });
    await G.ui.say(['YOU MAY CROSS', 'THE BRIDGE']);
    save({ area: this.state });
  }

  onFight = async () => {
    const near = this.objects.find(o => Math.hypot(o.x - G.hero.position.x, o.z - G.hero.position.z) < KINDS[o.kind].r + 2.2);
    if (near && !near.searched) return this.interact(near);
    await G.ui.say(['OH NO', 'NOBODY HERE']);
  };

  pushBack(o) {
    const h = G.hero.position, dx = h.x - o.x, dz = h.z - o.z, d = Math.hypot(dx, dz) || 1;
    h.x += dx / d * 1.5; h.z += dz / d * 1.5;
  }

  dim(o) {
    // searched scenery fades a little, so you can tell where you have been
    o.mesh.traverse(m => { if (m.isMesh && m.material === voxMat) { m.material = searchedMat; } });
  }
  saveState() { return this.state; }
}
const searchedMat = new THREE.MeshLambertMaterial({ vertexColors: true, color: 0x9a9a9a });
searchedMat.userData.keep = true;
