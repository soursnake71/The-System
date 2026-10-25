// Part 2: the village beyond the bridge. A walled square with the Trade Shop, the Magic Shop
// and the Hospital, warriors for hire, and the north gate with its sign:
// "ABANDON ALL HOPE YE WHO ENTER HERE! ... BEWARE OF THE DRAGON!"
import * as THREE from 'three';
import { loadHDR, addSun, gfx } from '../core/gfx.js';
import { Terrain } from '../world/terrain.js';
import { Grass } from '../world/grass.js';
import { forest } from '../world/trees.js';
import { house, church, gatehouse, palisade, place, box, M, iron } from '../world/buildings.js';
import { prop } from '../core/assets.js';
import { fbm, signTexture } from '../core/texgen.js';
import { knight } from '../actors/humanoid.js';
import { mulberry } from '../util.js';
import { sfx } from '../core/sound.js';
import * as R from '../rules.js';

const RAD = 46;
const ONE_OFF = ['LANTERN', 'CHAIN', 'LONGBOW', 'SAPPHIRE', 'RUBY'];

export class Village {
  constructor(game, state) {
    this.game = game;
    this.state = { hired: [], ...state };
    this.colliders = []; this.interactables = [];
    this.ambience = 'village'; this.music = 'village'; this.floor = 'stone';
  }
  async build(scene) {
    const g = this.game;
    // the village at golden hour
    const sky = await loadHDR('assets/hdri/qwantani_sunset_puresky_2k.hdr');
    scene.background = sky.background; scene.environment = sky.env; scene.environmentIntensity = 0.75;
    scene.fog = new THREE.FogExp2(0x9a8070, 0.0022); scene.backgroundIntensity = 0.7;
    addSun(scene, { dir: new THREE.Vector3(0.806, 0.26, 0.582), intensity: 2.2, color: 0xffb57a }); // low, along the HDR sun's bearing
    scene.userData.godRays = 0.45; scene.userData.sunTint = 0xffc890;
    const rand = mulberry(g.stats.seed ^ 0x2222);
    const height = (x, z) => {
      const r = Math.hypot(x, z);
      let h = fbm(x * 0.015, z * 0.015, 5, 3) * 3;
      h *= THREE.MathUtils.smoothstep(r, RAD * 0.8, RAD * 1.3);
      return h + Math.max(0, r - RAD - 15) * 0.15;
    };
    this.heightFn = height;
    const streets = (x, z) => Math.max(Math.max(0, 1 - Math.abs(x) / 3) * (Math.abs(z) < RAD ? 1 : 0), Math.hypot(x, z) < 20 ? 0.6 : 0);
    this.terrain = new Terrain({ x0: -170, x1: 170, z0: -170, z1: 170, res: 2, heightFn: height, pathFn: streets, grassFn: (x, z) => Math.hypot(x, z) < 21 ? 0 : 1 });
    scene.add(this.terrain.mesh);
    this.grass = new Grass(this.terrain); scene.add(this.grass.mesh);
    // the cobbled square
    const cob = new THREE.Mesh(new THREE.CircleGeometry(19, 64).rotateX(-Math.PI / 2), M.cobble());
    const uv = cob.geometry.attributes.uv; for (let i = 0; i < uv.count; i++) uv.setXY(i, (uv.getX(i) - 0.5) * 38, (uv.getY(i) - 0.5) * 38);
    cob.position.y = 0.04; cob.receiveShadow = true; scene.add(cob);
    // a well in the middle
    const well = new THREE.Group();
    const ring = new THREE.Mesh(new THREE.CylinderGeometry(1.3, 1.4, 0.9, 32, 1, true), M.stone()); ring.position.y = 0.45; ring.castShadow = ring.receiveShadow = true; well.add(ring);
    const water = new THREE.Mesh(new THREE.CircleGeometry(1.25, 32).rotateX(-Math.PI / 2), new THREE.MeshPhysicalMaterial({ color: 0x0a1416, roughness: 0.05 })); water.position.y = 0.3; well.add(water);
    for (const s of [-1, 1]) well.add(box(0.18, 2.4, 0.18, M.timber(), s * 1.2, 0, 0));
    well.add(box(2.7, 0.16, 0.16, M.timber(), 0, 2.35, 0));
    const roof = new THREE.Mesh(new THREE.ConeGeometry(1.9, 1.1, 4), M.thatch()); roof.rotation.y = Math.PI / 4; roof.position.y = 3; roof.castShadow = true; well.add(roof);
    scene.add(well); this.colliders.push({ type: 'circle', x: 0, z: 0, r: 1.5 });
    // buildings ringing the square, doors facing the well
    const ringAt = (ang, dist) => [Math.sin(ang) * dist, Math.cos(ang) * dist, ang + Math.PI];
    const shops = [
      { key: 'trade', ang: -1.1, b: house({ w: 9, d: 7, storeys: 2, roof: 'thatch', sign: 'TRADE SHOP' }), label: 'Enter the Trade Shop' },
      { key: 'magic', ang: 0.15 + Math.PI, b: house({ w: 8, d: 7, storeys: 2, roof: 'slate', sign: 'MAGIC SHOP' }), label: 'Enter the Magic Shop' },
      { key: 'hospital', ang: 1.15, b: church({ w: 7, d: 10, h: 5 }), label: 'Enter the Hospital' },
    ];
    for (const s of shops) {
      const [x, z, ry] = ringAt(s.ang, s.key === 'hospital' ? 30 : 27);
      this.colliders.push(...place(scene, s.b, x, height(x, z), z, ry));
      this.interactables.push({ pos: s.b.doorWorld, radius: 2.8, label: s.label, action: () => this[s.key]() });
    }
    // a hospital sign over the door
    const hs = shops[2].b;
    const sign = new THREE.Mesh(new THREE.PlaneGeometry(2.4, 0.7), new THREE.MeshStandardMaterial({ map: signTexture('HOSPITAL', { bg: '#ece4d4', ink: '#a01010' }), roughness: 0.6 }));
    sign.position.copy(hs.doorWorld).setY(height(hs.doorWorld.x, hs.doorWorld.z) + 4); sign.lookAt(0, sign.position.y, 0); scene.add(sign);
    // ordinary houses
    for (const [ang, w, st] of [[-0.45, 7, 1], [0.55, 8, 2], [2.0, 7, 1], [2.6, 9, 2], [-2.1, 8, 1], [-2.7, 7, 2]]) {
      const [x, z, ry] = ringAt(ang, 28 + rand() * 4);
      this.colliders.push(...place(scene, house({ w, d: 6, storeys: st, roof: rand() < 0.5 ? 'thatch' : 'slate', chimney: rand() < 0.7 }), x, height(x, z), z, ry));
    }
    // market clutter
    const pend = [];
    for (let i = 0; i < 14; i++) {
      const a = rand() * Math.PI * 2, d = 12 + rand() * 6;
      const x = Math.sin(a) * d, z = Math.cos(a) * d;
      if (Math.abs(x) < 4) continue;
      const kind = ['wine_barrel_01', 'wooden_crate_01', 'wine_barrel_01'][i % 3];
      pend.push(prop(kind, 0.8 + rand() * 0.3).then(m => { m.position.set(x, 0.04, z); m.rotation.y = rand() * 6; scene.add(m); }));
      this.colliders.push({ type: 'circle', x, z, r: 0.5 });
    }
    for (const [x, z] of [[4, 14], [-4, 14], [4, -16], [-4, -16]]) {
      const post = box(0.16, 2.6, 0.16, M.timber(), x, 0, z); scene.add(post);
      pend.push(prop('wooden_lantern_01', 0.35).then(m => { m.position.set(x, 2.6, z); scene.add(m); }));
    }
    // the palisade, open north and south
    const pts = [];
    for (let a = 0.12; a <= Math.PI - 0.12 + 1e-6; a += 0.06) pts.push({ x: Math.sin(a) * RAD, z: Math.cos(a) * RAD, y: height(Math.sin(a) * RAD, Math.cos(a) * RAD) });
    scene.add(palisade(pts));
    scene.add(palisade(pts.map(p => ({ ...p, x: -p.x }))));
    for (let a = 0.12; a <= Math.PI - 0.12; a += 0.06) for (const s of [-1, 1]) this.colliders.push({ type: 'circle', x: s * Math.sin(a) * RAD, z: Math.cos(a) * RAD, r: 1.6, noCam: true });
    // the north gate to the cave
    const gh = gatehouse({ span: 7 });
    gh.group.scale.setScalar(0.85);
    this.colliders.push(...place(scene, gh, 0, height(0, -RAD - 2), -RAD - 2, 0).map(c => ({ ...c, r: c.r ? c.r * 0.85 : c.r, hw: c.hw ? c.hw * 0.85 : c.hw, hd: c.hd ? c.hd * 0.85 : c.hd, x: c.x * 0.85, z: -RAD - 2 + (c.z + RAD + 2) * 0.85 })));
    const gate = await prop('large_iron_gate', 5.6).catch(() => null);
    if (gate) { gate.position.set(0, height(0, -RAD - 2), -RAD - 1.8); scene.add(gate); }
    this.colliders.push({ type: 'box', x: 0, z: -RAD - 2, hw: 3, hd: 0.6, rot: 0 });
    const board = new THREE.Mesh(new THREE.BoxGeometry(3.2, 1.6, 0.08), [M.timber(), M.timber(), M.timber(), M.timber(),
      new THREE.MeshStandardMaterial({ map: signTexture('ABANDON ALL HOPE', { w: 1024, h: 512, font: '600 70px Cinzel, Georgia, serif', bg: '#3a2a1a', ink: '#e8c890' }), roughness: 0.8 }), M.timber()]);
    // second line painted underneath
    const c = board.material[4].map.image, ctx = c.getContext('2d');
    ctx.font = '600 52px Cinzel, Georgia, serif'; ctx.fillStyle = '#e8c890'; ctx.textAlign = 'center';
    ctx.fillText('YE WHO ENTER HERE!', 512, 340); ctx.fillStyle = '#d04020'; ctx.fillText('BEWARE OF THE DRAGON!', 512, 430);
    board.material[4].map.needsUpdate = true;
    board.position.set(5.5, height(5.5, -RAD + 3) + 1.9, -RAD + 3); board.rotation.y = -0.3; board.castShadow = true; scene.add(board);
    scene.add(box(0.15, 1.3, 0.15, M.timber(), 5.0, height(5.0, -RAD + 3), -RAD + 3.1), box(0.15, 1.3, 0.15, M.timber(), 6.0, height(6.0, -RAD + 3), -RAD + 2.8));
    this.interactables.push({ pos: new THREE.Vector3(0, 0, -RAD + 1.5), radius: 4, label: 'Read the sign on the gate', action: () => this.gate() });
    // warriors for hire, by the hospital
    this.warriors = [];
    [0, 1, 2].forEach(i => {
      if (this.state.hired.includes(i)) return;
      const a = knight({ steelTint: 0x8a8a90, cloth: '#1e4a24', emblem: false, cape: null, plume: 0x1e6a2a, helm: 'kettle' });
      const x = 12 + i * 2.2, z = 8 - i * 1.4;
      a.root.position.set(x, 0.04, z); a.root.rotation.y = -2.2;
      scene.add(a.root);
      a.update(0.016);
      const w = { i, actor: a, price: [120, 150, 180][i] };
      w.inter = { pos: new THREE.Vector3(x, 0, z), radius: 2.4, label: 'Talk to the warrior', action: () => this.hire(w) };
      this.interactables.push(w.inter);
      this.colliders.push(w.col = { type: 'circle', x, z, r: 0.5 });
      this.warriors.push(w);
    });
    // woods outside the wall
    const trees = [];
    for (let i = 0; i < 260; i++) { const a = rand() * Math.PI * 2, d = RAD + 8 + rand() * 90; const x = Math.sin(a) * d, z = Math.cos(a) * d; if (Math.abs(x) < 8 && (z > RAD || z < -RAD)) continue; trees.push({ x, z, y: height(x, z), type: rand() < 0.5 ? 'pine' : rand() < 0.5 ? 'oak' : 'ash', scale: 0.8 + rand() * 0.5 }); }
    scene.add(forest(trees, { seedBase: 7 }));
    await Promise.all(pend);
  }
  start() { const pl = this.game.player; pl.pos.set(0, 0, RAD + 6); pl.yaw = Math.PI; pl.pos.y = this.groundAt(pl.pos.x, pl.pos.z); }
  groundAt = (x, z) => Math.hypot(x, z) < 19 ? Math.max(0.04, this.heightFn(x, z)) : this.heightFn(x, z);
  clamp = (x, z) => {
    const r = Math.hypot(x, z);
    if (r > RAD + 30) { x *= (RAD + 30) / r; z *= (RAD + 30) / r; }
    return [x, z];
  };
  update(dt) {
    const pl = this.game.player;
    this.grass.update(dt, gfx.camera.position, pl.pos);
    for (const w of this.warriors) { w.actor.t += dt; w.actor.update(dt); }
    if (pl.pos.z < -RAD - 3 && !this.leaving) { this.leaving = true; this.game.onAreaExit?.(3); }
    if (pl.pos.z > RAD + 25) { pl.pos.z = RAD + 25; this.game.hud.say(['THE ROAD BACK', 'IS LONG. THE DRAGON', 'LIES NORTH']); }
  }

