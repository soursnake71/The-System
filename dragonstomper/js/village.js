// Part 2: the village beyond the bridge. Shops, a hospital, warriors for hire, and the gate
// with its sign: ABANDON ALL HOPE YE WHO ENTER HERE! ... BEWARE OF THE DRAGON!
import * as THREE from '../lib/three.module.min.js';
import { ROM } from '../data.js';
import { G, newScene, addCollider, moveHero, save } from './engine.js';
import { spriteMesh } from './voxel.js';
import { drawText } from './ui.js';
import { Followers, warriorMesh } from './followers.js';
import { sfx } from './audio.js';
import * as R from './rules.js';

const SP = ROM.part2.sprites;
const W = 26, N = -60;
const ONE_OFF = ['LANTERN', 'CHAIN', 'LONGBOW', 'SAPPHIRE', 'RUBY'];
const gp = (n) => String(n).padStart(3, '0');

export class Village {
  constructor(state = {}) {
    this.state = { hired: [], ...state };
    this.ctx = 'field';
  }

  build() {
    const s = newScene({ sky: 0x95b7f0, fog: [60, 150], hemiGround: 0x484800 });
    const ground = new THREE.Mesh(new THREE.PlaneGeometry(160, 160, 40, 40), new THREE.MeshLambertMaterial({ color: 0x505916 }));
    ground.rotation.x = -Math.PI / 2; ground.position.z = N / 2; ground.receiveShadow = true; s.add(ground);
    // a dirt square
    const dirt = new THREE.Mesh(new THREE.PlaneGeometry(2 * W - 4, -N - 4), new THREE.MeshLambertMaterial({ color: 0x6d762b }));
    dirt.rotation.x = -Math.PI / 2; dirt.position.set(0, 0.02, N / 2); dirt.receiveShadow = true; s.add(dirt);
    // palisade
    const wood = new THREE.MeshLambertMaterial({ color: 0x694d14 });
    const stake = new THREE.BoxGeometry(0.9, 3.2, 0.9);
    const walls = [];
    for (let x = -W; x <= W; x += 1.2) { if (Math.abs(x) > 4) walls.push([x, 0], [x, N]); }
    for (let z = 0; z >= N; z -= 1.2) walls.push([-W, z], [W, z]);
    const im = new THREE.InstancedMesh(stake, wood, walls.length);
    const mx = new THREE.Matrix4();
    walls.forEach(([x, z], i) => { mx.makeTranslation(x, 1.6 + (i % 3) * 0.15, z); im.setMatrixAt(i, mx); });
    im.castShadow = true; s.add(im);

    const put = (key, x, z, scale, data, opts = {}) => {
      const sp = SP[key];
      const m = spriteMesh(sp.rows, opts.color || sp.color, opts.mode || 'taper', scale, { depth: opts.depth || 6 });
      m.position.set(x, 0, z);
      if (opts.rot) m.rotation.y = opts.rot;
      s.add(m);
      if (data) addCollider(x, z, opts.r || scale * 0.95, data);
      return m;
    };
    for (const [x, z] of [[-21, -5], [21, -5], [-21, -53], [21, -53]]) put('PALM', x, z, 2.6, { name: 'TREE', tree: true }, { mode: 'lathe', r: 1.4 });
    for (const x of [-15, 0, 15]) put('TREE', x, -36, 2.4, { name: 'TREE', tree: true }, { mode: 'lathe', r: 1.8 });
    put('MAGICSHOP', -9, -47, 3.6, { name: 'MAGIC SHOP', shop: 'magic' }, { depth: 8, r: 3.6 });
    put('HOSPITAL', 11, -23, 3.4, { name: 'HOSPITAL', shop: 'hospital' }, { depth: 8, r: 3.4 });
    put('TRADESHOP', -15, -14, 3.2, { name: 'TRADE SHOP', shop: 'trade' }, { depth: 8, r: 3.2 });
    // signs over the doors
    this.sign(s, 'MAGIC', -9, -43.6, 8.3);
    this.sign(s, 'HOSPITAL', 11, -19.9, 7.8);
    this.sign(s, 'TRADE', -15, -10.8, 6.1);
    // the gate in the north wall, with the cave door behind it
    const door = put('DOOR', 0, N - 1.5, 3.4, null, { color: '#1a1a1a', depth: 4 });
    this.gate = put('GATE', 0, N + 0.2, 3.2, null, { mode: 'slab', depth: 1, color: '#842c00' });
    addCollider(0, N + 1, 3, { name: 'GATE', gate: true });
    const post = new THREE.Mesh(new THREE.BoxGeometry(0.3, 2.4, 0.3), wood); post.position.set(4.5, 1.2, N + 3); s.add(post);
    this.sign(s, 'BEWARE', 4.5, N + 3.2, 2.6, '#fce070');
    // warriors for hire
    this.warriors = [];
    [-11, -6.5, -2].forEach((x, i) => {
      if (this.state.hired.includes(i)) return;
      const g = warriorMesh(1.15);
      g.position.set(x, 0, -22); g.rotation.y = 0.3;
      s.add(g);
      const w = { g, i, price: [120, 150, 180][i], name: 'WARRIOR' };
      w.col = addCollider(x, -22, 1.2, { warrior: w });
      this.warriors.push(w);
    });
    this.followers = new Followers(0, s);
    G.walkSpeed = 7;
  }

