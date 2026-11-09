/* Omega 110 additions to the app: the preset bar (100 factory presets, yours, the dice), the PAD EQ / ADSR page
   (a parametric EQ, an ADSR, drive and a delay send on every pad), the TC delay, the MASTER page (master
   parametric EQ, overdrive / clipper) and the bass extender and envelope switch on the voice-mod panel.
   Loaded after app.js (uses window.DRAPP). */
'use strict';
(() => {
const A = window.DRAPP, S = A.S, D = DR110, PR = DRPRESETS;
const {P, PARAMS, NV, VOICE_NAMES, vq} = D;
const $ = id => document.getElementById(id);
const el = (tag, cls, parent, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html !== undefined) e.innerHTML = html; if (parent) parent.appendChild(e); return e; };
const refresh = () => { for (const f of A.refreshers) f(); drawCurves(); };
const set = (i, v) => { A.setParam(i, v); drawCurves(); };

/* ---- knobs by kind: lin, log, db, int */
function knob(parent, label, i, kind = 'lin', fmt){
  const q = PARAMS[i], lo = q[2], hi = q[3];
  let get, put, dflt;
  if (kind === 'log'){ get = () => Math.log(S.params[i] / lo) / Math.log(hi / lo); put = n => set(i, +(lo * Math.pow(hi / lo, n)).toPrecision(3)); dflt = Math.log(q[4] / lo) / Math.log(hi / lo); }
  else if (kind === 'int'){ get = () => (S.params[i] - lo) / (hi - lo); put = n => set(i, Math.round(lo + n * (hi - lo))); dflt = (q[4] - lo) / (hi - lo); }
  else { get = () => (S.params[i] - lo) / (hi - lo); put = n => set(i, Math.round((lo + n * (hi - lo)) * 100) / 100); dflt = (q[4] - lo) / (hi - lo); }
  const f = fmt || (() => { const v = S.params[i]; if (kind === 'log' && v >= 1000) return (v / 1000).toFixed(v >= 10000 ? 1 : 2) + 'k'; return kind === 'int' ? String(v) : (Math.abs(v) >= 100 ? Math.round(v) : v.toFixed(2)); });
  const w = A.miniKnob(parent, label, get, n => { put(n); refreshNoKnobs(); }, f, dflt);
  A.refreshers.push(w.refresh);
  return w;
}
// after a knob turn only the curves and value-dependent text change
function refreshNoKnobs(){ drawCurves(); showPresetName(); }
const hz = v => v >= 1000 ? (v / 1000).toFixed(v >= 10000 ? 1 : 2) + ' kHz' : Math.round(v) + ' Hz';
const db = i => () => (S.params[i] > 0 ? '+' : '') + S.params[i].toFixed(1) + ' dB';
const ms = v => v >= 1 ? v.toFixed(2) + ' s' : Math.round(v * 1000) + ' ms';

/* ---- EQ curves (drawn from the engine's own filters) */
const curves = [];
function eqCurve(parent, bands){
  const c = el('canvas', 'eqc', parent); c.width = 640; c.height = 200;
  curves.push({c, bands}); return c;
}
function drawCurves(){
  for (let k = curves.length - 1; k >= 0; k--) if (!curves[k].c.isConnected) curves.splice(k, 1);
  for (const {c, bands, draw} of curves){
    if (!c.offsetParent) continue;
    if (draw){ draw(); continue; }
    const g = c.getContext('2d'), W = c.width, H = c.height, sr = 48000;
    g.fillStyle = '#141a12'; g.fillRect(0, 0, W, H);
    g.strokeStyle = 'rgba(124,255,138,.15)'; g.lineWidth = 1;
    for (const f of [50, 100, 200, 500, 1000, 2000, 5000, 10000]){ const x = Math.log(f / 20) / Math.log(1000) * W; g.beginPath(); g.moveTo(x, 0); g.lineTo(x, H); g.stroke(); }
    for (const d of [-12, -6, 0, 6, 12]){ const y = H / 2 - d / 18 * H / 2; g.beginPath(); g.moveTo(0, y); g.lineTo(W, y); g.stroke(); }
    const bq = bands().map(([ty, f, gn, q]) => { const b = new D.Biquad(); b.set(ty, f, gn, q, sr); return b; });
    g.strokeStyle = '#7cff8a'; g.lineWidth = 3; g.beginPath();
    for (let x = 0; x <= W; x += 2){
      const f = 20 * Math.pow(1000, x / W), w = 2 * Math.PI * f / sr; let mag = 0;
      for (const b of bq){ if (!b.on) continue;
        const cr = Math.cos(w), ci = -Math.sin(w), c2r = Math.cos(2 * w), c2i = -Math.sin(2 * w);
        const nr = b.b0 + b.b1 * cr + b.b2 * c2r, ni = b.b1 * ci + b.b2 * c2i, dr = 1 + b.a1 * cr + b.a2 * c2r, di = b.a1 * ci + b.a2 * c2i;
        mag += 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di)); }
      const y = H / 2 - Math.max(-18, Math.min(18, mag)) / 18 * H / 2;
      if (x === 0) g.moveTo(x, y); else g.lineTo(x, y);
    }
    g.stroke();
  }
}

