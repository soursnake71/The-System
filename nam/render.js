// render.js — Web Worker: plays an audio clip through a NAM model faster or slower than real time,
// so any machine can audition any model (a slow one just takes longer).
import { buildModel } from './engine.js';

self.onmessage = e => {
  const { id, json, input, gain } = e.data;
  try {
    const m = buildModel(json);
    const out = new Float32Array(input.length), chunk = 4800;
    const x = new Float32Array(chunk), y = new Float32Array(chunk);
    for (let i = 0; i < input.length; i += chunk) {
      const n = Math.min(chunk, input.length - i);
      const xs = x.subarray(0, n), ys = y.subarray(0, n);
      for (let k = 0; k < n; k++) xs[k] = input[i + k] * gain;
      m.process(xs, ys);
      out.set(ys, i);
      if ((i / chunk) % 10 === 0) self.postMessage({ id, progress: i / input.length });
    }
    self.postMessage({ id, done: true, output: out, loudness: m.loudness, sampleRate: m.sampleRate }, [out.buffer]);
  } catch (err) {
    self.postMessage({ id, error: String(err && err.message || err) });
  }
};