  sign(s, text, x, z, y, color = '#fff') {
    const cv = document.createElement('canvas'); cv.width = text.length * 4 + 3; cv.height = 8;
    const c = cv.getContext('2d'); c.fillStyle = '#1a1a1a'; c.fillRect(0, 0, cv.width, cv.height);
    drawText(c, text, 2, 2, color);
    const tex = new THREE.CanvasTexture(cv); tex.magFilter = THREE.NearestFilter; tex.colorSpace = THREE.SRGBColorSpace;
    const m = new THREE.Mesh(new THREE.PlaneGeometry(cv.width * 0.16, cv.height * 0.16), new THREE.MeshBasicMaterial({ map: tex }));
    m.position.set(x, y, z); s.add(m);
  }

  start() {
    G.hero.position.set(0, 0, -3); G.hero.rotation.y = Math.PI;
    G.camOffset.set(0, 11, 12); G.camLook.set(0, 1, -3);
    for (let i = 0; i < G.p.warriors; i++) this.followers.add(G.scene);
  }

  clamp = (x, z) => [Math.max(-W + 1.5, Math.min(W - 1.5, x)), Math.max(N + 1.5, Math.min(-1.5, z))];

  update(dt) {
    const b = moveHero(dt, { clamp: this.clamp });
    this.followers.update(dt);
    for (const w of this.warriors) w.g.rotation.y = Math.atan2(G.hero.position.x - w.g.position.x, G.hero.position.z - w.g.position.z);
    G.ui.idle(['THE VILLAGE', '', '', 'FIRE = MENU']);
    if (this.leave) return { goto: 3 };
    if (b) return { bump: b.data };
    return null;
  }

  async interact(d) {
    const push = (x, z) => { const h = G.hero.position, dx = h.x - x, dz = h.z - z, l = Math.hypot(dx, dz) || 1; h.x += dx / l * 1.4; h.z += dz / l * 1.4; };
    if (d.tree) { await G.ui.say(['TREE', 'NOBODY HERE']); return; }
    if (d.warrior) { await this.hire(d.warrior); return; }
    if (d.gate) { await this.gateTalk(); push(0, N + 1); return; }
    if (d.shop === 'trade') await this.trade();
    if (d.shop === 'magic') await this.magic();
    if (d.shop === 'hospital') await this.hospital();
    const h = G.hero.position; h.z += 2;
    G.ui.status(G.p);
    save({ area: this.state });
  }

  async trade() {
    const p = G.p;
    await G.ui.say(['TRADE SHOP', 'THE SHOPKEEPER', 'SAYS HELLO']);
    for (;;) {
      const c = await G.ui.dir4({ up: 'BUY', right: 'SELL', left: 'TRADE', down: 'LEAVE' }, 'TRADE SHOP');
      if (!c || c === 'down') return;
      if (c === 'up') {
        const items = Object.entries(R.GOODS).map(([n, g]) => ({ label: n.padEnd(9) + gp(g.price), value: n, off: ONE_OFF.includes(n) && p.goods[n] > 0 }));
        const n = await G.ui.list('BUY WHAT? ' + p.gold, items);
        if (!n) continue;
        const g = R.GOODS[n];
        if (p.gold < g.price) { sfx('bad'); await G.ui.say(['HE WANTS', g.price + ' GOLD', 'YOU CANNOT', 'DO THAT']); continue; }
        p.gold -= g.price; p.goods[n]++; sfx('coin');
        G.ui.status(p);
        await G.ui.say([n, g.info, 'DONE']);
      } else if (c === 'right') {
        const items = R.ITEM_NAMES.filter(n => p.inv[n] > 0).map(n => ({ label: n + (p.inv[n] > 1 ? ' X' + p.inv[n] : ''), value: n }));
        if (!items.length) { await G.ui.say(['YOU HAVE', 'NOTHING TO SELL']); continue; }
        const n = await G.ui.list('WANT TO SELL', items);
        if (!n) continue;
        const offer = R.SELL_PRICE[n] + R.randBelow(10);
        await G.ui.say(['HE OFFERS', offer + ' GOLD']);
        const a = await G.ui.dir4({ right: 'ACCEPT', left: 'REFUSE' });
        if (a === 'right') { p.inv[n]--; p.gold += offer; sfx('coin'); G.ui.status(p); await G.ui.say(['DONE']); }
        else await G.ui.say(['NO THANKS']);
      } else if (c === 'left') {
        const mine = R.ITEM_NAMES.filter(n => p.inv[n] > 0).map(n => ({ label: n, value: n }));
        if (!mine.length) { await G.ui.say(['YOU HAVE', 'NOTHING TO TRADE']); continue; }
        const give = await G.ui.list('PICK AN ITEM', mine);
        if (!give) continue;
        const items = Object.entries(R.GOODS).map(([n, g]) => ({ label: n, value: n, off: ONE_OFF.includes(n) && p.goods[n] > 0 }));
        const want = await G.ui.list('WHAT DO YOU WANT', items);
        if (!want) continue;
        const diff = Math.max(0, R.GOODS[want].price - R.SELL_PRICE[give] * 2);
        await G.ui.say(['FOR YOUR ' + give, 'HE WANTS', diff + ' GOLD MORE']);
        const a = await G.ui.dir4({ right: 'ACCEPT', left: 'REFUSE' });
        if (a !== 'right') { await G.ui.say(['NO THANKS']); continue; }
        if (p.gold < diff) { sfx('bad'); await G.ui.say(['YOU CANNOT', 'DO THAT']); continue; }
        p.gold -= diff; p.inv[give]--; p.goods[want]++; sfx('coin'); G.ui.status(p);
        await G.ui.say([want, R.GOODS[want].info, 'DONE']);
      }
    }
  }

