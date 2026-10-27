// Sound: recorded effects where we have them (footsteps, impacts, coins, doors), and
// Web Audio synthesis for everything else (creature voices, fire, magic, ambience).
import { SAMPLES } from './samples.js';
let ac = null, master = null, sfxBus = null, ambBus = null, verb = null, verbSend = null, noise = null;
let volume = 0.8, musicBus = null, musicVol = 0.6, cur = null;

export function initAudio(vol = 0.8) {
  volume = vol;
  if (ac) { if (ac.state === 'suspended') ac.resume(); return; }
  const AC = window.AudioContext || window.webkitAudioContext;
  if (!AC) return;
  ac = new AC();
  master = ac.createGain(); master.gain.value = volume;
  const comp = ac.createDynamicsCompressor(); comp.threshold.value = -14; comp.ratio.value = 4;
  master.connect(comp); comp.connect(ac.destination);
  sfxBus = ac.createGain(); sfxBus.connect(master);
  musicBus = ac.createGain(); musicBus.gain.value = musicVol; musicBus.connect(master);
  if (pendingMusic) { const [n, o] = pendingMusic; pendingMusic = null; music(n, o); }
  ambBus = ac.createGain(); ambBus.gain.value = 0.5; ambBus.connect(master);
  // a generated reverb impulse
  verb = ac.createConvolver();
  const len = ac.sampleRate * 2.5, ir = ac.createBuffer(2, len, ac.sampleRate);
  for (let c = 0; c < 2; c++) { const d = ir.getChannelData(c); for (let i = 0; i < len; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / len, 3); }
  verb.buffer = ir;
  verbSend = ac.createGain(); verbSend.gain.value = 0.15;
  verbSend.connect(verb); verb.connect(master);
  sfxBus.connect(verbSend);
  loadSamples();
  const n = ac.sampleRate * 2; noise = ac.createBuffer(1, n, ac.sampleRate);
  const d = noise.getChannelData(0); for (let i = 0; i < n; i++) d[i] = Math.random() * 2 - 1;
}
// Music (RandomMind, CC0, via OpenGameArt): one track at a time, crossfaded.
let pendingMusic = null;
export function setMusicVolume(v) { musicVol = v; if (musicBus) musicBus.gain.value = v; }
export function music(name, { loop = true, vol = 0.5 } = {}) {
  if (!ac) { pendingMusic = [name, { loop, vol }]; return; }
  if (cur && cur.name === name && !cur.el.ended) return;
  if (cur) { const old = cur; old.g.gain.setTargetAtTime(0.0001, now(), 0.8); setTimeout(() => old.el.pause(), 3500); }
  cur = null;
  if (!name) return;
  const el = new Audio(`assets/music/${name}.mp3`); el.loop = loop;
  const g = ac.createGain(); g.gain.value = 0.0001;
  ac.createMediaElementSource(el).connect(g); g.connect(musicBus);
  el.play().catch(() => {});
  g.gain.setTargetAtTime(vol, now(), 1.0);
  cur = { name, el, g };
}
export function setVolume(v) { volume = v; if (master) master.gain.value = v; }
export function setReverb(amount) { if (verbSend) verbSend.gain.setTargetAtTime(amount, ac.currentTime, 0.5); }

const now = () => ac.currentTime;
function env(g, t, a, peak, dec) { g.gain.setValueAtTime(0.0001, t); g.gain.exponentialRampToValueAtTime(peak, t + a); g.gain.exponentialRampToValueAtTime(0.0001, t + a + dec); }
function noiseSrc(t, dur, rate = 1) { const s = ac.createBufferSource(); s.buffer = noise; s.playbackRate.value = rate; s.loop = true; s.start(t, Math.random()); s.stop(t + dur + 0.05); return s; }
function filt(type, f, q = 1) { const b = ac.createBiquadFilter(); b.type = type; b.frequency.value = f; b.Q.value = q; return b; }
function osc(type, f, t, dur) { const o = ac.createOscillator(); o.type = type; o.frequency.setValueAtTime(f, t); o.start(t); o.stop(t + dur + 0.05); return o; }
function chain(...nodes) { for (let i = 0; i < nodes.length - 1; i++) nodes[i].connect(nodes[i + 1]); return nodes[nodes.length - 1]; }
function panner(x = 0) { const p = ac.createStereoPanner(); p.pan.value = x; return p; }

