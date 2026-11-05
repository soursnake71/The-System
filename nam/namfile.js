// namfile.js — reads Neural Amp Modeler (.nam) files and works out what each one costs to run.
//
// File layout (from NeuralAmpModelerCore and the neural-amp-modeler trainer):
//   { version, architecture, config, weights, sample_rate?, metadata? }
//   architecture "WaveNet"            A1 (standard / lite / feather / nano / custom) or a single A2 size
//   architecture "SlimmableContainer" A2: config.submodels = [{max_value, model}, ...], one per size
//                                     (3-channel A2-Lite at max_value 0.5, 8-channel A2-Full at 1.0)
//   architecture "LSTM" | "ConvNet" | "Linear" | "Sequential"
//   metadata: loudness, gain, input_level_dbu, output_level_dbu, gear_make, gear_model, gear_type,
//             tone_type, modeled_by, training.validation_esr, date
//
// Cost model. Work per sample = multiply-adds of every convolution plus a fixed overhead per layer
// (activation, residual add, bookkeeping). Two published figures pin it down:
//   - an A2-Full costs about 3.2x an A2-Lite (64 Full vs ~200 Lite in real time on an Apple M-series core)
//   - three A2-Full cost about what two A1-standard cost (A2 has a fast path in NAM core 0.5.2+)
// and one absolute figure sets the scale: an A2-Lite uses 50% of a 600 MHz Cortex-M7 at 48 kHz.
// Everything is an estimate. A real player on a real machine is the final word.

export const LAYER_OVERHEAD = 123;   // work units per layer per sample, besides the multiply-adds
export const A2_FAST_FACTOR = 0.72;  // A2 runs on the specialised fast path
// work units per second one core can do (an A2-Lite at 48 kHz is ~158 M units/s)
export const UNITS_PER_M7_MHZ = 0.525e6; // 600 MHz M7 -> 315 M units/s -> A2-Lite at 50%

const arr = (v, n, d) => Array.isArray(v) ? v : Array(n).fill(v ?? d);

function activationName(a) {
  if (a == null) return '';
  if (typeof a === 'string') return a;
  if (Array.isArray(a)) return activationName(a[0]);
  return a.type || a.name || '';
}

// Multiply-adds and layer count for one WaveNet config.
export function wavenetShape(config) {
  const arrays = [];
  let macs = 0, layers = 0, rf = 1;
  let a2Style = false;
  for (const la of config.layers || []) {
    const C = la.channels;
    const B = la.bottleneck ?? C;
    const n = (la.dilations || []).length;
    const K = la.kernel_sizes || arr(la.kernel_size, n, 3);
    const D = la.dilations || [];
    const gm = Array.isArray(la.gating_mode) ? la.gating_mode : arr(la.gating_mode ?? (la.gated ? 'gated' : 'none'), n);
    const g = la.groups_input || 1;
    const l1x1 = la.layer1x1 ? la.layer1x1.active !== false : true;
    const h1x1 = la.head1x1 && la.head1x1.active ? la.head1x1.out_channels : 0;
    const condSize = la.condition_size ?? 1;
    macs += (la.input_size ?? 1) * C; // rechannel
    for (let i = 0; i < n; i++) {
      const bout = (gm[i] && gm[i] !== 'none') ? 2 * B : B;
      macs += (C * bout * K[i]) / g;        // dilated conv
      macs += condSize * bout;               // input mixin
      if (l1x1) macs += B * C;               // layer 1x1 back to the residual
      if (h1x1) macs += B * h1x1;            // head 1x1
      rf += (K[i] - 1) * D[i];
    }
    layers += n;
    const headIn = h1x1 || B;
    if (la.head && typeof la.head === 'object') {
      macs += headIn * la.head.out_channels * (la.head.kernel_size || 1);
      rf += (la.head.kernel_size || 1) - 1;
      a2Style = true;
    } else if (la.head_size) {
      macs += headIn * la.head_size;
    }
    if (Array.isArray(la.kernel_sizes)) a2Style = true;
    arrays.push({ C, B, n, K, D, gated: gm.some(x => x && x !== 'none'), act: activationName(la.activation) });
  }
  if (config.head && typeof config.head === 'object' && Array.isArray(config.head.kernel_sizes)) {
    const h = config.head;
    let prev = h.in_channels ?? arrays.at(-1)?.B ?? 1;
    h.kernel_sizes.forEach((k, i) => {
      const out = i === h.kernel_sizes.length - 1 ? h.out_channels : h.channels;
      macs += prev * out * k; prev = out; rf += k - 1;
    });
    layers += h.kernel_sizes.length;
  }
  const leaky = arrays.length && arrays.every(a => /leaky/i.test(a.act));
  return { arrays, macs, layers, rf, a2Style: a2Style && leaky };
}