/* ---- presets */
const bar = $('presetBar');
bar.innerHTML = `<button class="pb" id="pPrev" aria-label="Previous preset">◀</button><select id="pSel" aria-label="Preset"></select><button class="pb" id="pNext" aria-label="Next preset">▶</button>
  <button class="pb dice" id="pDice" title="Roll the dice">⚄ DICE</button><select id="pPart" aria-label="What the dice changes"></select>
  <button class="pb" id="pSave">SAVE</button><button class="pb" id="pDel">DELETE</button><button class="pb" id="pInit">INIT</button><span id="pName"></span>`;
const sel = $('pSel'), part = $('pPart');
for (const p of PR.DICE_PARTS) el('option', '', part, {all:'Everything', voices:'Voices + mods', eq:'Pad EQ', env:'ADSR + drive', fx:'Reverb + delay', master:'Master EQ + drive'}[p]).value = p;
function fillPresets(){
  sel.innerHTML = '';
  let og = null, fam = '';
  PR.PRESETS.forEach((p, i) => { if (p.family !== fam){ fam = p.family; og = el('optgroup', '', sel); og.label = fam; } const o = el('option', '', og, String(i + 1).padStart(3, '0') + ' ' + p.name.split(' · ')[1]); o.value = i; });
  if (S.userPresets.length){ og = el('optgroup', '', sel); og.label = 'Yours'; S.userPresets.forEach((u, k) => { el('option', '', og, 'U' + (k + 1) + ' ' + u.name).value = 1000 + k; }); }
  sel.value = String(S.preset);
}
function showPresetName(){ const t = $('pName'); if (t) t.textContent = S.edited ? 'edited' : ''; }
function loadPreset(k){
  const src = k >= 1000 ? S.userPresets[k - 1000] : PR.PRESETS[k]; if (!src) return;
  S.params = PR.load(S.params, src.p); S.preset = k; S.edited = false;
  A.post({type:'params', p:A.engineParams()}); A.save(); refresh(); sel.value = String(k); showPresetName();
  A.flash(['PRESET', k >= 1000 ? 'U' + (k - 999) : String(k + 1).padStart(3, '0')], 900);
}
sel.onchange = () => loadPreset(+sel.value);
const step = d => { const all = [...sel.querySelectorAll('option')].map(o => +o.value); let i = all.indexOf(S.preset); i = (i + d + all.length) % all.length; loadPreset(all[i]); };
$('pPrev').onclick = () => step(-1); $('pNext').onclick = () => step(1);
$('pDice').onclick = () => {
  S.params = PR.load(S.params, PR.dice(S.params, part.value)); S.edited = true;
  A.post({type:'params', p:A.engineParams()}); A.save(); refresh(); showPresetName();
  A.flash(['DICE', part.value.toUpperCase()], 900);
  $('pDice').animate([{transform:'rotate(0)'}, {transform:'rotate(360deg)'}], {duration:350});
};
$('pSave').onclick = () => {
  const name = (prompt('Name for this preset', 'My kit ' + (S.userPresets.length + 1)) || '').trim(); if (!name) return;
  S.userPresets.push({name:name.slice(0, 40), p:S.params.slice()}); S.preset = 1000 + S.userPresets.length - 1; S.edited = false;
  fillPresets(); A.save(); A.toast('Saved ' + name);
};
$('pDel').onclick = () => {
  if (S.preset < 1000) return A.toast('Factory presets stay; pick one of yours to delete');
  const u = S.userPresets[S.preset - 1000]; if (!u || !confirm('Delete "' + u.name + '"?')) return;
  S.userPresets.splice(S.preset - 1000, 1); S.preset = 0; fillPresets(); A.save();
};
$('pInit').onclick = () => { S.params = PR.load(S.params, D.defaults()); S.edited = true; A.post({type:'params', p:A.engineParams()}); A.save(); refresh(); A.toast('Back to the stock machine'); };
// any knob turn marks the preset edited
const origSet = A.setParam;
A.setParam = (i, v) => { origSet(i, v); if (!S.edited){ S.edited = true; showPresetName(); } };

