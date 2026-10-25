// Part 1: the Enchanted Countryside. A long valley, laid out in rows like the original's scrolling
// strip, where every tree, hut, temple, rock, lake and swamp may hide a monster, an item, gold,
// a trap or a pit. A river and a bridge guard who wants your ID wait at the far end.
import * as THREE from 'three';
import { loadHDR, addSun, gfx } from '../core/gfx.js';
import { Terrain } from '../world/terrain.js';
import { Grass } from '../world/grass.js';
import { forest } from '../world/trees.js';
import { water } from '../world/water.js';
import { hut, temple, church, gatehouse, bridge, place } from '../world/buildings.js';
import { prop } from '../core/assets.js';
import { fbm } from '../core/texgen.js';
import { mulberry } from '../util.js';
import { sfx } from '../core/sound.js';
import { floatText, shake } from '../game/fx.js';
import * as R from '../rules.js';

const LEN = 700, HALF = 46, RIVER_Z = -LEN - 25, BRIDGE_HALF = 2.2;
const pathX = (z) => 9 * Math.sin(z * 0.011) + 5 * Math.sin(z * 0.029 + 1);
const TIERS = [
  ['MONKEY', 'BUG', 'BEETLE', 'DEMON', 'SPIDER', 'MONKEY'],
  ['SNAKE', 'SLIME', 'GOLEM', 'GHOUL', 'MANIAC', 'BEETLE'],
  ['WARRIOR', 'SCORPION', 'GHOUL', 'SNAKE', 'MANIAC', 'SLIME'],
];
const LOOT = ['POTION', 'POTION', 'KEY', 'KEY', 'KEY', 'CHEST', 'CHEST', 'CROSS', 'RING', 'STAFF', 'CHARM', 'SHIELD', 'HANDAXE', 'PAPER'];
const LABEL = { tree: 'Search the tree', hut: 'Search the hut', temple: 'Search the temple', rock: 'Search the rocks', lake: 'Search the lake shore', swamp: 'Search the swamp', grass: 'Search the tall grass', church: 'Enter the church' };
const NAME = { tree: 'TREE', hut: 'HUT', temple: 'TEMPLE', rock: 'ROCK', lake: 'LAKE', swamp: 'SWAMP', grass: 'GRASS', church: 'CHURCH' };

export class Countryside {
  constructor(game, state) {
    this.game = game;
    this.state = { searched: [], guardGone: false, ...state };
    this.colliders = []; this.interactables = [];
    this.ambience = 'meadow'; this.music = 'countryside'; this.floor = 'grass';
    this.updaters = [];
  }