  // ----- the shops -----
  async trade() {
    const g = this.game, p = g.stats;
    sfx('door');
    for (;;) {
      const c = await g.hud.dialog({ title: 'The Trade Shop', text: 'The shopkeeper looks up from his ledger. “Buy, sell or trade?”', options: [{ label: 'Buy', value: 'buy' }, { label: 'Sell', value: 'sell' }, { label: 'Trade', value: 'trade' }, { label: 'Leave', value: null }] });
      if (!c) return;
      if (c === 'buy') {
        const n = await g.hud.dialog({ title: 'Buy', text: `You have ${p.gold} gold.`, options: [...Object.entries(R.GOODS).map(([n, gd]) => ({ label: `${n} — ${gd.price} gold`, sub: gd.info + (p.goods[n] ? ` (you have ${p.goods[n]})` : ''), value: n, icon: n, off: (ONE_OFF.includes(n) && p.goods[n] > 0) || p.gold < gd.price })), { label: 'Back', value: null }] });
        if (!n) continue;
        p.gold -= R.GOODS[n].price; p.goods[n]++; sfx('coin'); g.hud.say([n, R.GOODS[n].info]);
      } else if (c === 'sell') {
        const mine = R.ITEM_NAMES.filter(n => p.inv[n] > 0);
        if (!mine.length) { g.hud.say(['YOU HAVE', 'NOTHING TO SELL']); continue; }
        const n = await g.hud.dialog({ title: 'Sell', text: 'What will you part with?', options: [...mine.map(n => ({ label: n + (p.inv[n] > 1 ? ' ×' + p.inv[n] : ''), value: n, icon: n })), { label: 'Back', value: null }] });
        if (!n) continue;
        const offer = R.SELL_PRICE[n] + R.randBelow(10);
        const a = await g.hud.dialog({ title: 'HE OFFERS ' + offer + ' GOLD', text: `For your ${n}.`, options: [{ label: 'Accept', value: true }, { label: 'Refuse', value: false }] });
        if (a) { p.inv[n]--; p.gold += offer; sfx('coin'); g.hud.say(['DONE']); } else g.hud.say(['NO THANKS']);
      } else if (c === 'trade') {
        const mine = R.ITEM_NAMES.filter(n => p.inv[n] > 0);
        if (!mine.length) { g.hud.say(['YOU HAVE', 'NOTHING TO TRADE']); continue; }
        const give = await g.hud.dialog({ title: 'PICK AN ITEM', options: [...mine.map(n => ({ label: n, value: n, icon: n })), { label: 'Back', value: null }] });
        if (!give) continue;
        const want = await g.hud.dialog({ title: 'WHAT DO YOU WANT FOR IT?', options: [...Object.keys(R.GOODS).map(n => ({ label: n, value: n, icon: n, off: ONE_OFF.includes(n) && p.goods[n] > 0 })), { label: 'Back', value: null }] });
        if (!want) continue;
        const diff = Math.max(0, R.GOODS[want].price - R.SELL_PRICE[give] * 2);
        const a = await g.hud.dialog({ title: 'HE WANTS ' + diff + ' GOLD MORE', text: `Your ${give} and ${diff} gold for a ${want}.`, options: [{ label: 'Accept', value: true, off: p.gold < diff }, { label: 'Refuse', value: false }] });
        if (!a) { g.hud.say(['NO THANKS']); continue; }
        p.gold -= diff; p.inv[give]--; p.goods[want]++; sfx('coin'); g.hud.say([want, R.GOODS[want].info]);
      }
    }
  }
  async magic() {
    const g = this.game, p = g.stats;
    sfx('door');
    for (;;) {
      const n = await g.hud.dialog({ title: 'The Magic Shop', text: `Jars of strange powders line the shelves. You have ${p.gold} gold. Spells are cast with keys 1–6.`, options: [...Object.entries(R.SPELLS).map(([n, sp], i) => ({ label: `${n} — ${sp.price} gold`, sub: sp.info + ` (key ${i + 1}${p.spells[n] ? ', you have ' + p.spells[n] : ''})`, value: n, off: p.gold < sp.price })), { label: 'Leave', value: null }] });
      if (!n) return;
      p.gold -= R.SPELLS[n].price; p.spells[n]++; sfx('magic');
      g.fx.burst(g.player.pos.clone().setY(g.player.pos.y + 1.2), 'magic', 30);
      g.hud.say(['SPELL ' + n, R.SPELLS[n].info]);
    }
  }
  async hospital() {
    const g = this.game, p = g.stats;
    sfx('door');
    const need = p.maxStr - Math.max(0, p.str);
    if (need <= 0 && !p.fever) { await g.hud.message('The Hospital', 'The doctor looks you over. “You are well.”'); return; }
    const cost = Math.max(10, need * 3);
    const a = await g.hud.dialog({ title: 'The Hospital', text: `“Rest here and you will be whole again${p.fever ? ', and rid of that fever' : ''}. ${cost} gold.”`, options: [{ label: 'Accept', value: true, off: p.gold < cost }, { label: 'Refuse', value: false }] });
    if (!a) return;
    p.gold -= cost; p.str = p.maxStr; p.fever = 0;
    sfx('heal'); g.fx.burst(g.player.pos.clone().setY(g.player.pos.y + 1), 'heal', 50);
    g.hud.say(['YOU FEEL', 'STRONG AGAIN']);
  }
  async hire(w) {
    const g = this.game, p = g.stats;
    const a = await g.hud.dialog({ title: 'A Warrior', text: `ONE MAN IS INTERESTED. HE WANTS ${w.price} GOLD. He will follow you into the cave and fight at your side.`, options: [{ label: 'Enlist him', value: true, off: p.gold < w.price }, { label: 'No thanks', value: false }] });
    if (!a) { g.hud.say(['NO THANKS']); return; }
    p.gold -= w.price; p.warriors++;
    this.state.hired.push(w.i);
    w.actor.root.parent.remove(w.actor.root); w.inter.off = true; w.col.off = true;
    const ally = g.addAlly(); ally.pos.copy(w.actor.root.position);
    sfx('clang', { vol: 0.5 }); g.hud.say(['WARRIOR', 'SERVICE', 'HE JOINS YOU']);
  }
  async gate() {
    const g = this.game, p = g.stats;
    await g.hud.message('There is a sign on the gate', '“!ABANDON ALL HOPE, YE WHO ENTER HERE!” — and it warns: “!BEWARE OF THE DRAGON!”');
    const a = await g.hud.dialog({ title: 'Do you enter?', text: p.goods.LANTERN ? 'Your lantern is lit.' : 'It is dark beyond the gate. A LANTERN would help.', options: [{ label: 'Enter', value: true }, { label: 'Leave', value: false }] });
    if (!a) return;
    sfx('door');
    g.hud.say(['YOU ENTER', 'THE DARKNESS']);
    this.leaving = true; g.onAreaExit?.(3);
  }
  dispose() {}
}
