// Dragonstomper 3D - boot, the main loop and moving between the three parts.
import { ROM } from '../data.js';
import { G, initEngine, makeHero, animateHero, updateCamera, stepWorld, save, load, wipe } from './engine.js';
import { pollInput, take, clearQueue, bindTouch, bindButton, input } from './input.js';
import { UI, drawText } from './ui.js';
import { audioStart, sfx, toggleMute, isMuted, ambience } from './audio.js';
import { fieldMenu, animateMonsters } from './play.js';
import { Countryside } from './countryside.js';
import { Village } from './village.js';
import { Cave } from './cave.js';
import * as R from './rules.js';

const AREAS = [null, Countryside, Village, Cave];
const TITLES = [null, ['PART 1', 'THE ENCHANTED COUNTRYSIDE'], ['PART 2', 'THE VILLAGE'], ['PART 3', 'THE DRAGON\'S CAVE']];
const $ = (id) => document.getElementById(id);

let playing = false, busy = false, snapshot = null, regenT = 0;

initEngine($('view'));
G.ui = new UI();
makeHero();
bindTouch($('stickzone'), $('stickknob'), $('stickbase'));
bindButton($('btnA'), 'A');
bindButton($('btnB'), 'B');

// ---------- title screen ----------
function drawTitle() {
  const cv = $('titlecv'), c = cv.getContext('2d');
  cv.width = 160; cv.height = 70;
  c.clearRect(0, 0, 160, 70);
  const big = document.createElement('canvas'); big.width = 60; big.height = 7;
  drawText(big.getContext('2d'), 'DRAGONSTOMPER', 1, 1, '#fce070');
  c.imageSmoothingEnabled = false;
  c.drawImage(big, 0, 0, 60, 7, -10 + 18, 2, 120, 14);
  drawText(c, '3D', 74, 19, '#fc5454');
  // the cast, straight from the ROM
  const ms = ROM.part1.monsters;
  ms.forEach((m, i) => {
    const x = 4 + (i % 7) * 22 + (i >= 7 ? 11 : 0), y = 30 + (i >= 7 ? 20 : 0);
    const col = m.color === '#000000' ? '#777' : m.color;
    c.fillStyle = col;
    m.rows.forEach((r, ry) => { for (let b = 0; b < 8; b++) if ((r >> (7 - b)) & 1) c.fillRect(x + b, y + ry, 1, 1); });
  });
}
drawTitle();
function showTitle() {
  playing = false;
  $('title').hidden = false;
  $('continue').hidden = !load();
  ambience(null);
}

async function newGame() {
  wipe();
  G.p = R.newPlayer();
  R.seed(G.p.seed);
  await enterArea(1, 'new');
}
async function continueGame() {
  const s = load();
  if (!s) return newGame();
  G.p = s.p;
  R.seed(G.p.seed ^ Date.now());
  await enterArea(G.p.part, 'resume', s.area);
}

async function enterArea(n, from, areaState) {
  $('title').hidden = true; $('dead').hidden = true; $('win').hidden = true;
  G.dead = false;
  G.area?.leaving?.();
  G.p.part = n;
  G.ui.setTheme(n);
  document.body.dataset.part = n;
  $('loading').hidden = false;
  await new Promise(r => setTimeout(r, 30));
  G.area = new AREAS[n](areaState || {});
  G.area.build();
  G.scene.add(G.hero);
  G.area.start(from);
  updateCamera(0, null, true);
  $('loading').hidden = true;
  G.ui.status(G.p);
  snapshot = JSON.stringify({ p: G.p, area: G.area.state });
  save({ area: G.area.state });
  playing = true;
  if (from !== 'resume') await partCard(n);
}

async function partCard(n) {
  const el = $('partcard');
  el.querySelector('b').textContent = TITLES[n][0];
  el.querySelector('span').textContent = TITLES[n][1];
  el.hidden = false; el.classList.remove('fade'); void el.offsetWidth; el.classList.add('fade');
  sfx('part');
  setTimeout(() => { el.hidden = true; }, 2600);
  if (n === 1 && !G.p.seenIntro) {
    G.p.seenIntro = true;
    await run(async () => {
      await G.ui.say(['DRAGON', '   STOMPER', 'COPYRIGHT 82', 'STARPATH']);
      await G.ui.say(['THE DRAGON HAS', 'STOLEN THE', 'ENCHANTED', 'AMULET']);
      await G.ui.say(['CROSS THE LAND', 'GET PAST THE', 'BRIDGE GUARD', 'AND STOMP IT']);
      await G.ui.say(['WALK INTO THINGS', 'TO SEARCH THEM', 'FIRE OPENS', 'THE MENU']);
    });
  }
}

