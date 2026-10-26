// Boot, menus and the main loop.
import * as THREE from 'three';
import { initGfx, gfx, render, settings, saveSettings, applyQuality, resize, adaptResolution } from './core/gfx.js';
import { input, initInput, pollInput, pressed, endFrame, lockPointer, unlockPointer } from './core/input.js';
import { initAudio, setVolume, setMusicVolume, music, sfx, ambience } from './core/sound.js';
import { progress } from './core/assets.js';
import { HUD } from './ui/hud.js';
import { initTouch } from './ui/touch.js';
import { Game } from './game/game.js';
import { Countryside } from './areas/countryside.js';
import { Village } from './areas/village.js';
import { Cave } from './areas/cave.js';
import * as R from './rules.js';

const AREAS = { 1: Countryside, 2: Village, 3: Cave };
const AREA_NAMES = { 1: 'The Countryside', 2: 'The Village', 3: 'The Dragon\'s Cave' };
const TIPS = [
  'Search trees, huts, rocks and lakes with E. Most hide nothing, some hide gold, a few hide monsters.',
  'The bridge guard lets you pass if you show him your ID paper. Keep it safe.',
  'Every blow is still decided by the 1982 dice: your DEX against the monster\'s attack.',
  'Raise your shield with the right mouse button. Each block costs stamina.',
  'Strength comes back slowly when nothing is hunting you.',
  'Magic items are shuffled every game. The cross that saved you last time might poison you now.',
  'Hired warriors fight beside you, all the way into the dragon\'s lair.',
  'Without a sapphire, half your spells will fail against the dragon.',
  'A ruby halves the dragon\'s fire. Buy one if you can afford it.',
];

const $ = (id) => document.getElementById(id);
const canvas = $('view');
const screens = ['title', 'loading', 'pause', 'settings', 'help', 'dead', 'won'];
let current = 'title', back = 'title';
function show(name) {
  for (const s of screens) $(s).classList.toggle('on', s === name);
  current = name;
}

initGfx(canvas);
initInput(canvas);
const hud = new HUD();
const game = new Game(hud);
initTouch(() => !!game.stats?.goods?.LONGBOW);
let playing = false;

// ---------- menus ----------
function refreshContinue() {
  const s = Game.loadSave(), b = $('bContinue');
  b.disabled = !s;
  b.querySelector('small').textContent = s ? `${AREA_NAMES[s.part] || ''} · ${s.p.gold} gold · STR ${Math.max(0, s.p.str)}` : '';
}
refreshContinue();

document.querySelectorAll('[data-open]').forEach(b => b.onclick = () => { sfx('ui'); back = current; show(b.dataset.open); syncSettings(); });
document.querySelectorAll('[data-back]').forEach(b => b.onclick = () => { sfx('uiBack'); show(back); });
$('bNew').onclick = () => newGame();
$('bContinue').onclick = () => continueGame();
$('bResume').onclick = () => resume();
$('bQuit').onclick = () => { game.save(); stop(); music(null); refreshContinue(); show('title'); };
$('bRetry').onclick = () => retry();
$('bDeadNew').onclick = () => newGame();
$('bWonNew').onclick = () => { Game.wipeSave(); refreshContinue(); newGame(); };
// the Windows app can quit; in a browser tab there's nothing to quit to
if (window.desktop) { $('bExit').hidden = false; $('bExit').onclick = () => { game.save(); window.desktop.quit(); }; }

// ---------- settings ----------
const fields = {
  sQuality: ['quality', v => v], sRes: ['resolution', v => v], sView: ['view', v => v],
  sFov: ['fov', Number], sSens: ['sensitivity', Number], sInv: ['invertY', v => v === '1'], sVol: ['volume', Number], sMusic: ['music', Number],
};
function syncSettings() {
  for (const [id, [key]] of Object.entries(fields)) {
    const e = $(id), v = settings[key];
    e.value = typeof v === 'boolean' ? (v ? '1' : '0') : String(v);
    const o = e.parentElement.querySelector('output');
    if (o) o.textContent = key === 'volume' || key === 'music' ? Math.round(v * 100) + '%' : key === 'fov' ? v + '°' : v.toFixed(2);
  }
  const s = gfx.renderSize || {};
  $('gpu').textContent = s.w ? `Rendering at ${s.w} × ${s.h}` + (s.w >= 3800 ? ' (4K)' : '') : '';
}
for (const [id, [key, conv]] of Object.entries(fields)) {
  $(id).addEventListener('input', () => {
    settings[key] = conv($(id).value);
    saveSettings();
    if (key === 'quality' || key === 'resolution') applyQuality();
    if (key === 'fov') resize();
    if (key === 'volume') setVolume(settings.volume);
    if (key === 'music') setMusicVolume(settings.music);
    if (key === 'view' && game.player) game.cam.setMode(settings.view, game.player);
    syncSettings();
  });
}

