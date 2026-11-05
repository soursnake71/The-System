// engine.js — runs Neural Amp Modeler models in JavaScript, one sample at a time.
// A port of NeuralAmpModelerCore (NAM/wavenet/model.cpp, conv1d.cpp, dsp.cpp, lstm.cpp):
// same weight order, same layer maths. Supports WaveNet (A1 and A2, gated, head 1x1, post-stack
// head), LSTM and the SlimmableContainer that A2 files use to hold their Full and Lite sizes.
// Works in a page, a Worker or an AudioWorklet.

function activation(cfg) {
  const a = typeof cfg === 'string' ? { type: cfg } : (cfg || { type: 'Tanh' });
  const t = String(a.type || a.name || 'Tanh').toLowerCase();
  switch (t) {
    case 'tanh': case 'fasttanh': return x => Math.tanh(x);
    case 'relu': return x => x > 0 ? x : 0;
    case 'leakyrelu': { const s = a.negative_slope ?? 0.01; return x => x > 0 ? x : s * x; }
    case 'sigmoid': return x => 1 / (1 + Math.exp(-x));
    case 'hardtanh': return x => x < -1 ? -1 : x > 1 ? 1 : x;
    case 'leakyhardtanh': {
      const lo = a.min_val ?? -1, hi = a.max_val ?? 1, ls = a.min_slope ?? 0.01, hs = a.max_slope ?? 0.01;
      return x => x < lo ? lo + ls * (x - lo) : x > hi ? hi + hs * (x - hi) : x;
    }
    case 'silu': case 'swish': return x => x / (1 + Math.exp(-x));
    case 'softsign': return x => x / (1 + Math.abs(x));
    case 'hardswish': return x => x <= -3 ? 0 : x >= 3 ? x : x * (x + 3) / 6;
    default: throw new Error(`Activation "${a.type}" is not supported here`);
  }
}

class Reader {
  constructor(w) { this.w = w; this.i = 0; }
  take(n) { if (this.i + n > this.w.length) throw new Error('The file has fewer weights than its layers need'); const a = Float32Array.from(this.w.slice(this.i, this.i + n)); this.i += n; return a; }
  one() { return this.take(1)[0]; }
}

// Causal dilated convolution with its own history ring. Weight layout: out, in, tap; then bias.
class Conv {
  constructor(cin, cout, k, dil, bias, r) {
    Object.assign(this, { cin, cout, k, dil });
    this.w = r.take(cin * cout * k);
    this.b = bias ? r.take(cout) : new Float32Array(cout);
    let R = 1; while (R < (k - 1) * dil + 1) R <<= 1;
    this.R = R; this.mask = R - 1; this.pos = 0;
    this.hist = new Float32Array(R * cin);
    this.out = new Float32Array(cout);
  }
  push(x) { this.pos = (this.pos + 1) & this.mask; this.hist.set(x.length === this.cin ? x : x.subarray(0, this.cin), this.pos * this.cin); }
  // output for the current sample (call push first). tap k reads dil*(K-1-k) samples back
  run() {
    const { cin, cout, k, dil, w, b, hist, mask, out } = this;
    for (let o = 0; o < cout; o++) {
      let s = b[o]; const wo = o * cin * k;
      for (let t = 0; t < k; t++) {
        const row = ((this.pos - dil * (k - 1 - t)) & mask) * cin;
        for (let c = 0; c < cin; c++) s += w[wo + c * k + t] * hist[row + c];
      }
      out[o] = s;
    }
    return out;
  }
  reset() { this.hist.fill(0); }
}
// 1x1 convolution. Weight layout: out, in; then bias
function dense(cin, cout, bias, r) {
  const w = r.take(cin * cout), b = bias ? r.take(cout) : new Float32Array(cout), out = new Float32Array(cout);
  return { cin, cout, run(x) { for (let o = 0; o < cout; o++) { let s = b[o]; const wo = o * cin; for (let c = 0; c < cin; c++) s += w[wo + c] * x[c]; out[o] = s; } return out; } };
}