const R = (a, b) => a + Math.random() * (b - a);

const buffers = {};
function loadSamples() {
  for (const [name, list] of Object.entries(SAMPLES)) {
    buffers[name] = [];
    for (const f of list) fetch(`assets/sfx/${f}.ogg`).then(r => r.ok ? r.arrayBuffer() : Promise.reject()).then(b => ac.decodeAudioData(b)).then(buf => buffers[name].push(buf)).catch(() => {});
  }
}
const SAMPLE_VOL = { step: 0.35, stepStone: 0.4, stepCave: 0.45, stepWood: 0.4, land: 0.7, clang: 0.8, block: 0.9, hitFlesh: 0.9, hitShell: 0.8, rock: 0.8, coin: 0.7, door: 0.8, chain: 0.7, item: 0.7, bell: 0.35, trap: 0.8, ui: 0.35 };

export function sfx(name, opts = {}) {
  if (!ac || ac.state !== 'running') return;
  const t = now() + 0.005, vol = opts.vol ?? 1;
  let out = sfxBus;
  if (opts.pan != null) { out = panner(Math.max(-1, Math.min(1, opts.pan))); out.connect(sfxBus); }
  const recorded = buffers[name];
  if (recorded && recorded.length) {
    const src = ac.createBufferSource(); src.buffer = recorded[(Math.random() * recorded.length) | 0];
    src.playbackRate.value = (opts.pitch || 1) * R(0.92, 1.08) * (name === 'stepCave' ? 0.85 : 1);
    const g = ac.createGain(); g.gain.value = vol * (SAMPLE_VOL[name] ?? 0.7);
    src.connect(g); g.connect(out); src.start(t);
    if (name !== 'hitFlesh') return;
  }
  const G = (v) => { const g = ac.createGain(); g.gain.value = v * vol; g.connect(out); return g; };
  switch (name) {
    case 'step': case 'stepStone': case 'stepCave': {
      const f = name === 'step' ? R(500, 900) : name === 'stepStone' ? R(1500, 2400) : R(900, 1400);
      const g = G(1); env(g, t, 0.005, name === 'step' ? 0.12 : 0.18, 0.09);
      chain(noiseSrc(t, 0.15, R(0.8, 1.2)), filt('bandpass', f, 1.2), g);
      if (name !== 'step') { const g2 = G(1); env(g2, t, 0.002, 0.08, 0.04); chain(noiseSrc(t, 0.06), filt('highpass', 3000), g2); }
      break;
    }
    case 'jump': { const g = G(1); env(g, t, 0.01, 0.06, 0.12); chain(noiseSrc(t, 0.2), filt('bandpass', 600, 0.8), g); break; }
    case 'land': { const g = G(1); env(g, t, 0.003, 0.3, 0.18); chain(noiseSrc(t, 0.25, 0.6), filt('lowpass', 500), g); break; }
    case 'dodge': { const g = G(1); env(g, t, 0.02, 0.15, 0.3); const n = noiseSrc(t, 0.4); const b = filt('bandpass', 400, 0.7); b.frequency.exponentialRampToValueAtTime(1200, t + 0.3); chain(n, b, g); break; }
    case 'swing': {
      const g = G(1); env(g, t, 0.04, 0.35, 0.22);
      const b = filt('bandpass', 700, 2.5); b.frequency.setValueAtTime(400, t); b.frequency.exponentialRampToValueAtTime(2400, t + 0.12); b.frequency.exponentialRampToValueAtTime(600, t + 0.28);
      chain(noiseSrc(t, 0.35, 1), b, g);
      break;
    }
    case 'hitFlesh': {
      const g = G(1); env(g, t, 0.002, 0.7, 0.18); chain(noiseSrc(t, 0.25, 0.5), filt('lowpass', 900), g);
      const o = osc('sine', 120, t, 0.2); o.frequency.exponentialRampToValueAtTime(50, t + 0.18); const g2 = G(1); env(g2, t, 0.002, 0.6, 0.15); o.connect(g2);
      break;
    }
    case 'hitShell': {
      const g = G(1); env(g, t, 0.001, 0.6, 0.12); chain(noiseSrc(t, 0.2, 1.5), filt('bandpass', 2200, 2), g);
      const o = osc('triangle', 300, t, 0.1); const g2 = G(1); env(g2, t, 0.001, 0.3, 0.08); o.connect(g2);
      break;
    }
    case 'clang': case 'block': {
      const base = name === 'block' ? R(380, 460) : R(700, 900);
      for (const [k, a] of [[1, 0.5], [2.76, 0.3], [5.4, 0.2], [8.93, 0.12]]) { const o = osc('sine', base * k, t, 1.2); const g = G(1); env(g, t, 0.001, a * 0.5, 0.9 / k + 0.15); o.connect(g); }
      const g = G(1); env(g, t, 0.001, 0.5, 0.05); chain(noiseSrc(t, 0.1, 2), filt('highpass', 2500), g);
      break;
    }
    case 'miss': { const g = G(1); env(g, t, 0.01, 0.15, 0.15); chain(noiseSrc(t, 0.2), filt('highpass', 1800), g); break; }
    case 'hurt': {
      const o = osc('sawtooth', R(140, 170), t, 0.4); o.frequency.exponentialRampToValueAtTime(90, t + 0.35);
      const f = filt('bandpass', 700, 3); const g = G(1); env(g, t, 0.02, 0.35, 0.3); chain(o, f, g);
      const g2 = G(1); env(g2, t, 0.002, 0.5, 0.15); chain(noiseSrc(t, 0.2, 0.5), filt('lowpass', 700), g2);
      break;
    }
    case 'growl': case 'roar': case 'shriek': case 'hiss': {
      const big = name === 'roar', dur = big ? 2.4 : name === 'growl' ? 0.9 : 0.6;
      const p = opts.pitch || 1;
      if (name === 'hiss') { const g = G(1); env(g, t, 0.05, 0.4, 0.6); chain(noiseSrc(t, 0.8, 1.2), filt('highpass', 3500, 0.7), g); break; }
      const o = osc('sawtooth', (big ? 55 : name === 'shriek' ? 420 : 95) * p, t, dur);
      o.frequency.setValueAtTime(o.frequency.value, t); o.frequency.linearRampToValueAtTime(o.frequency.value * (big ? 0.7 : 0.8), t + dur);
      const lfo = osc('sine', big ? 9 : 22, t, dur); const lg = ac.createGain(); lg.gain.value = big ? 6 : 12; lfo.connect(lg); lg.connect(o.frequency);
      const g = G(1); env(g, t, big ? 0.25 : 0.06, big ? 0.7 : 0.35, dur);
      const f1 = filt('bandpass', (big ? 400 : 700) * p, 4), f2 = filt('bandpass', (big ? 1100 : 1800) * p, 5);
      o.connect(f1); o.connect(f2); f1.connect(g); f2.connect(g);
      const g2 = G(1); env(g2, t, big ? 0.3 : 0.05, big ? 0.6 : 0.2, dur); chain(noiseSrc(t, dur + 0.2, 0.4), filt('lowpass', big ? 600 : 1500), g2);
      break;
    }
    case 'fire': {
      const d = opts.dur || 1.5;
      const g = G(1); g.gain.setValueAtTime(0.0001, t); g.gain.exponentialRampToValueAtTime(0.9, t + 0.15); g.gain.setValueAtTime(0.9, t + d - 0.3); g.gain.exponentialRampToValueAtTime(0.0001, t + d);
      chain(noiseSrc(t, d + 0.1, 0.35), filt('lowpass', 900), g);
      const g2 = G(1); env(g2, t, 0.1, 0.3, d); chain(noiseSrc(t, d + 0.1, 1), filt('bandpass', 2500, 0.5), g2);
      break;
    }
    case 'boom': { const g = G(1); env(g, t, 0.005, 1, 1.2); chain(noiseSrc(t, 1.4, 0.25), filt('lowpass', 400), g); const o = osc('sine', 60, t, 1); o.frequency.exponentialRampToValueAtTime(30, t + 0.9); const g2 = G(1); env(g2, t, 0.005, 0.8, 0.9); o.connect(g2); break; }
    case 'coin': for (let i = 0; i < 4; i++) { const tt = t + i * 0.05 + Math.random() * 0.02; for (const k of [1, 2.4]) { const o = osc('sine', R(2600, 3400) * k, tt, 0.25); const g = G(1); env(g, tt, 0.001, 0.12 / k, 0.2); o.connect(g); } } break;
    case 'item': [523, 659, 784, 1047].forEach((f, i) => { const tt = t + i * 0.07; const o = osc('triangle', f, tt, 0.4); const g = G(1); env(g, tt, 0.005, 0.18, 0.35); o.connect(g); }); break;
    case 'magic': for (let i = 0; i < 14; i++) { const tt = t + i * 0.035; const o = osc('sine', R(900, 2600), tt, 0.4); const g = G(1); env(g, tt, 0.01, 0.07, 0.35); o.connect(g); } break;
    case 'heal': [392, 494, 587, 784].forEach((f, i) => { const tt = t + i * 0.09; const o = osc('sine', f, tt, 0.8); const g = G(1); env(g, tt, 0.05, 0.15, 0.7); o.connect(g); }); break;
    case 'door': { const g = G(1); env(g, t, 0.05, 0.25, 0.8); const o = osc('sawtooth', 70, t, 0.9); o.frequency.linearRampToValueAtTime(110, t + 0.8); chain(o, filt('bandpass', 400, 6), g); chain(noiseSrc(t, 0.9, 0.3), filt('bandpass', 300, 2), g); break; }
    case 'chain': for (let i = 0; i < 8; i++) { const tt = t + i * 0.06 + Math.random() * 0.03; const o = osc('sine', R(1800, 3200), tt, 0.15); const g = G(1); env(g, tt, 0.001, 0.08, 0.1); o.connect(g); } break;
    case 'ui': { const o = osc('sine', 880, t, 0.08); const g = G(1); env(g, t, 0.002, 0.08, 0.06); o.connect(g); break; }
    case 'uiBack': { const o = osc('sine', 520, t, 0.08); const g = G(1); env(g, t, 0.002, 0.08, 0.06); o.connect(g); break; }
    case 'bad': { const o = osc('square', 140, t, 0.2); const g = G(1); env(g, t, 0.005, 0.07, 0.18); chain(o, filt('lowpass', 900), g); break; }
    case 'trap': { sfx('clang', opts); const g = G(1); env(g, t, 0.001, 0.6, 0.3); chain(noiseSrc(t, 0.35, 0.8), filt('bandpass', 1500, 1), g); break; }
    case 'arrow': { const g = G(1); env(g, t, 0.005, 0.25, 0.25); const b = filt('bandpass', 3000, 3); b.frequency.exponentialRampToValueAtTime(1200, t + 0.25); chain(noiseSrc(t, 0.3, 1.5), b, g); break; }
    case 'splash': { const g = G(1); env(g, t, 0.005, 0.5, 0.5); chain(noiseSrc(t, 0.6, 0.8), filt('bandpass', 1200, 0.8), g); break; }
    case 'rock': { const g = G(1); env(g, t, 0.003, 0.9, 0.8); chain(noiseSrc(t, 1, 0.3), filt('lowpass', 700), g); break; }
    case 'drip': { const o = osc('sine', R(1200, 2200), t, 0.2); o.frequency.exponentialRampToValueAtTime(R(500, 800), t + 0.12); const g = G(1); env(g, t, 0.002, 0.08, 0.12); o.connect(g); break; }
    case 'bell': for (const [k, a] of [[1, 0.4], [2.0, 0.2], [2.4, 0.15], [3.0, 0.1]]) { const o = osc('sine', 330 * k, t, 4); const g = G(1); env(g, t, 0.003, a * 0.4, 3.5 / k); o.connect(g); } break;
    case 'victory': [262, 330, 392, 523, 392, 523, 659, 784].forEach((f, i) => { const tt = t + i * 0.18; for (const k of [1, 1.5]) { const o = osc('triangle', f * k, tt, 0.6); const g = G(1); env(g, tt, 0.01, 0.13 / k, 0.5); o.connect(g); } }); break;
    case 'death': { const o = osc('sawtooth', 220, t, 2.5); o.frequency.exponentialRampToValueAtTime(55, t + 2.4); const g = G(1); env(g, t, 0.1, 0.25, 2.3); chain(o, filt('lowpass', 600), g); break; }
  }
}