function lstmShape(c) {
  let macs = 0, inp = c.input_size ?? 1;
  for (let i = 0; i < c.num_layers; i++) { macs += 4 * c.hidden_size * (inp + c.hidden_size) + 10 * c.hidden_size; inp = c.hidden_size; }
  macs += c.hidden_size;
  return { macs, layers: c.num_layers * 4, rf: 0, a2Style: false };
}

function convnetShape(c) {
  const d = c.dilations || [];
  let macs = 0, inp = 1;
  for (const _ of d) { macs += inp * c.channels * 2 + c.channels * (c.batchnorm ? 2 : 0); inp = c.channels; }
  macs += c.channels;
  return { macs, layers: d.length, rf: d.reduce((s, x) => s + x, 1), a2Style: false };
}

// Work units per sample for one shape
export function unitsOf(shape, fastPath = true) {
  const u = shape.macs + LAYER_OVERHEAD * shape.layers;
  return shape.a2Style && fastPath ? u * A2_FAST_FACTOR : u;
}

function a1Size(shape) {
  const a = shape.arrays;
  if (a.length !== 2) return null;
  const std = a.every(x => x.n === 10 && x.K.every(k => k === 3) && /tanh/i.test(x.act));
  if (!std) return null;
  const key = `${a[0].C}/${a[1].C}`;
  return { '16/8': 'standard', '12/6': 'lite', '8/4': 'feather', '4/2': 'nano' }[key] || null;
}

function a2Size(shape) {
  if (!shape.a2Style || shape.arrays.length !== 1) return null;
  const C = shape.arrays[0].C;
  return C === 8 ? 'Full' : C === 3 ? 'Lite' : `${C}-ch`;
}

// One runnable model (a single network) -> variant record
function describeNet(j) {
  const arch = j.architecture;
  const c = j.config || {};
  let shape, family = arch, label = arch;
  if (arch === 'WaveNet') {
    shape = wavenetShape(c);
    const a1 = a1Size(shape), a2 = a2Size(shape);
    if (a2) { family = 'A2'; label = `A2-${a2}`; }
    else if (a1) { family = 'A1'; label = `A1-${a1}`; }
    else { family = 'WaveNet'; label = `WaveNet (custom, ${shape.arrays.map(x => x.C).join('/')} ch)`; }
  } else if (arch === 'LSTM') { shape = lstmShape(c); label = `LSTM ${c.num_layers}x${c.hidden_size}`; }
  else if (arch === 'ConvNet') { shape = convnetShape(c); label = `ConvNet ${c.channels} ch`; }
  else if (arch === 'Linear') { shape = { macs: c.receptive_field || 0, layers: 1, rf: c.receptive_field || 0, a2Style: false }; label = 'Linear (IR-like)'; }
  else throw new Error(`Unknown architecture "${arch}"`);
  const params = Array.isArray(j.weights) ? j.weights.length : null;
  return { label, family, shape, params, sampleRate: j.sample_rate || 48000 };
}