/* ---- voice-mod panel: bass extender and the kick envelope switch (the Circuitbenders mod) */
const kick = document.querySelector('#modPanel .modSec');
if (kick){
  const w = knob(kick, 'BASS EXTEND', P.bdExtend, 'lin', () => Math.round(S.params[P.bdExtend] * 100) + '%'); w.querySelector('svg').classList.add('white');
  A.toggle(kick, 'ENV SHAPE', true, () => S.params[P.bdEnv] >= .5, on => set(P.bdEnv, on ? 1 : 0));
}

/* ---- PAD EQ / ADSR */
let pad = 0;
const pads = $('pads');
const tabs = el('div', 'padTabs', pads);
const padBtns = VOICE_NAMES.map((n, v) => { const b = el('button', 'padBtn' + (v ? '' : ' on'), tabs, n.toUpperCase()); b.onclick = () => { pad = v; buildPad(); }; return b; });
const padBody = el('div', 'padBody', pads);
function buildPad(){
  padBtns.forEach((b, v) => b.classList.toggle('on', v === pad));
  padBody.innerHTML = '';
  const q = f => vq(pad, f);
  const eqBox = el('div', 'box', padBody, `<h3>${VOICE_NAMES[pad].toUpperCase()} · PARAMETRIC EQ</h3>`);
  eqCurve(eqBox, () => [[0, S.params[q(0)], S.params[q(1)], .707], [1, S.params[q(2)], S.params[q(3)], S.params[q(4)]], [2, S.params[q(5)], S.params[q(6)], .707]]);
  const k1 = el('div', 'kRow', eqBox);
  knob(k1, 'LOW FREQ', q(0), 'log', () => hz(S.params[q(0)])); knob(k1, 'LOW GAIN', q(1), 'lin', db(q(1)));
  knob(k1, 'MID FREQ', q(2), 'log', () => hz(S.params[q(2)])); knob(k1, 'MID GAIN', q(3), 'lin', db(q(3))); knob(k1, 'MID Q', q(4), 'log');
  knob(k1, 'HIGH FREQ', q(5), 'log', () => hz(S.params[q(5)])); knob(k1, 'HIGH GAIN', q(6), 'lin', db(q(6)));
  const envBox = el('div', 'box', padBody, '<h3>ADSR · DRIVE · DELAY SEND</h3><p>The ADSR shapes the hit on top of the circuit\'s own decay. The gate is the key press, the MIDI note or one step of the pattern. Sustain at 100% and release at maximum leave the pad as it was.</p>');
  const adsr = el('canvas', 'eqc adsrc', envBox); adsr.width = 640; adsr.height = 140;
  curves.push({c:adsr, bands:null, draw:() => drawAdsr(adsr)});
  const k2 = el('div', 'kRow', envBox);
  knob(k2, 'ATTACK', q(7), 'lin', () => ms(S.params[q(7)])); knob(k2, 'DECAY', q(8), 'log', () => ms(S.params[q(8)]));
  knob(k2, 'SUSTAIN', q(9), 'lin', () => Math.round(S.params[q(9)] * 100) + '%');
  knob(k2, 'RELEASE', q(10), 'log', () => S.params[q(10)] >= 3.999 ? 'HOLD' : ms(S.params[q(10)]));
  knob(k2, 'DRIVE', q(11), 'lin', () => Math.round(S.params[q(11)] * 100) + '%'); knob(k2, 'DELAY SEND', q(12), 'lin', () => Math.round(S.params[q(12)] * 100) + '%');
  const b = el('div', 'kRow', envBox);
  const hit = el('button', 'small', b, 'PLAY ' + VOICE_NAMES[pad].toUpperCase());
  hit.onpointerdown = async () => { if (!S.power){ S.power = true; await A.startAudio(); A.draw(); } A.post({type:'hit', v:pad, acc:false}); };
  hit.onpointerup = hit.onpointerleave = () => A.post({type:'release', v:pad});
  const rs = el('button', 'small', b, 'RESET PAD');
  rs.onclick = () => { const d = D.defaults(); for (let f = 0; f < 13; f++) S.params[q(f)] = d[q(f)]; A.sendAll(); A.save(); refresh(); };
  refresh();
}
function drawAdsr(c){
  const g = c.getContext('2d'), W = c.width, H = c.height, q = f => S.params[vq(pad, f)];
  g.fillStyle = '#141a12'; g.fillRect(0, 0, W, H);
  const a = q(7), d = q(8), s = q(9), r = q(10), hold = r >= 3.999;
  const total = a + Math.min(d, 1.5) + 0.4 + (hold ? 0.6 : Math.min(r, 2));
  const X = t => 10 + t / total * (W - 20), Y = v => H - 10 - v * (H - 20);
  g.strokeStyle = '#7cff8a'; g.lineWidth = 3; g.beginPath(); g.moveTo(X(0), Y(a > 0 ? 0 : 1)); g.lineTo(X(a), Y(1));
  const dd = Math.min(d, 1.5); for (let t = 0; t <= dd; t += dd / 30) g.lineTo(X(a + t), Y(s + (1 - s) * Math.exp(-t / (d / 3))));
  const t0 = a + dd + 0.4; g.lineTo(X(t0), Y(s + (1 - s) * Math.exp(-dd / (d / 3))));
  const sv = s + (1 - s) * Math.exp(-dd / (d / 3));
  if (hold) g.lineTo(X(total), Y(sv)); else { const rr = Math.min(r, 2); for (let t = 0; t <= rr; t += rr / 30) g.lineTo(X(t0 + t), Y(sv * Math.exp(-t / (r / 3)))); }
  g.stroke();
  g.fillStyle = 'rgba(124,255,138,.6)'; g.font = '14px sans-serif'; g.fillText('gate', X(a + dd), 16); g.fillText(hold ? 'release: hold' : 'release', X(t0) + 4, 16);
}
window.addEventListener('resize', () => drawCurves());

