/* Omega Twelve TV screen: receives the app's live state and draws it big. Three ways in:
   - Cast (Presentation API): the TV/Chromecast loads this page and the phone's messages arrive on a presentation connection,
   - a second window on the same computer (HDMI TV as an extra screen): BroadcastChannel,
   - opened by the app with window.open: postMessage from the opener. */
'use strict';
const $ = s => document.querySelector(s);
const PBY = Object.fromEntries(TW.PARAMS.map(p => [p.id, p]));
const GROUPS = [['Sampler', ['machine', 'bits', 'rate', 'rpm', 'drive', 'pre', 'calib', 'mono']], ['Filter', ['ftype', 'fOn', 'cutoff', 'res', 'fenv', 'mix']],
  ['Tuning · time', ['semis', 'cents', 'a4', 'stretch', 'grain', 'smode']], ['Lo-fi', ['crackle', 'hiss', 'wow', 'flutter', 'tape', 'age', 'hum']],
  ['Master EQ', ['lsG', 'p1F', 'p1G', 'p2F', 'p2G', 'p3F', 'p3G', 'hsG', 'match']], ['Master dynamics', ['mOn', 'cThr', 'cRat', 'cMake', 'mGain', 'lim', 'input', 'output']]];
let st = null, peaks = null, pos = null, focusId = null, focusT = 0;

function fmt(p, v){
  if (p.choices) return p.choices[Math.round(v)] || '';
  if (p.toggle) return v > .5 ? 'On' : 'Off';
  switch (p.unit){
    case 'Hz': return v >= 1000 ? (v / 1000).toFixed(v >= 10000 ? 1 : 2) + 'k' : Math.round(v) + '';
    case '%': return Math.round(v * 100) + '%';
    case 'dB': return (v > 0 ? '+' : '') + v.toFixed(1);
    case 'st': return (v > 0 ? '+' : '') + Math.round(v) + ' st';
    case 'ct': return (v > 0 ? '+' : '') + Math.round(v) + ' ct';
    case 'x': return v.toFixed(2) + '×';
    case 'ms': return Math.round(v) + ' ms';
    case 'bit': return Math.round(v) + '-bit';
    case ':1': return v.toFixed(1) + ':1';
    default: return (Math.abs(v) < 10 ? v.toFixed(2) : v.toFixed(1)) + (p.unit || '');
  }
}

function build(){
  const g = $('#groups');
  for (const [name, ids] of GROUPS){
    const d = document.createElement('div'); d.className = 'grp';
    d.innerHTML = `<h3>${name}</h3>` + ids.map(id => `<div class="kv" data-id="${id}"><span>${PBY[id].name}</span><b></b></div>`).join('');
    g.appendChild(d);
  }
  const pads = $('#pads');
  for (const s of [0, 1, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -1]){ const d = document.createElement('div'); d.className = 'pad'; d.dataset.s = s; d.textContent = (s > 0 ? '+' : '') + s; pads.appendChild(d); }
}