// run something that uses the message window, pausing the world while it does
async function run(fn) {
  busy = true;
  try { await fn(); } finally { busy = false; clearQueue(); }
  afterModal();
}
function afterModal() {
  G.ui.status(G.p, G.p.fever ? 'FEVER' : '');
  if (G.dead) return died();
  save({ area: G.area.state });
}

function died() {
  playing = false;
  sfx('death');
  $('dead').hidden = false;
  try { localStorage.setItem('ds3d-save', JSON.stringify({ ...JSON.parse(snapshot), v: 1 })); } catch (e) {}
}
function retry() {
  const s = JSON.parse(snapshot);
  G.p = s.p;
  enterArea(G.p.part, 'retry', s.area);
}
function won() {
  playing = false;
  sfx('win');
  setTimeout(async () => {
    busy = true;
    await G.ui.say(['YOU KILLED', 'THE DRAGON']);
    await G.ui.say(['YOU HAVE', 'DEFEATED', 'THE DRAGON']);
    await G.ui.say(['THE AMULET', 'IS YOURS', 'DRAGONSTOMPER!']);
    busy = false;
    const p = G.p;
    $('winstats').textContent = `Gold ${p.gold} · Strength ${p.str}/${p.maxStr} · Dexterity ${p.dex} · Monsters slain ${p.kills} · Warriors standing ${p.warriors}`;
    $('win').hidden = false;
    wipe();
  }, 1600);
}

// ---------- the loop ----------
function frame() {
  requestAnimationFrame(frame);
  const dt = Math.min(0.05 * (G.timeScale || 1), G.clock.getDelta() * (G.timeScale || 1));
  G.time += dt;
  pollInput();
  if (playing && !busy && G.area && !G.dead) {
    let k;
    while ((k = take())) {
      const boss = G.area.boss && G.area.dragonState && !['sleep', 'dead'].includes(G.area.dragonState.mode);
      if (k === 'A') { audioStart(); if (boss) G.area.attack(); else { run(fieldMenu); break; } }
      if (k === 'B' && boss) { run(fieldMenu); break; }
    }
    // holding fire keeps swinging at the dragon
    if (!busy && input.aHeld && G.area.boss && G.area.dragonState && !['sleep', 'dead'].includes(G.area.dragonState.mode)) G.area.attack();
    if (!busy) {
      const r = G.area.update(dt);
      if (r) handle(r);
      // $FCE2: strength creeps back, one point every 256 frames
      regenT += dt;
      if (regenT > 256 / 60) { regenT = 0; if (!G.p.fever && G.p.str < G.p.maxStr && G.p.str >= 0) { G.p.str++; G.ui.status(G.p); } }
    }
  }
  if (G.area) {
    animateHero(dt);
    animateMonsters(dt, G.battleMeshes, G.hero.position);
    G.anims = G.anims.filter(f => f(dt) !== false);
    stepWorld(dt);
    updateCamera(dt, G.battleFocus);
    G.renderer.render(G.scene, G.camera);
  }
  G.ui.tick();
}

function handle(r) {
  if (r.goto) { const n = r.goto; playing = false; G.area.leaving?.(); enterArea(n, 'new'); return; }
  if (r.win) { won(); return; }
  const target = r.bump ?? r;
  run(() => G.area.interact(target));
}

// ---------- buttons ----------
$('newgame').onclick = () => { audioStart(); newGame(); };
$('continue').onclick = () => { audioStart(); continueGame(); };
$('retry').onclick = () => { audioStart(); retry(); };
$('deadnew').onclick = () => { audioStart(); newGame(); };
$('winnew').onclick = () => { audioStart(); newGame(); };
const helpOpen = () => { $('help').hidden = false; };
$('howto').onclick = helpOpen; $('helpbtn').onclick = helpOpen;
$('helpclose').onclick = () => { $('help').hidden = true; };
$('mute').onclick = () => { audioStart(); $('mute').textContent = toggleMute() ? '\u{1F507}' : '\u{1F50A}'; };
$('mute').textContent = isMuted() ? '\u{1F507}' : '\u{1F50A}';
$('quit').onclick = () => { if (playing) { save({ area: G.area.state }); } showTitle(); };
addEventListener('pointerdown', () => audioStart(), { once: true });
addEventListener('keydown', () => audioStart(), { once: true });
document.addEventListener('visibilitychange', () => { if (document.hidden && playing && G.area) save({ area: G.area.state }); });

showTitle();
requestAnimationFrame(frame);
window.__G = G; // handy for poking at the game from the console
window.__DS = { enterArea, newGame };
