// The message window, menus and status line, drawn with the original 3x5 font from the ROM.
import { ROM } from '../data.js';
import { take, clearQueue, bindButton } from './input.js';
import { sfx } from './audio.js';

// Glyphs the ROM font lacks (it has no punctuation beyond '!')
const EXTRA = {
  '.': [0, 0, 0, 0, 2], ',': [0, 0, 0, 2, 4], "'": [2, 2, 0, 0, 0], '-': [0, 0, 7, 0, 0], ':': [0, 2, 0, 2, 0],
  '?': [6, 1, 2, 0, 2], '/': [1, 1, 2, 4, 4], '+': [0, 2, 7, 2, 0], '>': [4, 2, 1, 2, 4], '<': [1, 2, 4, 2, 1],
  '(': [1, 2, 2, 2, 1], ')': [4, 2, 2, 2, 4], '=': [0, 7, 0, 7, 0], '*': [5, 2, 7, 2, 5], 'Q': [2, 5, 5, 6, 3],
};
const FONT = { ...ROM.font, ...EXTRA };
const ARROW = { up: '▲', right: '▶', left: '◀', down: '▼' };

export const THEMES = {
  1: { band: '#545cd6', box: '#a8308f', ink: '#fc90fc' },
  2: { band: '#545cd6', box: '#a8308f', ink: '#fc90fc' },
  3: { band: '#d48cfc', box: '#18624e', ink: '#84fcd4' },
};

export function drawText(ctx, str, x, y, color) {
  ctx.fillStyle = color;
  for (const ch of str.toUpperCase()) {
    const g = FONT[ch] || FONT[' '];
    for (let r = 0; r < 5; r++) for (let c = 0; c < 3; c++) if ((g[r] >> (2 - c)) & 1) ctx.fillRect(x + c, y + r, 1, 1);
    x += 4;
  }
}

const COLS = 20, LINES = 4;
export class UI {
  constructor() {
    this.box = document.getElementById('box');
    this.cv = document.getElementById('boxcv');
    this.cv.width = COLS * 4 + 7; this.cv.height = LINES * 7 + 5;
    this.ctx = this.cv.getContext('2d');
    this.stat = document.getElementById('statcv');
    this.sctx = this.stat.getContext('2d');
    this.dia = document.getElementById('diamond');
    this.theme = THEMES[1];
    this.busy = false;
    this.lines = [];
    this.cursorLine = -1;
    this.blink = 0;
    this.idleText = null;
    for (const b of this.dia.querySelectorAll('button')) bindButton(b, b.dataset.k);
    // tapping a line of the window picks that line in a list
    this.cv.addEventListener('pointerdown', e => {
      const r = this.cv.getBoundingClientRect();
      const line = Math.floor(((e.clientY - r.top) / r.height * this.cv.height - 3) / 7);
      this.tapLine = line;
      if (!this.listMode) this.tapA = true;
    });
  }
  setTheme(n) {
    this.theme = THEMES[n];
    document.documentElement.style.setProperty('--band', this.theme.band);
    document.documentElement.style.setProperty('--box', this.theme.box);
    this.render();
  }
  render() {
    const c = this.ctx, t = this.theme;
    c.fillStyle = t.box; c.fillRect(0, 0, this.cv.width, this.cv.height);
    const show = this.busy ? this.lines : (this.idleText || []);
    show.forEach((l, i) => drawText(c, l, 4, 3 + i * 7, t.ink));
    if (this.more && (this.blink & 16)) drawText(c, ARROW.down, this.cv.width - 7, 3 + 3 * 7, t.ink);
  }
  tick() { this.blink++; if (this.more) this.render(); }
  status(p, extra = '') {
    const c = this.sctx, w = this.stat.width = 120, h = this.stat.height = 8;
    c.clearRect(0, 0, w, h);
    const g = String(p.gold).padStart(4, '0');
    const s = `STR ${String(Math.max(0, p.str)).padStart(2, '0')} DEX ${String(p.dex).padStart(2, '0')} ${g} GOLD`;
    drawText(c, s, 1, 1, '#fff');
    if (extra) drawText(c, extra, 1 + (s.length + 1) * 4, 1, '#fc0');
  }
  idle(lines) { this.idleText = lines; if (!this.busy) this.render(); }