function render(){
  if (!st) return;
  const p = st.p;
  $('#wait').classList.add('hide');
  $('#preset').textContent = st.preset;
  $('#m').textContent = fmt(PBY.machine, p.machine);
  $('#bits').textContent = Math.round(p.bits);
  $('#rate').textContent = (p.rate / 1000).toFixed(p.rate < 10000 ? 2 : 1) + 'k';
  const t = p.semis + p.cents / 100; $('#tune').textContent = (t > 0 ? '+' : '') + (p.cents ? t.toFixed(2) : t);
  $('#ft').textContent = fmt(PBY.ftype, p.ftype).replace(/ \(.*/, '');
  $('#cut').textContent = p.ftype >= 1 && p.ftype <= 5 ? 'FIXED' : fmt(PBY.cutoff, p.cutoff);
  $('#res').textContent = fmt(PBY.res, p.res); $('#mix').textContent = fmt(PBY.mix, p.mix);
  $('#info').innerHTML = Object.entries(st.info || {}).map(([k, v]) => `<span>${k} <i>${v}</i></span>`).join('');
  document.querySelectorAll('.kv').forEach(el => { const id = el.dataset.id; el.querySelector('b').textContent = fmt(PBY[id], p[id]); el.classList.toggle('hot', id === focusId && Date.now() - focusT < 3000); });
  $('#status').innerHTML = st.status || '';
  drawEq();
  showFocus();
}
function showFocus(){
  if (!focusId || !st) return;
  const p = PBY[focusId], v = st.p[focusId];
  $('#fname').textContent = p.name; $('#fval').textContent = fmt(p, v);
  const n = p.choices || p.toggle ? (v - p.min) / Math.max(1, p.max - p.min) : p.log ? Math.log(v / p.min) / Math.log(p.max / p.min) : (v - p.min) / (p.max - p.min);
  const a0 = -135, a = a0 + Math.max(0, Math.min(1, n)) * 270, pt = d => { const r = (d - 90) * Math.PI / 180; return [26 + 23 * Math.cos(r), 26 + 23 * Math.sin(r)]; };
  const [x0, y0] = pt(a0), [x1, y1] = pt(a);
  $('#farc').setAttribute('d', n > .002 ? `M${x0} ${y0} A23 23 0 ${a - a0 > 180 ? 1 : 0} 1 ${x1} ${y1}` : '');
  $('#find').setAttribute('transform', `rotate(${a} 26 26)`);
}

function drawWave(){
  const c = $('#wave'), w = c.clientWidth, h = c.clientHeight; if (!w) return;
  const dpr = devicePixelRatio || 1; if (c.width !== w * dpr){ c.width = w * dpr; c.height = h * dpr; }
  const g = c.getContext('2d'); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, w, h);
  if (!peaks || !st){ g.fillStyle = '#3d8a62'; g.font = `${Math.round(h / 8)}px monospace`; g.textAlign = 'center'; g.fillText('no sample loaded', w / 2, h / 2); return; }
  const {mn, mx, len} = peaks, [v0, v1] = st.view || [0, len], [a, b] = st.sel || [0, len], N = mn.length;
  const X = i => (i - v0) / (v1 - v0) * w;
  g.fillStyle = '#12301f'; g.fillRect(X(a), 0, X(b) - X(a), h);
  for (let x = 0; x < w; x++){
    const i0 = v0 + x / w * (v1 - v0), i1 = v0 + (x + 1) / w * (v1 - v0), k0 = Math.floor(i0 / len * N), k1 = Math.max(k0 + 1, Math.floor(i1 / len * N));
    let lo = 0, hi = 0; for (let k = k0; k < k1 && k < N; k++){ lo = Math.min(lo, mn[k]); hi = Math.max(hi, mx[k]); }
    g.fillStyle = i0 >= a && i0 <= b ? '#7dffb0' : '#2f6b4c';
    g.fillRect(x, h / 2 - hi * h / 2, 1, Math.max(1, (hi - lo) * h / 2));
  }
  g.fillStyle = '#e2a83a'; for (const e of [a, b]){ g.fillRect(X(e) - 1.5, 0, 3, h); }
  if (pos != null){ g.fillStyle = '#fff'; g.fillRect(X(a + pos * (b - a)) - 1, 0, 2.5, h); }
}
function drawEq(){
  const c = $('#eq'), w = c.clientWidth, h = c.clientHeight; if (!w || !st) return;
  const dpr = devicePixelRatio || 1; c.width = w * dpr; c.height = h * dpr;
  const g = c.getContext('2d'); g.scale(dpr, dpr);
  const fs = 48000, p = st.p, m = st.match || [];
  const secs = [TW.rbj('ls', fs, p.lsF, .707, p.lsG), TW.rbj('peak', fs, p.p1F, p.p1Q, p.p1G), TW.rbj('peak', fs, p.p2F, p.p2Q, p.p2G), TW.rbj('peak', fs, p.p3F, p.p3Q, p.p3G), TW.rbj('hs', fs, p.hsF, .707, p.hsG)]
    .concat(p.match > 0 ? TW.MATCH_BANDS.map((f, i) => TW.rbj('peak', fs, f, 1.41, (m[i] || 0) * p.match)) : []);
  const resp = f => secs.reduce((s, q) => { const w0 = 2 * Math.PI * f / fs, cr = Math.cos(w0), ci = -Math.sin(w0), c2r = Math.cos(2 * w0), c2i = -Math.sin(2 * w0);
    const nr = q.b0 + q.b1 * cr + q.b2 * c2r, ni = q.b1 * ci + q.b2 * c2i, dr = 1 + q.a1 * cr + q.a2 * c2r, di = q.a1 * ci + q.a2 * c2i; return s + 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di)); }, 0);
  const X = f => Math.log(f / 20) / Math.log(1000) * w, Y = d => h / 2 - d / 15 * (h / 2 - 8);
  g.strokeStyle = '#1f2a25'; for (const f of [100, 1000, 10000]){ g.beginPath(); g.moveTo(X(f), 0); g.lineTo(X(f), h); g.stroke(); }
  g.beginPath(); g.moveTo(0, Y(0)); g.lineTo(w, Y(0)); g.stroke();
  g.strokeStyle = p.mOn > .5 ? '#7dffb0' : '#3d8a62'; g.lineWidth = 3; g.beginPath();
  for (let x = 0; x <= w; x += 3){ const y = Y(resp(20 * Math.pow(1000, x / w))); x ? g.lineTo(x, y) : g.moveTo(x, y); } g.stroke();
  g.fillStyle = '#3d8a62'; g.font = `${Math.round(h / 9)}px monospace`; g.fillText('MASTER EQ' + (p.mOn > .5 ? '' : ' (off)'), 10, h / 9 + 6);
}

function onMessage(d){
  if (typeof d === 'string') d = JSON.parse(d);
  if (d.type === 'state'){ const prev = st && st.p; st = d; if (prev && d.touched){ focusId = d.touched; focusT = Date.now(); } render(); drawWave(); }
  else if (d.type === 'peaks'){ peaks = {mn:Float32Array.from(d.mn), mx:Float32Array.from(d.mx), len:d.len}; drawWave(); }
  else if (d.type === 'pos'){ pos = d.pos; drawWave(); }
  else if (d.type === 'pad'){ const el = document.querySelector(`.pad[data-s="${d.semis}"]`); if (el){ el.classList.add('hit'); setTimeout(() => el.classList.remove('hit'), 140); } }
  else if (d.type === 'hello'){ $('#waitMsg').textContent = 'Connected. Load a sample on the app.'; }
}

build();
addEventListener('resize', () => { drawWave(); drawEq(); });
// 1: cast receiver
if (navigator.presentation && navigator.presentation.receiver){
  navigator.presentation.receiver.connectionList.then(list => {
    const hook = c => { c.onmessage = e => onMessage(e.data); c.send(JSON.stringify({type:'ready'})); };
    list.connections.forEach(hook); list.onconnectionavailable = e => hook(e.connection);
  });
}
// 2: same computer, another window
try { const bc = new BroadcastChannel('omega-twelve-tv'); bc.onmessage = e => onMessage(e.data); bc.postMessage({type:'ready'}); } catch (e) {}
// 3: opened by the app
addEventListener('message', e => { if (e.origin === location.origin) onMessage(e.data); });
if (window.opener) window.opener.postMessage({type:'ready'}, location.origin);
