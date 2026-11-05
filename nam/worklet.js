// worklet.js — AudioWorklet: a NAM model on live input (your guitar through an interface).
import { buildModel } from './engine.js';

class NamProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.model = null; this.gain = 1;
    this.port.onmessage = e => {
      const d = e.data;
      if (d.json) {
        try { this.model = buildModel(d.json); this.port.postMessage({ ready: true, loudness: this.model.loudness }); }
        catch (err) { this.model = null; this.port.postMessage({ error: String(err.message || err) }); }
      }
      if (d.gain != null) this.gain = d.gain;
      if (d.clear) this.model = null;
    };
  }
  process(inputs, outputs) {
    const inp = inputs[0] && inputs[0][0], out = outputs[0][0];
    if (!out) return true;
    if (!inp || !this.model) { out.fill(0); return true; }
    const g = this.gain, m = this.model;
    for (let i = 0; i < out.length; i++) out[i] = m.sample(inp[i] * g);
    for (let c = 1; c < outputs[0].length; c++) outputs[0][c].set(out);
    return true;
  }
}
registerProcessor('nam-processor', NamProcessor);