/* ---- TC delay (on the effects page) and the delay sends */
const tcd = $('tcd');
tcd.innerHTML = `<div class="tcHead"><b>TC 2290</b><span>DYNAMIC DIGITAL DELAY · STYLE</span><span id="tcLcd" class="revLcd"></span></div>`;
const tk = el('div', 'revKnobs', tcd);
const tcLcd = () => { const sync = Math.round(S.params[P.dlSync]); const t = sync ? 60 / S.params[P.tempo] * D.DL_DIVS[sync] * 1000 : S.params[P.dlTime];
  $('tcLcd').textContent = (sync ? D.DL_DIV_NAMES[sync] + ' ' : '') + Math.round(t) + ' ms'; };
A.refreshers.push(tcLcd);
knob(tk, 'TIME', P.dlTime, 'log', () => Math.round(S.params[P.dlTime]) + ' ms');
knob(tk, 'SYNC', P.dlSync, 'int', () => D.DL_DIV_NAMES[Math.round(S.params[P.dlSync])]);
knob(tk, 'FEEDBACK', P.dlFb, 'lin', () => Math.round(S.params[P.dlFb] * 100) + '%');
knob(tk, 'HIGH CUT', P.dlHiCut, 'log', () => hz(S.params[P.dlHiCut])); knob(tk, 'LOW CUT', P.dlLoCut, 'log', () => hz(S.params[P.dlLoCut]));
knob(tk, 'MOD RATE', P.dlModRate, 'log', () => S.params[P.dlModRate].toFixed(2) + ' Hz'); knob(tk, 'MOD DEPTH', P.dlModDepth, 'lin', () => Math.round(S.params[P.dlModDepth] * 100) + '%');
knob(tk, 'DUCKING', P.dlDuck, 'lin', () => Math.round(S.params[P.dlDuck] * 100) + '%'); knob(tk, 'RETURN', P.dlLevel, 'lin', () => Math.round(S.params[P.dlLevel] * 100) + '%');
A.toggle(tk, 'PING-PONG', true, () => S.params[P.dlPing] >= .5, on => set(P.dlPing, on ? 1 : 0));
// a delay send under each reverb send
document.querySelectorAll('#sends .strip').forEach((s, v) => knob(s, 'DELAY SEND', vq(v, 12), 'lin', () => Math.round(S.params[vq(v, 12)] * 100) + '%'));