// Slimmable WaveNet (one network that can be cut down): one variant per allowed channel count
function slimVariants(j) {
  const idx = (j.config.layers || []).findIndex(l => l.slimmable && l.slimmable.kwargs);
  const allowed = idx >= 0 ? j.config.layers[idx].slimmable.kwargs.allowed_channels : null;
  if (!allowed) return null;
  return [...allowed].sort((a, b) => b - a).map(ch => {
    const cfg = JSON.parse(JSON.stringify(j.config));
    cfg.layers[idx].channels = ch; cfg.layers[idx].bottleneck = ch;
    const v = describeNet({ ...j, config: cfg, weights: null });
    v.label = `Slimmable ${ch} ch`;
    return v;
  });
}

function pickMeta(...ms) {
  const out = {};
  for (const m of ms) if (m && typeof m === 'object') for (const [k, v] of Object.entries(m)) if (v != null && out[k] == null) out[k] = v;
  return out;
}

// Parse a .nam JSON (object or text). Returns { kind, label, variants[], meta, version }
export function analyzeNam(input) {
  const j = typeof input === 'string' ? JSON.parse(input) : input;
  if (!j || typeof j !== 'object' || !j.architecture || !j.config) throw new Error('Not a .nam file (no architecture/config)');
  let variants, kind, metas = [j.metadata];
  if (j.architecture === 'SlimmableContainer') {
    const subs = [...(j.config.submodels || [])].sort((a, b) => b.max_value - a.max_value);
    variants = subs.map(s => describeNet(s.model));
    metas.push(...subs.map(s => s.model.metadata));
    const a2 = variants.length && variants.every(v => v.family === 'A2');
    kind = a2 ? 'A2' : 'Slimmable';
  } else if (j.architecture === 'Sequential') {
    const parts = (j.config.models || []).map(describeNet);
    const shape = { macs: 0, layers: 0, rf: 0, a2Style: false };
    let units = 0;
    for (const p of parts) { units += unitsOf(p.shape); shape.rf += p.shape.rf; }
    variants = [{ label: `Chain of ${parts.length}`, family: 'Sequential', shape, unitsOverride: units, params: null, sampleRate: j.sample_rate || 48000 }];
    kind = 'Sequential';
  } else {
    variants = slimVariants(j) || [describeNet(j)];
    kind = variants[0].family;
  }
  const meta = pickMeta(...metas);
  const training = meta.training || {};
  return {
    kind,
    label: kind === 'A2' ? `A2 (${variants.map(v => v.label.replace('A2-', '')).join(' + ')})` : variants.length > 1 ? `${kind} (${variants.length} sizes)` : variants[0].label,
    version: j.version,
    variants,
    meta: {
      esr: training.validation_esr ?? meta.validation_esr ?? null,
      loudness: meta.loudness ?? null,
      gain: meta.gain ?? null,
      inputDbu: meta.input_level_dbu ?? null,
      outputDbu: meta.output_level_dbu ?? null,
      make: meta.gear_make || '', model: meta.gear_model || '', gearType: meta.gear_type || '', toneType: meta.tone_type || '',
      by: meta.modeled_by || '',
      date: meta.date ? `${meta.date.year}-${String(meta.date.month).padStart(2, '0')}-${String(meta.date.day).padStart(2, '0')}` : '',
    },
  };
}

export function variantUnits(v, fastPath = true) {
  return v.unitsOverride ?? unitsOf(v.shape, fastPath);
}