  async build(scene) {
    const g = this.game;
    const sky = await loadHDR('assets/hdri/kloofendal_48d_partly_cloudy_puresky_2k.hdr');
    scene.background = sky.background; scene.environment = sky.env;
    scene.environmentIntensity = 0.75; scene.backgroundIntensity = 1;
    scene.fog = new THREE.FogExp2(0xb4c4d4, 0.0024);
    addSun(scene, { dir: new THREE.Vector3(0.554, 0.743, 0.375) }); // where the sun is in the HDR sky
    scene.userData.godRays = 0.55;
    const rand = mulberry(g.stats.seed);

    // ----- layout first: the terrain dips for lakes and swamps -----
    const objs = [];
    let id = 0, churches = 0, maniacs = 0;
    const kinds = { tree: 26, rock: 10, hut: 8, temple: 5, lake: 4, swamp: 4, grass: 9 };
    const pickKind = () => { let t = Object.values(kinds).reduce((a, b) => a + b) * rand(); for (const [k, w] of Object.entries(kinds)) { t -= w; if (t <= 0) return k; } return 'tree'; };
    const rows = 34;
    for (let r = 0; r < rows; r++) {
      const z = -30 - r * (LEN - 60) / rows + (rand() - 0.5) * 6;
      const t = r / rows;
      let kind = pickKind();
      if ((r === 7 || r === 18 || r === 29) && churches < 3) { kind = 'church'; churches++; }
      const n = kind === 'church' || kind === 'lake' || kind === 'temple' ? 1 : [1, 2, 2, 3][Math.floor(rand() * 4)];
      for (let i = 0; i < n; i++) {
        const side = n === 1 ? (rand() < 0.5 ? -1 : 1) : (i % 2 ? 1 : -1);
        const x = pathX(z) + side * (9 + rand() * 22) + (n === 3 && i === 2 ? 0 : 0);
        const o = { id: id++, kind, x: THREE.MathUtils.clamp(x, -HALF + 6, HALF - 6), z: z + (rand() - 0.5) * 8, t, rot: rand() * 6.28 };
        o.content = this.roll(o, rand, t);
        if (o.content.k === 'monster' && o.content.names[0] === 'MANIAC') maniacs++;
        o.searched = this.state.searched.includes(o.id);
        objs.push(o);
      }
    }
    // make sure an ID can be had: maniacs always carry the PAPER
    if (maniacs < 2) {
      const mid = objs.filter(o => o.t > 0.25 && ['tree', 'hut', 'rock', 'grass'].includes(o.kind) && o.content.k !== 'church');
      for (let i = 0; i < 2 - maniacs && mid.length; i++) mid.splice(Math.floor(rand() * mid.length), 1)[0].content = { k: 'monster', names: ['MANIAC', 'MANIAC', 'MANIAC'] };
    }
    this.objs = objs;
    const dips = objs.filter(o => o.kind === 'lake' || o.kind === 'swamp').map(o => ({ x: o.x, z: o.z, r: o.kind === 'lake' ? 11 : 9, d: o.kind === 'lake' ? 2.2 : 0.7 }));
    this.dips = dips;

    // ----- terrain -----
    const height = (x, z) => {
      const px = pathX(z);
      let h = fbm(x * 0.012, z * 0.012, 0, 4) * 6 + fbm(x * 0.05, z * 0.05, 3, 3) * 0.8;
      const side = Math.max(0, Math.abs(x) - HALF);
      h += Math.min(side, 70) ** 2 * 0.009 + side * 0.3 + THREE.MathUtils.clamp(side - 10, 0, 50) * fbm(x * 0.02, z * 0.02, 7, 3) * 1.6;
      // crags and ledges on the valley walls
      if (side > 2) h += Math.min(side - 2, 40) * 0.22 * fbm(x * 0.04, z * 0.04, 11, 4);
      // flatten the road
      const pd = Math.abs(x - px);
      h = THREE.MathUtils.lerp(h, h * 0.3, Math.max(0, 1 - pd / 7));
      for (const d of dips) { const dd = Math.hypot(x - d.x, z - d.z); if (dd < d.r * 1.6) h -= d.d * Math.pow(Math.max(0, 1 - dd / (d.r * 1.6)), 1.5) * 1.6; }
      // the river valley
      const rz = Math.abs(z - RIVER_Z);
      if (rz < 26) h -= Math.pow(1 - rz / 26, 2) * 4.5;
      if (z < RIVER_Z - 6) h = Math.max(h, -0.5 + (RIVER_Z - 6 - z) * 0.02);
      return h;
    };
    this.heightFn = height;
    const path = (x, z) => { const d = Math.abs(x - pathX(z)); return z < RIVER_Z + 18 && z > RIVER_Z - 30 ? Math.max(0, 1 - Math.abs(x) / 4) : Math.max(0, 1 - d / 3.2); };
    const grassy = (x, z) => { let k = 1; for (const d of dips) { const dd = Math.hypot(x - d.x, z - d.z); if (dd < d.r) k = 0; } if (Math.abs(z - RIVER_Z) < 9) k = 0; return k; };
    this.terrain = new Terrain({ x0: -260, x1: 260, z0: RIVER_Z - 120, z1: 60, res: 2.5, heightFn: height, pathFn: path, grassFn: grassy });
    scene.add(this.terrain.mesh);
    this.grass = new Grass(this.terrain);
    scene.add(this.grass.mesh);

    // ----- the searchable scenery -----
    const treeList = [];
    const pending = [];
    for (const o of objs) {
      const y = height(o.x, o.z);
      const inter = { pos: new THREE.Vector3(o.x, y, o.z), radius: 4, label: o.searched && o.kind !== 'church' ? null : LABEL[o.kind], obj: o, action: () => this.search(o) };
      if (o.kind === 'tree') {
        treeList.push({ x: o.x, z: o.z, y, type: ['oak', 'oakBig', 'ash', 'pine', 'aspen'][Math.floor(rand() * 5)], scale: 0.85 + rand() * 0.4 });
        this.colliders.push({ type: 'circle', x: o.x, z: o.z, r: 0.7 });
        inter.radius = 3.4;
      } else if (o.kind === 'hut') {
        const b = hut({ r: 3 + rand(), h: 2.4 });
        this.colliders.push(...place(scene, b, o.x, y, o.z, o.rot));
        inter.pos.copy(b.doorWorld); inter.radius = 2.4;
      } else if (o.kind === 'temple') {
        const b = temple({}, rand);
        this.colliders.push(...place(scene, b, o.x, y - 0.3, o.z, o.rot));
        inter.pos.copy(b.doorWorld); inter.radius = 3;
      } else if (o.kind === 'church') {
        const b = church();
        const face = Math.atan2(pathX(o.z) - o.x, 0) ;
        this.colliders.push(...place(scene, b, o.x, y, o.z, Math.sign(pathX(o.z) - o.x) * Math.PI / 2));
        inter.pos.copy(b.doorWorld); inter.radius = 3;
      } else if (o.kind === 'rock') {
        pending.push(prop(rand() < 0.5 ? 'namaqualand_boulder_02' : 'rock_moss_set_01', 3 + rand() * 2.5).then(m => { m.position.set(o.x, y - 0.2, o.z); m.rotation.y = o.rot; scene.add(m); }));
        this.colliders.push({ type: 'circle', x: o.x, z: o.z, r: 1.6 });
        inter.radius = 3.6;
      } else if (o.kind === 'lake' || o.kind === 'swamp') {
        const w = water(o.kind === 'lake' ? 24 : 18, o.kind === 'lake' ? 24 : 18, o.kind === 'swamp' ? { color: 0x1a2412, opacity: 0.96 } : {});
        w.geometry = new THREE.CircleGeometry(o.kind === 'lake' ? 12 : 9.5, 48).rotateX(-Math.PI / 2);
        w.position.set(o.x, height(o.x + (o.kind === 'lake' ? 12 : 9.5), o.z) - (o.kind === 'lake' ? 0.25 : 0.05), o.z);
        scene.add(w); this.updaters.push(w.userData.update);
        if (o.kind === 'swamp') {
          for (let k = 0; k < 3; k++) treeList.push({ x: o.x + (rand() - 0.5) * 12, z: o.z + (rand() - 0.5) * 12, y: y - 0.5, type: 'dead', scale: 0.7 + rand() * 0.4 });
          for (let k = 0; k < 8; k++) pending.push(prop('fern_02', 1.2 + rand()).then(m => { const a = rand() * 6.28, r2 = 8 + rand() * 3; m.position.set(o.x + Math.cos(a) * r2, height(o.x + Math.cos(a) * r2, o.z + Math.sin(a) * r2) - 0.1, o.z + Math.sin(a) * r2); scene.add(m); }));
        } else this.colliders.push({ type: 'circle', x: o.x, z: o.z, r: 9.5, noCam: true });
        inter.pos.set(o.x + (pathX(o.z) > o.x ? 10.5 : -10.5), y, o.z); inter.radius = 4;
        if (o.kind === 'swamp') { inter.pos.set(o.x, y, o.z); inter.radius = 6; }
      } else if (o.kind === 'grass') {
        for (let k = 0; k < 7; k++) pending.push(prop('fern_02', 1.4 + rand() * 0.8).then(m => { const a = rand() * 6.28, r2 = rand() * 2.5; m.position.set(o.x + Math.cos(a) * r2, height(o.x, o.z) - 0.1, o.z + Math.sin(a) * r2); m.rotation.y = rand() * 6; scene.add(m); }));
        inter.radius = 3;
      }
      o.inter = inter;
      this.interactables.push(inter);
    }
    // the forests on the valley sides, and scattered trees and bushes in the meadows
    for (let i = 0; i < 520; i++) {
      const side = i % 2 ? 1 : -1;
      const x = side * (HALF - 4 + Math.pow(rand(), 0.7) * 120), z = 40 - rand() * (LEN + 140);
      if (Math.abs(z - RIVER_Z) < 14) continue;
      treeList.push({ x, z, y: height(x, z), type: rand() < 0.55 ? (rand() < 0.5 ? 'pine' : 'pineBig') : rand() < 0.5 ? 'oak' : 'ash', scale: 0.8 + rand() * 0.5 });
    }
    for (let i = 0; i < 140; i++) {
      const x = (rand() - 0.5) * HALF * 2, z = -rand() * LEN;
      if (Math.abs(x - pathX(z)) < 5 || objs.some(o => Math.hypot(o.x - x, o.z - z) < 7) || dips.some(d => Math.hypot(d.x - x, d.z - z) < d.r + 2)) continue;
      treeList.push({ x, z, y: height(x, z), type: rand() < 0.75 ? 'bush' : 'aspen', scale: 0.7 + rand() * 0.6 });
      if (treeList[treeList.length - 1].type === 'aspen') this.colliders.push({ type: 'circle', x, z, r: 0.5 });
    }
    scene.add(forest(treeList, { seedBase: g.stats.seed & 255 }));
    // rocks and stumps for detail
    for (let i = 0; i < 40; i++) {
      const x = (rand() - 0.5) * HALF * 2.2, z = -rand() * LEN;
      if (Math.abs(x - pathX(z)) < 4) continue;
      pending.push(prop(['tree_stump_01', 'rock_moss_set_01', 'dead_tree_trunk'][i % 3], i % 3 === 2 ? 5 : 1.2 + rand()).then(m => { m.position.set(x, height(x, z) - 0.05, z); m.rotation.y = rand() * 6; scene.add(m); }));
    }

    // boulders strewn along the foot of the valley walls
    for (let i = 0; i < 70; i++) {
      const side = i % 2 ? 1 : -1, x = side * (HALF - 2 + rand() * 26), z = 30 - rand() * (LEN + 60);
      if (Math.abs(z - RIVER_Z) < 16) continue;
      pending.push(prop(rand() < 0.6 ? 'namaqualand_boulder_02' : 'rock_moss_set_01', 2.5 + rand() * 6).then(m => { m.position.set(x, height(x, z) - 0.4, z); m.rotation.y = rand() * 6; scene.add(m); }));
    }

    // ----- the river, the bridge, the guard and the castle -----
    const river = water(600, 16, { color: 0x14302c });
    river.position.set(0, -2.2, RIVER_Z); scene.add(river); this.updaters.push(river.userData.update);
    const br = bridge({ len: 22, w: 4.4, rise: 1.2 });
    br.group.position.set(0, -1.6, RIVER_Z); scene.add(br.group);
    this.bridgeDeck = (z) => -1.6 + br.deck(z - RIVER_Z);
    const gh = gatehouse({ span: 7 });
    this.colliders.push(...place(scene, gh, 0, height(0, RIVER_Z - 34), RIVER_Z - 34, 0));
    const portcullis = await prop('large_iron_gate', 6.5).catch(() => null);
    if (portcullis) { portcullis.position.set(0, height(0, RIVER_Z - 34) - 0.1, RIVER_Z - 33.8); scene.add(portcullis); this.portcullis = portcullis; }
    this.gateZ = RIVER_Z - 34;
    // the guard waits at the near end of the bridge
    if (!this.state.guardGone) {
      const [guardM] = g.spawn(['GUARD'], new THREE.Vector3(0, 0, RIVER_Z + 13), { emerge: false, aggro: false });
      guardM.yaw = 0; guardM.passive = true; this.guard = guardM;
      this.guardInter = { pos: new THREE.Vector3(0, 0, RIVER_Z + 13), radius: 4.2, label: 'Speak to the guard', action: () => this.guardTalk() };
      this.interactables.push(this.guardInter);
    }
    await Promise.all(pending);
  }