/* ---- MASTER: parametric EQ and the overdrive / clipper */
const mt = $('masterTab');
const mEq = el('div', 'box', mt, '<h3>MASTER PARAMETRIC EQ</h3>');
eqCurve(mEq, () => [[0, S.params[P.mLsF], S.params[P.mLsG], .707], [1, S.params[P.mP1F], S.params[P.mP1G], S.params[P.mP1Q]], [1, S.params[P.mP2F], S.params[P.mP2G], S.params[P.mP2Q]], [2, S.params[P.mHsF], S.params[P.mHsG], .707]]);
const mk = el('div', 'kRow', mEq);
knob(mk, 'LOW FREQ', P.mLsF, 'log', () => hz(S.params[P.mLsF])); knob(mk, 'LOW GAIN', P.mLsG, 'lin', db(P.mLsG));
knob(mk, 'MID 1 FREQ', P.mP1F, 'log', () => hz(S.params[P.mP1F])); knob(mk, 'MID 1 GAIN', P.mP1G, 'lin', db(P.mP1G)); knob(mk, 'MID 1 Q', P.mP1Q, 'log');
knob(mk, 'MID 2 FREQ', P.mP2F, 'log', () => hz(S.params[P.mP2F])); knob(mk, 'MID 2 GAIN', P.mP2G, 'lin', db(P.mP2G)); knob(mk, 'MID 2 Q', P.mP2Q, 'log');
knob(mk, 'HIGH FREQ', P.mHsF, 'log', () => hz(S.params[P.mHsF])); knob(mk, 'HIGH GAIN', P.mHsG, 'lin', db(P.mHsG));
const od = el('div', 'box', mt, '<h3>OVERDRIVE · CLIPPER</h3><p>After the master EQ, before VOLUME. DRIVE at 0 is off.</p>');
const ok = el('div', 'kRow', od);
knob(ok, 'DRIVE', P.odDrive, 'lin', () => Math.round(S.params[P.odDrive] * 100) + '%');
knob(ok, 'MIX', P.odMix, 'lin', () => Math.round(S.params[P.odMix] * 100) + '%');
knob(ok, 'OUTPUT', P.odOut, 'lin', db(P.odOut));
const modes = el('div', 'revProgs', od);
D.OD_MODES.forEach((m, i) => { const b = el('button', '', modes, m); b.onclick = () => { set(P.odMode, i); refresh(); }; });
A.refreshers.push(() => [...modes.children].forEach((b, i) => b.classList.toggle('on', i === Math.round(S.params[P.odMode]))));
const mm = el('div', 'box', mt, '<h3>MACHINE</h3>');
const mk2 = el('div', 'kRow', mm);
knob(mk2, 'BALANCE', P.balance, 'lin', () => S.params[P.balance].toFixed(1)); knob(mk2, 'ACCENT', P.accent, 'lin', () => S.params[P.accent].toFixed(1)); knob(mk2, 'VOLUME', P.volume, 'lin', () => S.params[P.volume].toFixed(1));

// curves: redraw when their page opens
document.querySelectorAll('#tabs button').forEach(b => b.addEventListener('click', () => setTimeout(drawCurves, 0)));
fillPresets(); buildPad(); refresh(); showPresetName();

/* ---- expand / fold: the whole machine, or just the front panel */
const foldBtn = $('foldBtn');
const setFold = basic => {
  document.body.classList.toggle('basic', basic);
  foldBtn.textContent = basic ? 'EXPAND ▾' : 'FOLD ▴'; foldBtn.setAttribute('aria-pressed', String(basic));
  try { localStorage.setItem('omega-110-basic', basic ? '1' : '0'); } catch (e){}
  if (!basic) setTimeout(drawCurves, 0);
};
foldBtn.onclick = () => setFold(!document.body.classList.contains('basic'));
let startBasic = false; try { startBasic = localStorage.getItem('omega-110-basic') === '1'; } catch (e){}
setFold(startBasic);
window.DROMEGA = {refresh, loadPreset, fillPresets};
})();