// Reference shapes (no file needed) so the device check works before anything is loaded
const A2_KS = [6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6];
const A2_DS = [1, 3, 7, 17, 41, 101, 239, 1, 3, 7, 17, 41, 101, 239, 1, 13, 1, 3, 7, 17, 41, 101, 239];
export function a2Config(ch) {
  return { layers: [{ input_size: 1, condition_size: 1, head: { out_channels: 1, kernel_size: 16, bias: true }, channels: ch, bottleneck: ch,
    kernel_sizes: A2_KS, dilations: A2_DS, activation: A2_KS.map(() => ({ type: 'LeakyReLU', negative_slope: 0.01 })),
    head1x1: { active: false, out_channels: 1, groups: 1 }, layer1x1: { active: true, groups: 1 }, gating_mode: A2_KS.map(() => 'none') }], head: null, head_scale: 0.01 };
}
export function a1Config(c1, c2) {
  const d = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512];
  return { layers: [
    { input_size: 1, condition_size: 1, head_size: c2, channels: c1, kernel_size: 3, dilations: d, activation: 'Tanh', gated: false, head_bias: false },
    { input_size: c1, condition_size: 1, head_size: 1, channels: c2, kernel_size: 3, dilations: d, activation: 'Tanh', gated: false, head_bias: true },
  ], head: null, head_scale: 0.02 };
}
export const REFERENCE = [
  { label: 'A2-Full', family: 'A2', shape: wavenetShape(a2Config(8)) },
  { label: 'A2-Lite', family: 'A2', shape: wavenetShape(a2Config(3)) },
  { label: 'A1-standard', family: 'A1', shape: wavenetShape(a1Config(16, 8)) },
  { label: 'A1-lite', family: 'A1', shape: wavenetShape(a1Config(12, 6)) },
  { label: 'A1-feather', family: 'A1', shape: wavenetShape(a1Config(8, 4)) },
  { label: 'A1-nano', family: 'A1', shape: wavenetShape(a1Config(4, 2)) },
].map(r => ({ ...r, sampleRate: 48000 }));

// Small machines. capacity = work units per second one core can spend on audio.
// mhzEq = speed in "600 MHz Cortex-M7" terms; ARM NEON / x86 SIMD cores get a per-clock factor.
// These are rough: per-clock factors come from typical float SIMD throughput, not NAM measurements,
// except the M7 (published A2-Lite figure) and Apple M-series (published 64 Full / ~200 Lite).
export const DEVICES = [
  { id: 'esp32s3', name: 'ESP32-S3 (240 MHz)', mhzEq: 120, note: 'Microcontroller with a slow float unit. Rough.' },
  { id: 'daisy', name: 'Daisy Seed / Cortex-M7 480 MHz', mhzEq: 480, note: 'Published: A2-Lite runs in real time.' },
  { id: 'm7', name: 'Cortex-M7 600 MHz (Teensy 4.x, ToneX One class)', mhzEq: 600, note: 'Published: A2-Lite at 50% CPU.' },
  { id: 'pizero2', name: 'Raspberry Pi Zero 2 W (A53 1 GHz)', mhzEq: 2000 },
  { id: 'pi3', name: 'Raspberry Pi 3B+ (A53 1.4 GHz)', mhzEq: 2800 },
  { id: 'phonelow', name: 'Budget Android phone (A55 ~2 GHz)', mhzEq: 4400 },
  { id: 'pi4', name: 'Raspberry Pi 4 (A72 1.8 GHz)', mhzEq: 6800 },
  { id: 'atom', name: 'Celeron N4020 / older Atom laptop', mhzEq: 7600 },
  { id: 'phonemid', name: 'Mid-range Android phone (A76/A78 class)', mhzEq: 13000 },
  { id: 'pi5', name: 'Raspberry Pi 5 / RK3588 (A76 2.4 GHz)', mhzEq: 15500 },
  { id: 'n100', name: 'Intel N100 mini PC', mhzEq: 19000 },
  { id: 'm1', name: 'Apple M1 (one core)', mhzEq: 48000 },
  { id: 'm4', name: 'Apple M2–M5 (one core)', mhzEq: 60000, note: 'Published: 64 A2-Full / ~200 A2-Lite.' },
].map(d => ({ ...d, capacity: d.mhzEq * UNITS_PER_M7_MHZ }));

// Smaller buffers mean more per-call overhead
export function bufferFactor(buf) {
  return buf <= 16 ? 1.3 : buf <= 32 ? 1.15 : buf <= 64 ? 1.05 : 1;
}

// CPU share one instance of a variant takes on a device (0..)
export function cpuShare(v, capacity, { buffer = 64, fastPath = true } = {}) {
  return variantUnits(v, fastPath) * v.sampleRate * bufferFactor(buffer) / capacity;
}
