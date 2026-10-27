/* Omega Twelve render worker: holds the source, renders the chain off the main thread, analyses and encodes. */
importScripts('sp1200.js', 'engine.js', 'codecs.js');
const LAME = 'https://cdn.jsdelivr.net/npm/lamejs@1.2.1/lame.min.js';

let src = null, sr = 44100;
const cache = {stretchKey:'', stretched:null, preKey:'', pre:null, preFs:0, pads:new Map()};
const soundKey = (p, sel) => JSON.stringify([sel, TW.SOUND_IDS.map(id => p[id])]);

function slice(sel){
  const a = Math.max(0, Math.min(src[0].length - 1, sel[0] | 0)), b = Math.max(a + 1, Math.min(src[0].length, sel[1] | 0));
  return src.map(c => c.subarray(a, b));
}
function stretched(p, sel){
  const k = JSON.stringify([sel, p.mono, p.stretch, p.grain, p.smode]);
  if (k !== cache.stretchKey){
    let chs = slice(sel);
    if (p.mono > .5 && chs.length > 1){ const m = new Float32Array(chs[0].length); for (let i = 0; i < m.length; i++) m[i] = (chs[0][i] + chs[1][i]) * .5; chs = [m]; }
    cache.stretched = TW.stretch(chs, p.stretch, sr, p.grain, p.smode > .5); cache.stretchKey = k;
  }
  return cache.stretched;
}
function pre(p, sel, fs){
  const k = soundKey(p, sel) + fs;
  if (k !== cache.preKey){ cache.pre = TW.renderPre(null, sr, p, fs, {stretched:stretched(p, sel)}).pre; cache.preKey = k; cache.preFs = fs; cache.pads.clear(); }
  return cache.pre;
}
function stats(out, fs){ return {lufs:Math.round(TW.loudness(out, fs) * 10) / 10, peak:Math.round(TW.peakDb(out) * 10) / 10}; }

onmessage = e => {
  const d = e.data;
  try {
    if (d.type === 'source'){ src = d.chs; sr = d.sr; cache.stretchKey = cache.preKey = ''; cache.pads.clear(); return; }
    if (!src) return postMessage({type:'error', id:d.id, msg:'No sample loaded'});
    if (d.type === 'render'){
      const p = d.p, out = TW.renderMaster(pre(p, d.sel, d.fs), p, d.match, d.fs);
      postMessage({type:'rendered', id:d.id, out, fs:d.fs, ...stats(out, d.fs)}, out.map(c => c.buffer));
    } else if (d.type === 'pad'){
      const p = d.p, base = pre(p, d.sel, d.fs), k = d.semis;
      let out = cache.pads.get(k);
      if (!out){
        const q = Object.assign({}, p, {semis:p.semis + k});
        out = TW.renderMaster(TW.renderPre(null, sr, q, d.fs, {stretched:stretched(p, d.sel)}).pre, p, d.match, d.fs);
        cache.pads.set(k, out);
      }
      void base;
      postMessage({type:'pad', id:d.id, semis:k, out:out.map(c => c.slice()), fs:d.fs});
    } else if (d.type === 'analyze'){
      const chs = slice(d.sel);
      postMessage({type:'analyzed', id:d.id, bpm:TW.detectTempo(chs, sr), key:TW.detectKey(chs, sr)});
    } else if (d.type === 'refspec'){
      postMessage({type:'refspec', id:d.id, bands:TW.bandSpectrum(d.chs, d.sr), name:d.name});
    } else if (d.type === 'match'){
      const cur = TW.bandSpectrum(pre(d.p, d.sel, d.fs), d.fs);
      postMessage({type:'match', id:d.id, match:TW.matchGains(d.ref, cur)});
    } else if (d.type === 'automaster'){
      const r = TW.autoMaster(pre(d.p, d.sel, d.fs), d.fs, d.ref, d.target);
      postMessage({type:'automaster', id:d.id, params:r.params, match:r.match});
    } else if (d.type === 'export'){
      const p = d.p, sel = d.sel, fs = d.fs;
      const out = TW.renderMaster(TW.renderPre(null, sr, p, fs, {stretched:stretched(p, sel)}).pre, p, d.match, fs);
      postMessage({type:'export', id:d.id, ...encode(out, fs, d.fmt)});
    } else if (d.type === 'encode'){
      postMessage({type:'export', id:d.id, ...encode(d.chs, d.fs, d.fmt)});
    }
  } catch (err){ postMessage({type:'error', id:d.id, msg:String(err && err.message || err)}); }
};

function encode(out, fs, fmt){
  const [kind, q] = fmt.split(':');
  if (kind === 'wav') return {bytes:TWC.encodeWav(out, fs, q === 'f32' ? 'f32' : +q), mime:'audio/wav', ext:'wav'};
  if (kind === 'aiff') return {bytes:TWC.encodeAiff(out, fs, +q), mime:'audio/aiff', ext:'aif'};
  if (kind === 'flac') return {bytes:TWC.encodeFlac(out, fs, +q), mime:'audio/flac', ext:'flac'};
  if (kind === 'mp3'){
    if (typeof lamejs === 'undefined') importScripts(LAME);
    if (![32000, 44100, 48000].includes(fs)) throw new Error('MP3 needs 32, 44.1 or 48 kHz');
    const enc = new lamejs.Mp3Encoder(out.length, fs, +q), parts = [], B = 1152, ints = out.map(c => { const r = new Int16Array(c.length); for (let i = 0; i < c.length; i++) r[i] = Math.max(-32768, Math.min(32767, Math.round(c[i] * 32767))); return r; });
    for (let i = 0; i < ints[0].length; i += B){ const m = out.length > 1 ? enc.encodeBuffer(ints[0].subarray(i, i + B), ints[1].subarray(i, i + B)) : enc.encodeBuffer(ints[0].subarray(i, i + B)); if (m.length) parts.push(new Uint8Array(m)); }
    const f = enc.flush(); if (f.length) parts.push(new Uint8Array(f));
    const len = parts.reduce((s, x) => s + x.length, 0), bytes = new Uint8Array(len); let o = 0; for (const x of parts){ bytes.set(x, o); o += x.length; }
    return {bytes, mime:'audio/mpeg', ext:'mp3'};
  }
  throw new Error('Unknown format ' + fmt);
}