  roll(o, rand, t) {
    const tier = TIERS[Math.min(2, Math.floor(t * 3))];
    const monster = () => { const n = tier[Math.floor(rand() * tier.length)]; return { k: 'monster', names: R.MONSTERS[n].group ? [n, n, n] : [n] }; };
    const item = () => ({ k: 'item', item: LOOT[Math.floor(rand() * LOOT.length)] });
    const gold = () => ({ k: 'gold', n: 10 + Math.floor(rand() * (20 + 60 * t)) });
    const r = rand();
    switch (o.kind) {
      case 'church': return { k: 'church' };
      case 'hut': case 'temple':
        if (r < 0.35) return { ...monster(), then: item().item };
        if (r < 0.55) return { k: 'locked', item: item().item, gold: 50 + Math.floor(rand() * 80) };
        if (r < 0.8) return item();
        return gold();
      case 'rock':
        if (r < 0.35) return { k: 'trap' };
        if (r < 0.6) return { k: 'pit' };
        if (r < 0.8) return gold();
        return { k: 'none' };
      case 'swamp': return r < 0.5 ? { k: 'sick' } : r < 0.75 ? { k: 'monster', names: ['SLIME'] } : { k: 'none' };
      case 'lake': return r < 0.4 ? { k: 'monster', names: ['SNAKE'] } : r < 0.7 ? item() : { k: 'none' };
      case 'grass': return r < 0.5 ? { k: 'monster', names: [rand() < 0.5 ? 'SNAKE' : 'SCORPION'] } : r < 0.65 ? gold() : { k: 'none' };
      default:
        if (r < 0.42) return monster();
        if (r < 0.58) return item();
        if (r < 0.74) return gold();
        return { k: 'none' };
    }
  }

