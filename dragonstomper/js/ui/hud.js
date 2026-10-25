// HUD, subtitles, prompts and modal dialogs (DOM over the canvas).
import * as THREE from 'three';
import { iconCanvas } from '../game/items.js';
import { sfx } from '../core/sound.js';
import { glyph } from '../core/input.js';

const $ = (id) => document.getElementById(id);
const el = (tag, cls, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html != null) e.innerHTML = html; return e; };

export class HUD {
  constructor() {
    this.root = $('hud');
    this.subs = $('subtitles');
    this.promptEl = $('prompt');
    this.toastEl = $('toasts');
    this.bossEl = $('boss');
    this.barsEl = $('enemybars');
    this.modalOpen = false;
    this.queue = []; this.subT = 0;
    this.compass = $('compass');
    this.bars = new Map();
  }
  show(on) { this.root.classList.toggle('on', on); }

  stats(p, pl, extra = {}) {
    const s = Math.max(0, p.str);
    $('hpfill').style.width = (s / p.maxStr * 100) + '%';
    $('hptext').textContent = `STR ${s} / ${p.maxStr}`;
    $('stfill').style.width = Math.max(0, pl.stamina) + '%';
    $('dex').textContent = 'DEX ' + p.dex;
    $('gold').textContent = p.gold;
    $('status').textContent = [p.fever ? 'FEVER' : '', p.protect > 0 ? 'PROTECTED' : '', p.stuck ? 'STUCK' : ''].filter(Boolean).join('  ');
    const sp = $('spells'); let html = '';
    ['BLAST', 'FLASH', 'STUN', 'UNLOCK', 'VISION', 'PROTECT'].forEach((n, i) => { if (p.spells[n]) html += `<div class="slot"><b>${i + 1}</b>${n}<i>${p.spells[n]}</i></div>`; });
    if (p.goods.LONGBOW) html += `<div class="slot"><b>${glyph('potion')}</b>BOW</div>`;
    if (sp.innerHTML !== html) sp.innerHTML = html;
    if (extra.heading != null) {
      const deg = ((-extra.heading * 180 / Math.PI) % 360 + 360) % 360;
      this.compass.style.backgroundPositionX = (-deg * 4) + 'px';
    }
    $('crosshair').classList.toggle('on', !!extra.crosshair);
  }
  prompt(text) {
    const key = glyph('interact');
    if (this._prompt === text && this._key === key) return;
    this._prompt = text; this._key = key;
    this.promptEl.innerHTML = text ? `<kbd>${key}</kbd> ${text}` : '';
    this.promptEl.classList.toggle('on', !!text);
  }
  // the original game's message lines, as subtitles
  say(lines, dur = 3.2) {
    const text = (Array.isArray(lines) ? lines : [lines]).filter(Boolean).join(' — ');
    this.queue.push({ text, dur });
    if (this.queue.length > 3) this.queue.shift();
  }
  toast(text, cls = '') {
    const t = el('div', 'toast ' + cls, text);
    this.toastEl.appendChild(t);
    setTimeout(() => t.classList.add('out'), 2600);
    setTimeout(() => t.remove(), 3300);
  }
  boss(name, frac) {
    if (frac == null) { this.bossEl.classList.remove('on'); return; }
    this.bossEl.classList.add('on');
    this.bossEl.querySelector('span').textContent = name;
    this.bossEl.querySelector('i').style.width = Math.max(0, frac * 100) + '%';
  }
  update(dt, camera, monsters) {
    if (this.subT > 0) { this.subT -= dt; if (this.subT <= 0) this.subs.classList.remove('on'); }
    if (this.subT <= 0.3 && this.queue.length) {
      const m = this.queue.shift();
      this.subs.textContent = m.text; this.subs.classList.add('on'); this.subT = m.dur;
    }
    // floating health bars over monsters that are hurt or close
    const v = new THREE.Vector3(), seen = new Set();
    for (const m of monsters) {
      if (!m.alive || m.boss) continue;
      m.hpShown = Math.max(0, (m.hpShown || 0) - dt);
      if (m.hpShown <= 0 && !m.aggro) continue;
      v.copy(m.pos); v.y += m.type.height + 0.35; v.project(camera);
      if (v.z > 1 || Math.abs(v.x) > 1.1 || Math.abs(v.y) > 1.1) continue;
      let b = this.bars.get(m);
      if (!b) { b = el('div', 'ebar', '<span></span><i></i>'); b.querySelector('span').textContent = m.name; this.barsEl.appendChild(b); this.bars.set(m, b); }
      b.style.transform = `translate(${(v.x * 0.5 + 0.5) * innerWidth}px, ${(-v.y * 0.5 + 0.5) * innerHeight}px) translate(-50%, -100%)`;
      b.querySelector('i').style.width = Math.max(0, (m.stats.hpLeft + 1) / (m.stats.hp + 1) * 100) + '%';
      seen.add(m);
    }
    for (const [m, b] of this.bars) if (!seen.has(m)) { b.remove(); this.bars.delete(m); }
  }
  clearBars() { for (const b of this.bars.values()) b.remove(); this.bars.clear(); }