// ---------- flow ----------
function startAudio() { initAudio(settings.volume); setMusicVolume(settings.music); }

async function load(part, areaState, from) {
  startAudio();
  playing = false;
  hud.show(false);
  show('loading');
  $('tip').textContent = TIPS[(Math.random() * TIPS.length) | 0];
  $('loadtext').textContent = 'ENTERING ' + (AREA_NAMES[part] || '').toUpperCase();
  const t0 = performance.now();
  const tick = setInterval(() => {
    const f = progress.total ? progress.done / progress.total : 0;
    $('loadbar').querySelector('i').style.width = (f * 100).toFixed(1) + '%';
  }, 100);
  try { await game.enter(AREAS[part], part, areaState, from); }
  finally { clearInterval(tick); }
  // compile shaders before the first frame so the game doesn't hitch
  try { await gfx.renderer.compileAsync?.(game.scene, gfx.camera); } catch (e) {}
  $('loadbar').querySelector('i').style.width = '100%';
  console.log(`area ${part} ready in ${((performance.now() - t0) / 1000).toFixed(1)}s`);
  show(null);
  hud.show(true);
  playing = true;
  lockPointer(canvas);
  if (from === 'new' && part === 1) hud.say(['YOUR QUEST', 'FIND THE AMULET OF THE DRAGON'], 4);
  else hud.say([AREA_NAMES[part].toUpperCase()], 3);
}

function newGame() {
  sfx('ui');
  Game.wipeSave();
  game.stats = R.newPlayer();
  R.seed(game.stats.seed);
  game.ended = false;
  load(1, null, 'new');
}
function continueGame() {
  const s = Game.loadSave();
  if (!s) return newGame();
  sfx('ui');
  game.stats = s.p;
  R.seed(s.p.seed);
  game.ended = false;
  load(s.part, { ...s.area, pos: s.pos || s.area?.pos }, 'resume');
}
function retry() {
  sfx('ui');
  const s = JSON.parse(game.snapshot);
  game.stats = s.p;
  game.stats.stuck = 0;
  game.ended = false;
  load(s.part, s.area, 'retry');
}
function stop() { playing = false; game.paused = true; hud.show(false); unlockPointer(); ambience(null); }
function pause() {
  if (!playing || current === 'pause') return;
  game.paused = true;
  unlockPointer();
  sfx('uiBack');
  show('pause');
}
function resume() {
  sfx('ui');
  show(null);
  game.paused = false;
  lockPointer(canvas);
}

game.onAreaExit = (n) => {
  game.save();
  // the next region starts fresh; your stats and warriors come with you
  setTimeout(() => load(n, null, 'enter'), 400);
};
game.onDeath = () => {
  stop();
  music('defeat', { loop: false });
  const p = game.stats;
  $('deadtext').textContent = `You slew ${p.kills} monster${p.kills === 1 ? '' : 's'} and carried ${p.gold} gold.`;
  show('dead');
};
game.onVictory = () => {
  game.ended = true;
  sfx('victory');
  music('victory', { loop: false });
  hud.say(['YOU HAVE THE AMULET', 'THE LAND IS SAVED'], 6);
  setTimeout(() => {
    stop();
    const p = game.stats;
    $('wontext').textContent = `The dragon is dead and the amulet is yours. ${p.kills} monsters slain, ${p.gold} gold, ${p.warriors} warrior${p.warriors === 1 ? '' : 's'} at your side.`;
    Game.wipeSave();
    show('won');
  }, 5000);
};

