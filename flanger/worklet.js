// AudioWorklet: the M-117 circuit model (engine.js, added to this scope first as globalThis.M117) on the live signal.
class FlangerProcessor extends AudioWorkletProcessor {
  constructor(){
    super();
    this.e = new globalThis.M117.Engine();
    this.e.prepare(sampleRate);
    this.port.onmessage = ev => {
      const m = ev.data;
      if (m.set) for (const [k, v] of Object.entries(m.set)) this.e.set(k, v);
      if (m.reset) this.e.reset();
    };
  }
  process(inputs, outputs){
    const inp = inputs[0], out = outputs[0];
    const n = out[0].length;
    const L = inp && inp[0] ? inp[0] : (this.z || (this.z = new Float32Array(n)));
    const R = inp && inp[1] ? inp[1] : L;
    this.e.process(L, R, out[0], out[1] || out[0], n);
    return true;
  }
}
registerProcessor('m117', FlangerProcessor);