  start(from) {
    const pl = this.game.player;
    pl.pos.set(pathX(10), 0, 10);
    pl.yaw = Math.PI;
    if (from === 'resume' && this.state.pos) pl.pos.set(this.state.pos[0], 0, this.state.pos[1]);
    pl.pos.y = this.groundAt(pl.pos.x, pl.pos.z);
  }
  floorAt = (x, z) => Math.abs(z - RIVER_Z) < 11 && Math.abs(x) < BRIDGE_HALF + 0.2 ? 'wood' : 'grass';
  groundAt = (x, z) => {
    if (Math.abs(z - RIVER_Z) < 11 && Math.abs(x) < BRIDGE_HALF + 0.2) return this.bridgeDeck(z);
    return this.heightFn(x, z);
  };
  clamp = (x, z) => {
    x = THREE.MathUtils.clamp(x, -HALF - 30, HALF + 30);
    z = Math.min(40, z);
    // the river: only the bridge crosses it
    if (Math.abs(z - RIVER_Z) < 9.5 && Math.abs(x) > BRIDGE_HALF) {
      if (Math.abs(x) < BRIDGE_HALF + 0.8 && Math.abs(z - RIVER_Z) < 9) x = Math.sign(x) * BRIDGE_HALF;
      else z = z > RIVER_Z ? RIVER_Z + 9.5 : RIVER_Z - 9.5;
    }
    // the guard bars the bridge until he lets you by
    if (!this.state.guardGone && z < RIVER_Z + 11 && z > RIVER_Z - 12) z = RIVER_Z + 11;
    // the castle wall, open only at the gate once the guard has gone
    if (z < this.gateZ + 1.5 && Math.abs(x) > 2.8) z = this.gateZ + 1.5;
    return [x, z];
  };