  async magic() {
    const p = G.p;
    await G.ui.say(['MAGIC SHOP', 'SPELLS FOR SALE']);
    for (;;) {
      const items = Object.entries(R.SPELLS).map(([n, g]) => ({ label: n.padEnd(8) + gp(g.price) + (p.spells[n] ? ' ' + p.spells[n] : ''), value: n }));
      const n = await G.ui.list('BUY WHAT? ' + p.gold, items);
      if (!n) return;
      const g = R.SPELLS[n];
      if (p.gold < g.price) { sfx('bad'); await G.ui.say(['HE WANTS', g.price + ' GOLD', 'YOU CANNOT', 'DO THAT']); continue; }
      p.gold -= g.price; p.spells[n]++; sfx('magic'); G.ui.status(p);
      G.fx.burst(G.hero.position.clone().setY(2), ['#d48cfc', '#fff'], 25, { speed: 2, up: 5, gravity: 2, size: 0.12 });
      await G.ui.say(['SPELL ' + n, g.info, 'DONE']);
    }
  }

  async hospital() {
    const p = G.p;
    const need = p.maxStr - p.str;
    if (need <= 0 && !p.fever) { await G.ui.say(['HOSPITAL', 'YOU ARE WELL']); return; }
    const cost = Math.max(10, need * 3);
    await G.ui.say(['HOSPITAL', 'THE DOCTOR', 'WANTS ' + cost + ' GOLD']);
    const a = await G.ui.dir4({ right: 'ACCEPT', left: 'REFUSE' });
    if (a !== 'right') return;
    if (p.gold < cost) { sfx('bad'); await G.ui.say(['YOU CANNOT', 'DO THAT']); return; }
    p.gold -= cost; p.str = p.maxStr; p.fever = 0; sfx('magic');
    G.fx.burst(G.hero.position.clone().setY(2), ['#fff', '#84fcd4'], 30, { speed: 2, up: 6, gravity: 3, size: 0.12 });
    G.ui.status(p);
    await G.ui.say(['YOU FEEL', 'STRONG AGAIN']);
  }

  async hire(w) {
    const p = G.p;
    await G.ui.say(['WARRIOR', 'ONE MAN IS', 'INTERESTED', 'HE WANTS ' + w.price + ' GOLD']);
    const a = await G.ui.dir4({ right: 'ENLIST', left: 'NO THANKS' });
    if (a !== 'right') { await G.ui.say(['NO THANKS']); G.hero.position.z += 1.5; return; }
    if (p.gold < w.price) { sfx('bad'); await G.ui.say(['YOU CANNOT', 'DO THAT']); G.hero.position.z += 1.5; return; }
    p.gold -= w.price; p.warriors++; sfx('part');
    this.state.hired.push(w.i);
    w.g.parent.remove(w.g);
    const i = G.colliders.indexOf(w.col); if (i >= 0) G.colliders.splice(i, 1);
    this.warriors.splice(this.warriors.indexOf(w), 1);
    this.followers.add(G.scene);
    G.ui.status(p);
    await G.ui.say(['WARRIOR', 'SERVICE', 'HE JOINS YOU']);
    save({ area: this.state });
  }

  async gateTalk() {
    await G.ui.say(['THERE IS', 'A SIGN ON', 'THE GATE']);
    await G.ui.say(['!ABANDON ALL', 'HOPE, YE WHO', 'ENTER HERE!']);
    await G.ui.say(['AND IT WARNS', '!BEWARE OF', 'THE DRAGON!']);
    const a = await G.ui.dir4({ up: 'ENTER', down: 'LEAVE' }, 'DO YOU');
    if (a !== 'up') return;
    if (!G.p.goods.LANTERN) await G.ui.say(['IT IS DARK', 'A LANTERN', 'WOULD HELP']);
    sfx('door');
    await G.ui.say(['YOU ENTER', 'THE DARKNESS']);
    this.leave = true;
  }
}