  // wait for one input event; directions and buttons
  async next() {
    for (;;) {
      if (this.tapA) { this.tapA = false; return 'A'; }
      if (this.tapLine != null && this.listMode) { const l = this.tapLine; this.tapLine = null; return { line: l }; }
      this.tapLine = null;
      const k = take();
      if (k) return k;
      await new Promise(r => requestAnimationFrame(r));
    }
  }
  open(diamond) {
    this.busy = true; clearQueue(); this.tapA = false; this.tapLine = null;
    this.dia.className = diamond || '';
    document.body.classList.add('modal');
  }
  close() {
    this.busy = false; this.more = false; this.listMode = false;
    this.dia.className = ''; document.body.classList.remove('modal');
    this.render();
  }

  // Show text; pages of up to 4 lines, each waits for a press (like "MORE" in the original)
  async say(text, opts = {}) {
    const lines = Array.isArray(text) ? text.flatMap(t => wrap(t)) : wrap(text);
    this.open('more');
    sfx('text');
    for (let i = 0; i < lines.length; i += LINES) {
      this.lines = lines.slice(i, i + LINES);
      this.more = true;
      this.render();
      if (opts.time) { await new Promise(r => setTimeout(r, opts.time)); continue; }
      for (;;) { const k = await this.next(); if (k === 'A' || k === 'down' || k === 'B' || k === 'right') break; }
      sfx('blip');
    }
    this.close();
  }

  // The four-way stick menu: { up: 'MOVE', right: 'FIGHT', left: 'USE', down: 'STATUS' }
  async dir4(opts, title) {
    this.open('dir');
    for (const [k, b] of Object.entries({ up: 0, right: 1, left: 2, down: 3 })) {
      const btn = this.dia.querySelector(`[data-k=${k}]`);
      btn.querySelector('span').textContent = opts[k] || '';
      btn.style.visibility = opts[k] ? '' : 'hidden';
    }
    this.lines = ['up', 'right', 'left', 'down'].filter(k => opts[k]).map(k => ARROW[k] + ' ' + opts[k]);
    if (title && this.lines.length < 4) this.lines.unshift(title);
    this.render();
    for (;;) {
      const k = await this.next();
      if (opts[k]) { sfx('blip'); this.close(); return k; }
      if (k === 'B' || k === 'A') { this.close(); return null; }
    }
  }

  // A scrolling list. items: [{label, value, off}] ; returns value or null
  async list(title, items, start = 0) {
    this.open('list'); this.listMode = true;
    let cur = Math.min(start, items.length - 1), top = 0;
    const vis = LINES - 1;
    const draw = () => {
      if (cur < top) top = cur;
      if (cur >= top + vis) top = cur - vis + 1;
      this.lines = [title];
      for (let i = top; i < Math.min(items.length, top + vis); i++)
        this.lines.push((i === cur ? ARROW.right : ' ') + items[i].label);
      this.render();
    };
    draw();
    for (;;) {
      const k = await this.next();
      if (k && k.line != null) {
        const i = top + k.line - 1;
        if (k.line >= 1 && i < items.length) { if (i === cur) { if (!items[i].off) { sfx('blip'); this.close(); return items[i].value; } } else { cur = i; draw(); } }
        continue;
      }
      if (k === 'up') { cur = (cur + items.length - 1) % items.length; sfx('tick'); draw(); }
      else if (k === 'down') { cur = (cur + 1) % items.length; sfx('tick'); draw(); }
      else if (k === 'A' || k === 'right') { if (items[cur].off) { sfx('bad'); continue; } sfx('blip'); this.close(); return items[cur].value; }
      else if (k === 'B' || k === 'left') { this.close(); return null; }
    }
  }
}

export function wrap(s) {
  const out = [];
  for (const para of String(s).split('\n')) {
    let line = '';
    for (const w of para.split(' ')) {
      if (!line) line = w;
      else if ((line + ' ' + w).length <= COLS) line += ' ' + w;
      else { out.push(line); line = w; }
    }
    out.push(line);
  }
  return out;
}
