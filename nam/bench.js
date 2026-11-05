// bench.js — Web Worker. Runs a WaveNet of a given shape on noise, sample by sample in 64-sample
// buffers like a player would, and reports how many seconds of audio it does per second.
// The output sound is not a real amp (weights are random); only the amount of work matches.

function makeLayerArray(a, inSize) {
  const { C, B, K, D } = a;
  const layers = [];
  for (let i = 0; i < K.length; i++) {
    const span = (K[i] - 1) * D[i] + 1;
    let R = 1; while (R < span + 64) R <<= 1;
    layers.push({
      K: K[i], d: D[i], R, mask: R - 1, pos: 0,
      hist: new Float32Array(R * C),
      w: Float32Array.from({ length: B * C * K[i] }, () => (Math.random() - 0.5) * 0.2),
      bias: new Float32Array(B), mix: Float32Array.from({ length: B }, () => Math.random() - 0.5),
      w1: Float32Array.from({ length: C * B }, () => (Math.random() - 0.5) * 0.2),
    });
  }
  return {
    C, B, layers, inSize,
    rech: Float32Array.from({ length: C * inSize }, () => Math.random() - 0.5),
    x: new Float32Array(C), z: new Float32Array(B), head: new Float32Array(B),
    headK: a.headK || 1, headHist: new Float32Array(B * 16), headPos: 0,
    headW: Float32Array.from({ length: B * (a.headK || 1) }, () => (Math.random() - 0.5) * 0.1),
  };
}

function step(net, input, cond) {
  const { C, B, x, z, head } = net;
  for (let c = 0; c < C; c++) { let s = 0; for (let i = 0; i < net.inSize; i++) s += net.rech[c * net.inSize + i] * input[i]; x[c] = s; }
  head.fill(0);
  for (const L of net.layers) {
    const { hist, w, K, d, mask } = L;
    L.pos = (L.pos + 1) & mask;
    const base = L.pos * C;
    for (let c = 0; c < C; c++) hist[base + c] = x[c];
    for (let b = 0; b < B; b++) {
      let s = L.bias[b] + L.mix[b] * cond;
      const wb = b * C * K;
      for (let k = 0; k < K; k++) {
        const row = ((L.pos - d * (K - 1 - k)) & mask) * C;
        for (let c = 0; c < C; c++) s += w[wb + c * K + k] * hist[row + c];
      }
      s = s > 0 ? s : 0.01 * s;
      z[b] = s; head[b] += s;
    }
    for (let c = 0; c < C; c++) { let s = x[c]; for (let b = 0; b < B; b++) s += L.w1[c * B + b] * z[b]; x[c] = s; }
  }
  // head: conv over the summed skip path (kernel 16 for A2, 1x1 for A1)
  const hk = net.headK;
  net.headPos = (net.headPos + 1) & 15;
  for (let b = 0; b < B; b++) net.headHist[net.headPos * B + b] = head[b];
  let y = 0;
  for (let k = 0; k < hk; k++) { const row = ((net.headPos - k) & 15) * B; for (let b = 0; b < B; b++) y += net.headW[k * B + b] * net.headHist[row + b]; }
  return y;
}

function run(spec, seconds, sr) {
  const nets = []; let inSize = 1;
  for (const a of spec.arrays) { nets.push(makeLayerArray(a, inSize)); inSize = a.C; }
  const total = Math.round(seconds * sr), buf = 64;
  const inp = new Float32Array(1);
  let sink = 0;
  const t0 = performance.now();
  for (let n = 0; n < total; n += buf) {
    for (let i = 0; i < buf; i++) {
      const s = Math.sin((n + i) * 0.0287) * 0.5 + (Math.random() - 0.5) * 0.01;
      inp[0] = s;
      let v = s, prev = inp;
      for (const net of nets) { v = step(net, prev, s); prev = net.x; }
      sink += v;
    }
  }
  const ms = performance.now() - t0;
  return { audioSec: total / sr, wallSec: ms / 1000, sink };
}

self.onmessage = e => {
  const { id, spec, seconds = 1, sr = 48000 } = e.data;
  try {
    run(spec, 0.1, sr); // warm up the JIT
    const r = run(spec, seconds, sr);
    self.postMessage({ id, ok: true, rtf: r.audioSec / r.wallSec, ...r });
  } catch (err) {
    self.postMessage({ id, ok: false, error: String(err && err.message || err) });
  }
};