function layerArray(la, r) {
  const C = la.channels, B = la.bottleneck ?? C, n = la.dilations.length;
  const K = la.kernel_sizes || Array(n).fill(la.kernel_size);
  const acts = Array.isArray(la.activation) ? la.activation : Array(n).fill(la.activation);
  let gm = la.gating_mode != null ? la.gating_mode : (la.gated ? 'gated' : 'none');
  gm = Array.isArray(gm) ? gm : Array(n).fill(gm);
  const sec = Array.isArray(la.secondary_activation) ? la.secondary_activation : Array(n).fill(la.secondary_activation);
  if ((la.groups_input || 1) !== 1 || (la.groups_input_mixin || 1) !== 1) throw new Error('Grouped convolutions are not supported here');
  for (const f of ['conv_pre_film', 'conv_post_film', 'input_mixin_pre_film', 'input_mixin_post_film', 'activation_pre_film', 'activation_post_film', 'layer1x1_post_film', 'head1x1_post_film'])
    if (la[f] && la[f].active !== false && la[f] !== false) throw new Error('FiLM layers are not supported here');
  const l1 = la.layer1x1 ? la.layer1x1.active !== false : true;
  const h1 = la.head1x1 && la.head1x1.active ? la.head1x1.out_channels : 0;
  const cond = la.condition_size ?? 1;
  const rech = dense(la.input_size, C, false, r);
  const layers = [];
  for (let i = 0; i < n; i++) {
    const mode = String(gm[i] || 'none').toLowerCase();
    if (mode === 'blended') throw new Error('Blended gating is not supported here');
    const gated = mode !== 'none';
    const bout = gated ? 2 * B : B;
    const conv = new Conv(C, bout, K[i], la.dilations[i], true, r);
    const mix = dense(cond, bout, false, r);
    const l1x1 = l1 ? dense(B, C, true, r) : null;
    const h1x1 = h1 ? dense(B, h1, true, r) : null;
    layers.push({ conv, mix, l1x1, h1x1, gated, act: activation(acts[i]), gate: activation(sec[i] || 'Sigmoid') });
  }
  const headIn = h1 || B;
  let hk = 1, hd = 1, hb = !!la.head_bias, hs = la.head_size;
  if (la.head && typeof la.head === 'object') { hk = la.head.kernel_size; hd = la.head.head_dilation || 1; hb = !!la.head.bias; hs = la.head.out_channels; }
  const head = new Conv(headIn, hs, hk, hd, hb, r);
  return { C, B, layers, rech, head, headIn, z: new Float32Array(2 * B), v: new Float32Array(C), acc: new Float32Array(headIn) };
}

function wavenet(j) {
  const c = j.config, r = new Reader(j.weights);
  if (c.condition_dsp) throw new Error('Models with a condition DSP are not supported here');
  if ((c.in_channels ?? 1) !== 1) throw new Error('Only mono-input models are supported');
  const arrays = c.layers.map(la => layerArray(la, r));
  let post = null;
  if (c.head && typeof c.head === 'object' && Array.isArray(c.head.kernel_sizes)) {
    const h = c.head, act = activation(h.activation);
    let cin = h.in_channels ?? arrays.at(-1).head.cout;
    post = h.kernel_sizes.map((k, i) => { const cout = i === h.kernel_sizes.length - 1 ? h.out_channels : h.channels; const cv = new Conv(cin, cout, k, 1, true, r); cin = cout; return cv; });
    post.act = act;
  }
  const headScale = r.one();
  if (r.i !== j.weights.length) throw new Error(`Weight count mismatch: used ${r.i} of ${j.weights.length}`);
  const x1 = new Float32Array(1), scratch = new Float32Array(64);
  function sample(x) {
    x1[0] = x;
    let input = x1, headPrev = null;
    for (const A of arrays) {
      const { v, z, acc, B, C } = A;
      v.set(A.rech.run(input));
      if (headPrev) acc.set(headPrev); else acc.fill(0);
      for (const L of A.layers) {
        L.conv.push(v);
        const co = L.conv.run(), mo = L.mix.run(x1);
        const n = co.length;
        for (let o = 0; o < n; o++) z[o] = co[o] + mo[o];
        if (L.gated) for (let b = 0; b < B; b++) z[b] = L.act(z[b]) * L.gate(z[b + B]);
        else for (let b = 0; b < B; b++) z[b] = L.act(z[b]);
        const zb = z.subarray(0, B);
        if (L.h1x1) { const h = L.h1x1.run(zb); for (let i = 0; i < h.length; i++) acc[i] += h[i]; }
        else for (let b = 0; b < B; b++) acc[b] += z[b];
        if (L.l1x1) { const l = L.l1x1.run(zb); for (let ch = 0; ch < C; ch++) v[ch] += l[ch]; }
      }
      A.head.push(acc);
      headPrev = A.head.run();
      input = v;
    }
    if (!post) return headScale * headPrev[0];
    let cur = scratch.subarray(0, headPrev.length);
    for (let i = 0; i < headPrev.length; i++) cur[i] = headScale * headPrev[i];
    for (const cv of post) {
      for (let i = 0; i < cur.length; i++) cur[i] = post.act(cur[i]);
      cv.push(cur); cur = cv.run();
    }
    return cur[0];
  }
  let rf = 1;
  for (const A of arrays) { for (const L of A.layers) rf += (L.conv.k - 1) * L.conv.dil; rf += (A.head.k - 1) * A.head.dil; }
  if (post) for (const cv of post) rf += cv.k - 1;
  function reset() { for (const A of arrays) { for (const L of A.layers) L.conv.reset(); A.head.reset(); } if (post) post.forEach(cv => cv.reset()); }
  return { sample, reset, receptiveField: rf };
}

