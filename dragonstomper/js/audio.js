// TIA-flavoured sound: square and "buzz" tones at the frequencies the 2600 can make
// (30 kHz / (AUDF + 1) / divider) and a 5-bit poly-noise, all through Web Audio.
let ac = null, master = null, noiseBuf = null, muted = false;
try { muted = localStorage.getItem('ds3d-mute') === '1'; } catch (e) {}

export function audioStart() {
  if (ac) { if (ac.state === 'suspended') ac.resume(); return; }
  const AC = window.AudioContext || window.webkitAudioContext;
  if (!AC) return;
  ac = new AC();
  master = ac.createGain(); master.gain.value = muted ? 0 : 0.35; master.connect(ac.destination);
  // poly-5 xor poly-4 style noise, like AUDC 8
  const n = ac.sampleRate; noiseBuf = ac.createBuffer(1, n, n);
  const d = noiseBuf.getChannelData(0);
  let p5 = 0x1f, p4 = 0xf, v = 0;
  for (let i = 0; i < n; i++) {
    if (i % 3 === 0) {
      const b5 = ((p5 >> 4) ^ (p5 >> 2)) & 1; p5 = ((p5 << 1) | b5) & 0x1f;
      const b4 = ((p4 >> 3) ^ (p4 >> 2)) & 1; p4 = ((p4 << 1) | b4) & 0xf;
      v = (p5 ^ p4) & 1 ? 1 : -1;
    }
    d[i] = v;
  }
}
export function toggleMute() {
  muted = !muted;
  try { localStorage.setItem('ds3d-mute', muted ? '1' : '0'); } catch (e) {}
  if (master) master.gain.value = muted ? 0 : 0.35;
  return muted;
}
export const isMuted = () => muted;

const tia = (f, div = 2) => 31400 / ((f + 1) * div); // AUDF -> Hz

function tone(freq, t0, dur, vol = 0.3, type = 'square', slideTo = null) {
  const o = ac.createOscillator(), g = ac.createGain();
  o.type = type; o.frequency.setValueAtTime(freq, t0);
  if (slideTo) o.frequency.exponentialRampToValueAtTime(slideTo, t0 + dur);
  g.gain.setValueAtTime(vol, t0); g.gain.setValueAtTime(vol, t0 + dur * 0.8); g.gain.linearRampToValueAtTime(0, t0 + dur);
  o.connect(g); g.connect(master); o.start(t0); o.stop(t0 + dur + 0.02);
}
function noise(t0, dur, vol = 0.3, rate = 1, cut = 4000, slide = null) {
  const s = ac.createBufferSource(), g = ac.createGain(), f = ac.createBiquadFilter();
  s.buffer = noiseBuf; s.loop = true; s.playbackRate.value = rate;
  f.type = 'lowpass'; f.frequency.setValueAtTime(cut, t0);
  if (slide) f.frequency.exponentialRampToValueAtTime(slide, t0 + dur);
  g.gain.setValueAtTime(vol, t0); g.gain.exponentialRampToValueAtTime(0.001, t0 + dur);
  s.connect(f); f.connect(g); g.connect(master); s.start(t0); s.stop(t0 + dur + 0.02);
}

export function sfx(name) {
  if (!ac || muted) return;
  const t = ac.currentTime + 0.01;
  switch (name) {
    case 'blip': tone(tia(8), t, 0.05, 0.2); break;
    case 'tick': tone(tia(12), t, 0.03, 0.15); break;
    case 'text': tone(tia(5), t, 0.03, 0.12); break;
    case 'bad': tone(tia(31, 6), t, 0.15, 0.25); break;
    case 'step': noise(t, 0.04, 0.05, 0.6, 900); break;
    case 'encounter': [20, 18, 24, 28].forEach((f, i) => tone(tia(f, 6), t + i * 0.09, 0.08, 0.25)); break;
    case 'swing': noise(t, 0.12, 0.25, 1.5, 6000, 800); break;
    case 'hit': noise(t, 0.18, 0.4, 0.7, 3000, 300); tone(tia(28, 6), t, 0.12, 0.2, 'square', 60); break;
    case 'hurt': tone(tia(18, 6), t, 0.25, 0.3, 'sawtooth', 50); noise(t, 0.2, 0.3, 0.5, 1500); break;
    case 'kill': [12, 10, 8, 6, 4].forEach((f, i) => tone(tia(f), t + i * 0.06, 0.06, 0.25)); break;
    case 'coin': tone(tia(4), t, 0.06, 0.2); tone(tia(2), t + 0.07, 0.12, 0.2); break;
    case 'item': [10, 7, 5, 3].forEach((f, i) => tone(tia(f), t + i * 0.07, 0.07, 0.22)); break;
    case 'magic': for (let i = 0; i < 10; i++) tone(tia(2 + (i * 7) % 13), t + i * 0.04, 0.04, 0.15); break;
    case 'death': tone(tia(10, 6), t, 1.4, 0.3, 'square', 30); break;
    case 'trap': tone(tia(30, 6), t, 0.4, 0.3, 'square', 40); noise(t, 0.3, 0.3, 0.4); break;
    case 'roar': noise(t, 1.2, 0.6, 0.35, 900, 120); tone(55, t, 1.0, 0.25, 'sawtooth', 35); break;
    case 'fire': noise(t, 0.6, 0.35, 0.8, 2500, 300); break;
    case 'boom': noise(t, 0.7, 0.6, 0.25, 600, 60); break;
    case 'arrow': noise(t, 0.15, 0.2, 2, 8000, 2000); break;
    case 'door': tone(tia(25, 6), t, 0.2, 0.25); tone(tia(31, 6), t + 0.2, 0.3, 0.25); break;
    case 'win': [16, 12, 9, 7, 9, 7, 5, 4].forEach((f, i) => tone(tia(f), t + i * 0.13, 0.12, 0.25)); break;
    case 'part': [9, 7, 5, 9, 7, 5, 4].forEach((f, i) => tone(tia(f), t + i * 0.1, 0.09, 0.22)); break;
  }
}

// a slow drone per area, like the 2600 game's ambient hum
let drone = null;
export function ambience(kind) {
  if (!ac) return;
  if (drone) { drone.g.gain.linearRampToValueAtTime(0, ac.currentTime + 0.5); drone.o.stop(ac.currentTime + 0.6); drone = null; }
  if (!kind) return;
  const o = ac.createOscillator(), g = ac.createGain(), f = ac.createBiquadFilter();
  o.type = 'square'; o.frequency.value = kind === 'cave' ? 41 : kind === 'boss' ? 49 : 0;
  if (!o.frequency.value) return;
  f.type = 'lowpass'; f.frequency.value = 180;
  g.gain.value = 0; g.gain.linearRampToValueAtTime(kind === 'boss' ? 0.12 : 0.06, ac.currentTime + 2);
  o.connect(f); f.connect(g); g.connect(master); o.start();
  drone = { o, g };
}