  update(dt) {
    const g = this.game, pl = g.player;
    this.grass.update(dt, gfx.camera.position, pl.pos);
    for (const u of this.updaters) u(dt);
    this.state.pos = [pl.pos.x, pl.pos.z];
    // swamps drag at your feet
    for (const d of this.dips) if (d.d < 1 && Math.hypot(pl.pos.x - d.x, pl.pos.z - d.z) < d.r) { pl.vel.multiplyScalar(0.92); }
    if (this.guard && !this.guard.alive && !this.state.guardGone) { this.state.guardGone = true; g.hud.say(['THE BRIDGE', 'IS YOURS']); }
    // through the gate: on to the village
    if (pl.pos.z < this.gateZ - 2 && !this.leaving) { this.leaving = true; g.onAreaExit?.(2); }
    if (this.portcullis && this.state.guardGone) this.portcullis.position.y = Math.min(this.heightFn(0, this.gateZ) + 5.5, this.portcullis.position.y + dt * 1.5);
  }

  // ----- searching -----
  async search(o) {
    const g = this.game, p = g.stats, hud = g.hud;
    const name = NAME[o.kind];
    if (o.searched && o.kind !== 'church') { hud.say([name, 'OH NO', 'NOBODY HERE']); return; }
    const done = () => { o.searched = true; this.state.searched.push(o.id); o.inter.label = null; o.inter.off = true; };
    const c = o.content;
    const at = o.inter.pos.clone();
    switch (c.k) {
      case 'none': hud.say([name, 'OH NO', 'NOBODY HERE']); done(); break;
      case 'gold': g.drop('GOLD', at.clone().add(new THREE.Vector3(0.6, 0, 0.6)), c.n); hud.say([name, 'SOMETHING GLINTS']); done(); break;
      case 'item': g.drop(c.item, at.clone().add(new THREE.Vector3(0.6, 0, 0.6))); hud.say([name, 'SOMETHING IS HERE']); done(); break;
      case 'monster': {
        const plural = c.names.length > 1 ? c.names[0] + (c.names[0].endsWith('Y') && !c.names[0].endsWith('EY') ? '' : 'S') : (/^[AEIOU]/.test(c.names[0]) ? 'AN ' : 'A ') + c.names[0];
        hud.say([name, 'OH NO', plural], 3.5);
        const toPl = g.player.pos.clone().sub(at).setY(0).normalize();
        const spawnAt = new THREE.Vector3(o.x, 0, o.z).addScaledVector(toPl, o.kind === 'tree' ? 2 : o.kind === 'lake' ? 11 : 3.5);
        const ms = g.spawn(c.names, spawnAt);
        if (c.then) ms[ms.length - 1].carry = c.then;
        shake.add(0.3);
        done();
        break;
      }
      case 'locked':
        if (p.inv.KEY > 0) {
          p.inv.KEY--; sfx('door');
          hud.say([name, 'YOU OPEN THE DOOR']);
          g.drop('GOLD', at.clone().add(new THREE.Vector3(0.6, 0, 0.8)), c.gold);
          g.drop(c.item, at.clone().add(new THREE.Vector3(-0.6, 0, 0.8)));
          done();
        } else { sfx('bad'); hud.say([name, 'THE DOOR IS LOCKED']); }
        break;
      case 'trap':
        sfx('trap'); shake.add(0.4);
        p.stuck = R.randBelow(3) + 3;
        hud.say(['>>TRAP<<', 'YOU ARE CAUGHT', 'PRESS SPACE TO STRUGGLE'], 4);
        done(); break;
      case 'pit': {
        sfx('rock'); shake.add(0.8);
        const d = R.randBelow(4) + 1; p.str -= d;
        g.playerHurt(d);
        hud.say(['PIT', 'YOU FELL IN', 'YOU TAKE ' + R.pad(d) + ' UNITS DAMAGE'], 3.5);
        done(); break;
      }
      case 'sick': {
        const d = R.randBelow(2) + 1; p.str -= d;
        g.playerHurt(d);
        hud.say(['SWAMP', 'YOU GET SICK', 'YOU TAKE ' + R.pad(d) + ' UNITS DAMAGE'], 3.5);
        done(); break;
      }
      case 'church': await this.church(); break;
    }
  }
  onKill(m) {
    if (m.carry) { this.game.drop(m.carry, m.pos.clone().add(new THREE.Vector3(0, 0, 0.8))); m.carry = null; }
  }