  // A modal choice. options: [{ label, value, sub, off }]. Resolves with the value, or null.
  dialog({ title = '', text = '', options = [], art = null, cls = '' }) {
    return new Promise(res => {
      this.modalOpen = true;
      const wrap = $('dialog'); wrap.className = 'on ' + cls; wrap.innerHTML = '';
      const card = el('div', 'card');
      if (art) card.appendChild(art);
      if (title) card.appendChild(el('h2', '', title));
      if (text) card.appendChild(el('p', '', text));
      const list = el('div', 'opts');
      const close = (v) => { wrap.className = ''; wrap.innerHTML = ''; this.modalOpen = false; removeEventListener('keydown', key); res(v); };
      options.forEach((o, i) => {
        const b = el('button', o.off ? 'off' : '');
        b.innerHTML = `<b>${i + 1}</b><span>${o.label}</span>${o.sub ? `<small>${o.sub}</small>` : ''}`;
        if (o.icon) b.prepend(iconCanvas(o.icon, 40));
        b.onclick = () => { if (o.off) { sfx('bad'); return; } sfx('ui'); close(o.value); };
        list.appendChild(b);
      });
      card.appendChild(list);
      wrap.appendChild(card);
      const key = (e) => {
        const n = parseInt(e.key, 10);
        if (n >= 1 && n <= options.length) { e.preventDefault(); list.children[n - 1].click(); }
        if (e.key === 'Escape') { e.preventDefault(); sfx('uiBack'); close(null); }
      };
      addEventListener('keydown', key);
    });
  }
  // a message the player acknowledges
  async message(title, text, button = 'Continue') { return this.dialog({ title, text, options: [{ label: button, value: true }] }); }
}

export function inventoryPanel(game) {
  const p = game.stats;
  return new Promise(res => {
    const wrap = $('dialog'); wrap.className = 'on inv'; wrap.innerHTML = '';
    game.hud.modalOpen = true;
    const card = el('div', 'card wide');
    card.appendChild(el('h2', '', 'Inventory'));
    card.appendChild(el('p', 'statline', `STR ${Math.max(0, p.str)}/${p.maxStr} &nbsp; DEX ${p.dex} &nbsp; Gold ${p.gold} &nbsp; Sword 0-${p.weapon}${p.inv.HANDAXE ? ' +2 (handaxe)' : ''} &nbsp; Warriors ${p.warriors} &nbsp; Monsters slain ${p.kills}`));
    const grid = el('div', 'grid');
    const add = (kind, name, count, note) => {
      const c = el('button', 'cell');
      c.appendChild(iconCanvas(name, 56));
      c.appendChild(el('span', '', name + (count > 1 ? ' ×' + count : '')));
      c.appendChild(el('small', '', note));
      c.onclick = () => { sfx('ui'); close([kind, name]); };
      grid.appendChild(c);
    };
    const notes = { PAPER: 'Your ID for the bridge', HANDAXE: 'Weapon: hits +2', CHEST: 'Locked. Needs a KEY', SHIELD: 'Takes 1 off each blow', KEY: 'Opens doors and chests',
      CROSS: 'Magic. Effect unknown', RING: 'Magic. Effect unknown', STAFF: 'Magic. Effect unknown', CHARM: 'Magic. Effect unknown', POTION: 'Magic. Effect unknown' };
    for (const n of Object.keys(p.inv)) if (p.inv[n] > 0) add('inv', n, p.inv[n], notes[n] + (p.known?.[n] ? ' (' + p.known[n] + ')' : ''));
    const gnotes = { LANTERN: 'Lights the cave', MEDICINE: 'Cures fever', SAPPHIRE: 'Wards off dragon magic', CHAIN: 'Armour: -1 damage', ROPE: 'Climb out of pits', RUBY: 'Halves dragon fire', VITAMINS: 'Max STR +3', ELIXIR: 'Restores all STR', LONGBOW: 'Shoot with R' };
    for (const n of Object.keys(p.goods)) if (p.goods[n] > 0) add('goods', n, p.goods[n], gnotes[n]);
    for (const n of Object.keys(p.spells)) if (p.spells[n] > 0) add('spell', n, p.spells[n], 'Spell');
    if (!grid.children.length) grid.appendChild(el('p', 'empty', 'You carry nothing yet. Search trees, huts and rocks.'));
    card.appendChild(grid);
    card.appendChild(el('p', 'hint', 'Click an item to use it. Esc or I to close.'));
    wrap.appendChild(card);
    const close = (v) => { wrap.className = ''; wrap.innerHTML = ''; game.hud.modalOpen = false; removeEventListener('keydown', key); res(v); };
    const key = (e) => { if (e.key === 'Escape' || e.code === 'KeyI' || e.code === 'Tab') { e.preventDefault(); close(null); } };
    setTimeout(() => addEventListener('keydown', key), 50);
  });
}