// ---------- ambience ----------
let amb = [];
let ambTimer = null;
export function ambience(kind) {
  if (!ac) return;
  for (const n of amb) { try { n.g.gain.setTargetAtTime(0.0001, now(), 0.6); n.s.stop(now() + 2); } catch (e) {} }
  amb = [];
  clearInterval(ambTimer);
  if (!kind) return;
  const bed = (rate, type, f, q, level) => {
    const s = ac.createBufferSource(); s.buffer = noise; s.loop = true; s.playbackRate.value = rate;
    const b = filt(type, f, q), g = ac.createGain(); g.gain.value = 0.0001;
    const lfo = ac.createOscillator(); lfo.frequency.value = 0.07 + Math.random() * 0.05; const lg = ac.createGain(); lg.gain.value = f * 0.4; lfo.connect(lg); lg.connect(b.frequency); lfo.start();
    s.connect(b); b.connect(g); g.connect(ambBus); s.start();
    g.gain.setTargetAtTime(level, now(), 1.5);
    amb.push({ s, g });
  };
  if (kind === 'meadow' || kind === 'village') {
    bed(0.5, 'bandpass', 500, 0.6, 0.12); bed(1, 'highpass', 3000, 0.3, 0.02);
    setReverb(0.08);
    ambTimer = setInterval(() => {
      if (Math.random() < 0.6) bird(Math.random() * 1.6 - 0.8);
      if (kind === 'village' && Math.random() < 0.15) sfx('bell', { vol: 0.25, pan: 0.5 });
    }, 1400);
  } else if (kind === 'cave' || kind === 'lair') {
    bed(0.25, 'lowpass', 160, 0.7, kind === 'lair' ? 0.35 : 0.22); bed(0.6, 'bandpass', 900, 3, 0.03);
    setReverb(0.6);
    ambTimer = setInterval(() => { if (Math.random() < 0.5) sfx('drip', { vol: 0.5, pan: Math.random() * 2 - 1 }); }, 900);
  }
}
function bird(pan) {
  const t = now() + 0.02, base = R(2600, 4200), notes = 2 + Math.floor(Math.random() * 5);
  for (let i = 0; i < notes; i++) {
    const tt = t + i * R(0.08, 0.16);
    const o = ac.createOscillator(); o.type = 'sine';
    o.frequency.setValueAtTime(base * R(0.85, 1.15), tt); o.frequency.exponentialRampToValueAtTime(base * R(0.7, 1.4), tt + 0.07);
    const g = ac.createGain(); env(g, tt, 0.01, 0.03, 0.07);
    const pn = panner(pan); pn.connect(ambBus);
    o.connect(g); g.connect(pn); o.start(tt); o.stop(tt + 0.12);
  }
}