  async church() {
    const g = this.game, p = g.stats;
    sfx('bell', { vol: 0.5 });
    for (;;) {
      const v = await g.hud.dialog({
        title: 'The Church', text: 'Candles flicker before a worn stone altar. A collection box waits by the door.',
        options: [{ label: 'Pray', value: 0 }, { label: 'Donate 15 gold', sub: 'Perhaps a point of strength', value: 1, off: p.gold < 15 }, { label: 'Donate 200 gold', sub: 'Strength restored in full', value: 2, off: p.gold < 200 }, { label: 'Leave', value: null }],
      });
      if (v == null) return;
      const msg = R.church(p, v);
      if (msg[0] === 'YOU GAIN') { sfx('heal'); g.fx.burst(g.player.pos.clone().setY(g.player.pos.y + 1), 'heal', 40); }
      g.hud.say(msg);
    }
  }

  async guardTalk() {
    const g = this.game, p = g.stats;
    const v = await g.hud.dialog({
      title: 'The Bridge Guard', text: 'A giant in gilded plate bars the bridge, halberd lowered. “THE GUARD WANTS ID.”',
      options: [
        { label: 'Show your ID', sub: p.inv.PAPER ? 'The PAPER you carry' : 'You have no ID', value: 'id', off: !p.inv.PAPER },
        { label: 'Pay 250 gold', value: 'pay', off: p.gold < 250 },
        { label: 'Fight him', sub: 'He has 112 strength', value: 'fight' },
        { label: 'Leave', value: null },
      ],
    });
    if (v === 'id') { p.inv.PAPER--; g.hud.say(['THE GUARD', 'READS YOUR ID']); return this.letPass(); }
    if (v === 'pay') { p.gold -= 250; sfx('coin'); g.hud.say(['THE GUARD', 'TAKES 250 GOLD']); return this.letPass(); }
    if (v === 'fight') { this.guard.passive = false; this.guard.aggro = true; this.guardInter.off = true; g.hud.say(['THE GUARD', 'FIGHTS']); sfx('growl', { pitch: 1.2 }); }
  }
  async onPaper() {
    if (!this.guard || this.state.guardGone || this.guard.pos.distanceTo(this.game.player.pos) > 10) return false;
    this.game.stats.inv.PAPER--;
    this.game.hud.say(['THE GUARD', 'READS YOUR ID']);
    await this.letPass();
    return true;
  }
  async letPass() {
    const g = this.game;
    this.state.guardGone = true;
    this.guardInter.off = true;
    sfx('chain');
    g.hud.say(['YOU MAY CROSS', 'THE BRIDGE'], 3.5);
    // he steps aside and stays out of the fight
    const m = this.guard;
    m.passive = true; m.aggro = false;
    m.pos.set(-3.4, this.groundAt(-3.4, RIVER_Z + 13), RIVER_Z + 13.5); m.yaw = Math.PI / 2;
    m.place();
  }
  dispose() {}
}