function lstm(j) {
  const c = j.config, r = new Reader(j.weights);
  const H = c.hidden_size, layers = [];
  let I = c.input_size ?? 1;
  for (let l = 0; l < c.num_layers; l++) {
    const w = r.take(4 * H * (I + H)), b = r.take(4 * H), h0 = r.take(H), c0 = r.take(H);
    layers.push({ I, w, b, h0, c0, xh: new Float32Array(I + H), c: new Float32Array(H), g: new Float32Array(4 * H) });
    I = H;
  }
  const hw = r.take(H), hb = r.one();
  if (r.i !== j.weights.length) throw new Error(`Weight count mismatch: used ${r.i} of ${j.weights.length}`);
  const sig = x => 1 / (1 + Math.exp(-x));
  function reset() { for (const L of layers) { L.xh.fill(0); L.xh.set(L.h0, L.I); L.c.set(L.c0); } }
  reset();
  function sample(x) {
    let inp = null;
    for (const L of layers) {
      const { I, w, b, xh, c, g } = L, n = I + H;
      if (inp) xh.set(inp, 0); else xh[0] = x;
      for (let o = 0; o < 4 * H; o++) { let s = b[o]; const wo = o * n; for (let i = 0; i < n; i++) s += w[wo + i] * xh[i]; g[o] = s; }
      for (let i = 0; i < H; i++) c[i] = sig(g[H + i]) * c[i] + sig(g[i]) * Math.tanh(g[2 * H + i]);
      for (let i = 0; i < H; i++) xh[I + i] = sig(g[3 * H + i]) * Math.tanh(c[i]);
      inp = xh.subarray(I, I + H);
    }
    let y = hb; for (let i = 0; i < H; i++) y += hw[i] * inp[i];
    return y;
  }
  return { sample, reset, receptiveField: 4096 };
}

// The playable sizes of a file: [{label, json}] largest first
export function namSizes(j) {
  if (j.architecture === 'SlimmableContainer') {
    return [...j.config.submodels].sort((a, b) => b.max_value - a.max_value).map(s => {
      const la = s.model.config?.layers?.[0];
      const ch = la?.channels;
      const label = Array.isArray(la?.kernel_sizes) ? (ch === 8 ? 'Full' : ch === 3 ? 'Lite' : `${ch} ch`) : (s.model.architecture + (ch ? ` ${ch} ch` : ''));
      return { label, json: s.model };
    });
  }
  return [{ label: 'Model', json: j }];
}

// Build a runnable model from one network's JSON
export function buildModel(j) {
  let m;
  if (j.architecture === 'WaveNet') m = wavenet(j);
  else if (j.architecture === 'LSTM') m = lstm(j);
  else throw new Error(`${j.architecture} models are not supported for listening yet`);
  m.sampleRate = j.sample_rate || 48000;
  m.loudness = j.metadata?.loudness ?? null;
  // settle the history on silence, the way NAM prewarms a model
  m.prewarm = () => { m.reset(); for (let i = 0; i < m.receptiveField; i++) m.sample(0); };
  m.prewarm();
  m.process = (inp, out) => { for (let i = 0; i < inp.length; i++) out[i] = m.sample(inp[i]); };
  return m;
}