// losing pointer lock in play (Esc, alt-tab) pauses, unless a dialog took it on purpose
input.onLockChange = (locked) => {
  $('clickhint').classList.remove('on');
  if (!locked && playing && !game.paused && !hud.modalOpen && !game.busy && !game.player?.dead) pause();
};
canvas.addEventListener('click', () => { if (playing && !game.paused && !input.locked && !hud.modalOpen) lockPointer(canvas); });
addEventListener('keydown', (e) => {
  if (e.code !== 'Escape') return;
  if (current === 'pause') resume();
  else if (current === 'settings' || current === 'help') { sfx('uiBack'); show(back); }
});
// the Android app: its Back button, and pausing when the app goes to the background
addEventListener('ds-pause', () => pause());
addEventListener('ds-back', () => {
  if (hud.modalOpen) dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', code: 'Escape' }));
  else if (current === 'pause') resume();
  else if (current === 'settings' || current === 'help') { sfx('uiBack'); show(back); }
  else if (current === 'dead' || current === 'won') { show('title'); refreshContinue(); }
  else pause();
});
// no controller on a phone or TV: say how to play
if (/Android/.test(navigator.userAgent)) {
  const hint = () => {
    const pads = navigator.getGamepads ? [...navigator.getGamepads()].filter(Boolean) : [];
    document.querySelector('#title .foot').textContent = pads.length ? 'Controller connected · press A / ✕ to start'
      : 'Tap to play with touch controls, or connect an Xbox or PlayStation controller';
  };
  hint(); addEventListener('gamepadconnected', hint); addEventListener('gamepaddisconnected', hint);
}
addEventListener('pointerdown', startAudio, { once: true });
addEventListener('keydown', startAudio, { once: true });
addEventListener('beforeunload', () => game.save());
setInterval(() => { if (playing && !game.paused) game.save(); }, 15000);

// ---------- controller navigation of menus and dialogs ----------
function padMenus() {
  const m = input.menu;
  if (!m.size) return false;
  const dlg = $('dialog').classList.contains('on') ? $('dialog') : null;
  const scr = current && current !== 'loading' ? $(current) : null;
  const root = dlg || scr;
  if (!root) return false;
  const btns = [...root.querySelectorAll('button:not([disabled])')];
  if (!btns.length) return false;
  let i = btns.indexOf(document.activeElement);
  if (m.has('down')) { btns[(i + 1) % btns.length].focus(); sfx('ui', { vol: 0.4 }); }
  if (m.has('up')) { btns[(i - 1 + btns.length) % btns.length].focus(); sfx('ui', { vol: 0.4 }); }
  if (m.has('ok')) { (i >= 0 ? btns[i] : btns[0]).click(); startAudio(); }
  if (m.has('back') || (m.has('start') && current === 'pause')) {
    if (dlg) dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', code: 'Escape' }));
    else if (current === 'pause') resume();
    else if (current === 'settings' || current === 'help') { sfx('uiBack'); show(back); }
  }
  return true;
}

// ---------- loop ----------
const clock = new THREE.Clock();
let frames = 0, fpsT = 0;
const fpsEl = $('fps');
if (/[?&]fps/.test(location.search)) fpsEl.classList.add('on');
function frame() {
  requestAnimationFrame(frame);
  const dt = Math.min(clock.getDelta(), 1 / 20) * (window.__timeScale || 1);
  pollInput();
  const navved = padMenus();
  if (playing && !navved && pressed('pause') && !hud.modalOpen && !game.busy) pause();
  if (playing && !game.paused) game.update(dt);
  if (gfx.scene && playing) { render(); adaptResolution(dt); }
  endFrame();
  frames++; fpsT += dt;
  if (fpsT > 0.5) { fpsEl.textContent = Math.round(frames / fpsT) + ' fps · ' + (gfx.renderSize?.w || 0) + '×' + (gfx.renderSize?.h || 0); frames = 0; fpsT = 0; }
}
// ?test drives the frames by hand (for headless screenshots)
const TEST = /[?&]test/.test(location.search);
if (!TEST) frame();

// test hooks
window.__G = game;
window.__DS = {
  load, newGame, continueGame, show, settings, hud,
  step(n = 1, dt = 1 / 30) {
    for (let i = 0; i < n; i++) { pollInput(); if (!game.paused) game.update(dt); endFrame(); }
    render();
    const gl = gfx.renderer.getContext(); gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array(4));
  },
};

if ('serviceWorker' in navigator && location.protocol === 'https:') navigator.serviceWorker.register('sw.js').catch(() => {});
