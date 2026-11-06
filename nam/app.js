// app.js — NAM A2 Finder
import { analyzeNam, REFERENCE, DEVICES, cpuShare, variantUnits, unitsOf } from './namfile.js';
import { addRig, initPlayer } from './player.js';
import { saveBlob } from './audio.js';

const $ = id => document.getElementById(id);
const T3K = 'https://www.tone3000.com';
const store = {
  get(k, d) { try { const v = localStorage.getItem('namfinder.' + k); return v == null ? d : JSON.parse(v); } catch { return d; } },
  set(k, v) { try { localStorage.setItem('namfinder.' + k, JSON.stringify(v)); } catch {} },
};
const sess = {
  get(k) { try { const v = sessionStorage.getItem(k); return v == null ? null : JSON.parse(v); } catch { return null; } },
  set(k, v) { try { sessionStorage.setItem(k, JSON.stringify(v)); } catch {} },
  del(k) { try { sessionStorage.removeItem(k); } catch {} },
};
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const pct = x => x >= 10 ? '>999%' : (x * 100 < 10 ? (x * 100).toFixed(1) : Math.round(x * 100)) + '%';
const fmtN = n => n >= 1e6 ? (n / 1e6).toFixed(1) + 'M' : n >= 1e3 ? (n / 1e3).toFixed(1) + 'k' : String(n ?? 0);

// ─── 1. Machine ───────────────────────────────────────────────────────────────

const devices = [...DEVICES];
const measured = store.get('measured', null); // { capacityGeneric, rtf: {...}, when }
function measuredDevice() {
  const m = store.get('measured', null);
  if (!m) return null;
  const x = Number($('nativeX').value) || 1;
  return { id: 'measured', name: `This device (measured, native ≈ ${x}x browser)`, capacity: m.capacityGeneric * x, measured: m };
}

function fillDevices() {
  const sel = $('device');
  const cur = sel.value || store.get('device', 'pi4');
  sel.innerHTML = '';
  const md = measuredDevice();
  for (const d of md ? [md, ...devices] : devices) {
    const o = document.createElement('option');
    o.value = d.id; o.textContent = d.name;
    sel.appendChild(o);
  }
  sel.value = [...sel.options].some(o => o.value === cur) ? cur : 'pi4';
}
function currentDevice() {
  const id = $('device').value;
  return id === 'measured' ? measuredDevice() : devices.find(d => d.id === id);
}
function settings() {
  return {
    dev: currentDevice(),
    count: Number($('count').value),
    buffer: Number($('buffer').value),
    budget: Number($('budget').value) / 100,
    fastPath: $('fast').checked,
  };
}
// share of the CPU for `count` copies; status vs budget
function judge(v, s = settings()) {
  const share = cpuShare(v, s.dev.capacity, { buffer: s.buffer, fastPath: s.fastPath }) * s.count;
  const st = share <= s.budget ? 'ok' : share <= 1 ? 'warn' : 'bad';
  return { share, st, word: st === 'ok' ? 'Runs' : st === 'warn' ? 'Tight' : "Won't run" };
}
const color = st => `var(--${st === 'ok' ? 'ok' : st === 'warn' ? 'warn' : 'bad'})`;

function renderMachine() {
  const s = settings();
  $('budgetV').textContent = Math.round(s.budget * 100) + '%';
  const tiles = REFERENCE.map(r => {
    const j = judge(r, s);
    const m = s.dev.measured && s.dev.measured.rtf[r.label];
    return `<div class="tile ${r.family === 'A2' ? 'a2' : ''}">
      <div class="t">${esc(r.label)} ${r.family === 'A2' ? '<span class="tag a2">A2</span>' : ''}</div>
      <div class="pc" style="color:${color(j.st)}">${pct(j.share)}</div>
      <div class="bar"><i style="width:${Math.min(100, j.share * 100)}%;background:${color(j.st)}"></i></div>
      <span class="tag ${j.st}">${j.word}</span>
      ${m ? `<div class="note">browser: ${m.toFixed(1)}x real time</div>` : ''}
    </div>`;
  });
  $('tiles').innerHTML = tiles.join('');
  const full = judge(REFERENCE[0], s), lite = judge(REFERENCE[1], s);
  const n = s.count > 1 ? `${s.count} at once` : 'one at a time';
  let v;
  if (full.st === 'ok') v = `<b>Use A2-Full.</b> It is the more accurate size and fits ${n} with room to spare (${pct(full.share)} of the CPU). Any A2 tone will run, so pick on sound and accuracy.`;
  else if (lite.st === 'ok') v = `<b>Use A2-Lite.</b> A2-Full needs ${pct(full.share)} here, A2-Lite only ${pct(lite.share)}. Every A2 file has a Lite inside, so any A2 tone works: load it in a player that lets you choose the size (or set it to Lite).` +
    (full.st === 'warn' ? ' A2-Full might squeeze in with a bigger buffer and nothing else running.' : '');
  else if (lite.st === 'warn') v = `<b>A2-Lite is borderline</b> (${pct(lite.share)}). Try a bigger buffer, fewer models at once, or give NAM more of the CPU.`;
  else v = `<b>Too slow for NAM A2</b> (A2-Lite needs ${pct(lite.share)}). Look at A1-nano, an LSTM model, or a faster board.`;
  if (s.dev.note) v += `<div class="note">${esc(s.dev.note)}</div>`;
  $('verdict').innerHTML = v;
  store.set('device', $('device').value);
  store.set('machine', { count: s.count, buffer: s.buffer, budget: s.budget, fastPath: s.fastPath, nativeX: $('nativeX').value });
  renderRows();
}

function loadMachine() {
  const m = store.get('machine', null);
  if (m) {
    $('count').value = String(m.count); $('buffer').value = String(m.buffer);
    $('budget').value = String(Math.round(m.budget * 100)); $('fast').checked = m.fastPath !== false;
    if (m.nativeX) $('nativeX').value = m.nativeX;
  }
  fillDevices();
  if (measured) $('benchBox').open = false;
}

// benchmark
function benchSpec(shape) {
  return { arrays: shape.arrays.map(a => ({ C: a.C, B: a.B, K: a.K, D: a.D, headK: shape.a2Style ? 16 : 1 })) };
}
function runBench(worker, spec) {
  return new Promise((res, rej) => {
    const id = Math.random();
    worker.onmessage = e => { if (e.data.id === id) e.data.ok ? res(e.data) : rej(new Error(e.data.error)); };
    worker.postMessage({ id, spec, seconds: 1, sr: 48000 });
  });
}
async function bench() {
  const btn = $('benchBtn'), st = $('benchStatus');
  btn.disabled = true; st.classList.remove('err');
  let worker;
  try {
    worker = new Worker('bench.js');
    const rtf = {};
    let capacityGeneric = 0;
    for (const r of REFERENCE.slice(0, 3)) {
      st.textContent = `Running ${r.label}…`;
      const out = await runBench(worker, benchSpec(r.shape));
      rtf[r.label] = out.rtf;
      // units/s this device did on the A2-Full shape, counted the generic way (the fast-path factor is added on top later)
      if (r.label === 'A2-Full') capacityGeneric = unitsOf(r.shape, false) * 48000 * out.rtf;
    }
    store.set('measured', { capacityGeneric, rtf, when: Date.now() });
    st.textContent = `Done. In this browser: A2-Full ${rtf['A2-Full'].toFixed(1)}x real time, A2-Lite ${rtf['A2-Lite'].toFixed(1)}x, A1-standard ${rtf['A1-standard'].toFixed(1)}x (1x = just keeps up). "This device" is now in the device list.`;
    fillDevices(); $('device').value = 'measured'; renderMachine();
  } catch (e) {
    st.textContent = 'Test failed: ' + e.message; st.classList.add('err');
  } finally { worker && worker.terminate(); btn.disabled = false; }
}

// ─── 2. TONE3000 ──────────────────────────────────────────────────────────────

const redirectUri = location.origin + location.pathname;
$('redirectShow').textContent = redirectUri;

const b64url = bytes => btoa(String.fromCharCode(...bytes)).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
async function pkce() {
  const verifier = b64url(crypto.getRandomValues(new Uint8Array(32)));
  const state = b64url(crypto.getRandomValues(new Uint8Array(16)));
  const challenge = b64url(new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(verifier))));
  sess.set('t3k_verifier', verifier); sess.set('t3k_state', state);
  return { challenge, state };
}
async function authorize(extra = {}) {
  const key = $('pubKey').value.trim();
  const msg = /^t3k_cs_/.test(key) ? 'That is a secret key. Never put it in a web page: delete it here and use the publishable key (t3k_pub_…).'
    : !/^t3k_pub_/.test(key) ? 'That does not look like a publishable key (t3k_pub_…).' : '';
  if (msg) { alert(msg); return; }
  store.set('pubKey', key);
  const { challenge, state } = await pkce();
  const u = new URL(T3K + '/api/v1/oauth/authorize');
  const p = { client_id: key, redirect_uri: redirectUri, response_type: 'code', code_challenge: challenge, code_challenge_method: 'S256', state, ...extra };
  for (const [k, v] of Object.entries(p)) u.searchParams.set(k, v);
  location.href = u.toString();
}
async function tokenRequest(body) {
  const res = await fetch(T3K + '/api/v1/oauth/token', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams(body) });
  if (!res.ok) { const e = await res.json().catch(() => ({})); throw Object.assign(new Error(e.error || 'token request failed'), { status: res.status }); }
  const d = await res.json();
  const t = { access_token: d.access_token, refresh_token: d.refresh_token, expires_at: Date.now() + d.expires_in * 1000 };
  sess.set('t3k_tokens', t);
  return t;
}
async function handleCallback() {
  const qs = new URLSearchParams(location.search);
  if (!qs.has('code') && !qs.has('error') && !qs.has('canceled')) return;
  const state = sess.get('t3k_state'), verifier = sess.get('t3k_verifier');
  sess.del('t3k_state'); sess.del('t3k_verifier');
  history.replaceState(null, '', redirectUri + location.hash);
  if (qs.get('state') !== state) return status('searchStatus', 'Sign-in failed (state did not match). Try Connect again.', true);
  if (qs.get('error')) return status('searchStatus', 'TONE3000 said: ' + qs.get('error'), true);
  if (!qs.get('code')) return;
  try {
    await tokenRequest({ grant_type: 'authorization_code', code: qs.get('code'), code_verifier: verifier, redirect_uri: redirectUri, client_id: store.get('pubKey', '') });
    if (qs.get('tone_id')) pendingTone = qs.get('tone_id');
  } catch (e) { status('searchStatus', 'Sign-in failed: ' + e.message, true); }
}
let pendingTone = null, refreshing = null;
async function accessToken() {
  let t = sess.get('t3k_tokens');
  if (!t) throw new Error('Not connected');
  if (Date.now() > t.expires_at - 60000) {
    refreshing ||= tokenRequest({ grant_type: 'refresh_token', refresh_token: t.refresh_token, client_id: store.get('pubKey', '') })
      .catch(e => { if (e.status === 400 || e.status === 401) { sess.del('t3k_tokens'); showConnected(); } throw e; })
      .finally(() => { refreshing = null; });
    t = await refreshing;
  }
  return t.access_token;
}
async function api(path) {
  const res = await fetch(path.startsWith('http') ? path : T3K + path, { headers: { Authorization: 'Bearer ' + await accessToken() } });
  if (res.status === 429) throw new Error('TONE3000 rate limit reached. Wait a minute, or use Pick on TONE3000.');
  if (res.status === 401) { sess.del('t3k_tokens'); showConnected(); throw new Error('Signed out. Connect again.'); }
  if (!res.ok) throw new Error(`TONE3000 returned ${res.status}`);
  return res;
}
function showConnected() {
  const on = !!sess.get('t3k_tokens');
  $('t3kSetup').classList.toggle('hidden', on);
  $('t3kSearch').classList.toggle('hidden', !on);
  if (on) api('/api/v1/user').then(r => r.json()).then(u => { $('who').textContent = 'Signed in as ' + (u.display_name || u.username); }).catch(() => {});
}
function status(id, msg, err = false) { const el = $(id); el.textContent = msg; el.classList.toggle('err', err); }

// ranking: how much people liked it, with a nudge for verified creators
function toneScore(t) {
  const dl = t.downloads_count || 0, fav = t.favorites_count || 0;
  return Math.log10(dl + 1) + 1.6 * Math.log10(fav + 1) + (t.user?.is_verified ? 0.5 : 0);
}
let searchState = { page: 1, total_pages: 1, tones: [] };
function gearsChosen() { return [...document.querySelectorAll('#gears input:checked')].map(i => i.value); }

async function search(page = 1) {
  const sortSel = $('sort').value;
  const q = $('q').value.trim();
  const qs = new URLSearchParams({ architecture: '2', format: 'nam', page: String(page), page_size: '25' });
  if (q) qs.set('query', q);
  qs.set('sort', sortSel === 'ranked' ? (q ? 'best-match' : 'downloads-all-time') : sortSel);
  const gears = gearsChosen();
  if (gears.length) qs.set('gears', gears.join('_'));
  if ($('verified').checked) qs.set('verified', 'true');
  if ($('calibrated').checked) qs.set('calibrated', 'true');
  store.set('search', { q, sort: sortSel, gears, verified: $('verified').checked, calibrated: $('calibrated').checked });
  status('searchStatus', 'Searching…');
  $('searchBtn').disabled = true;
  try {
    const d = await (await api('/api/v1/tones/search?' + qs)).json();
    const tones = (d.data || []).filter(t => (t.a2_models_count ?? 1) > 0);
    searchState = { page: d.page, total_pages: d.total_pages, tones: page === 1 ? tones : [...searchState.tones, ...tones] };
    renderTones();
    status('searchStatus', `${d.total} tones with A2 models${sortSel === 'ranked' ? ', ranked by downloads and favorites' : ''}.`);
  } catch (e) { status('searchStatus', e.message, true); }
  finally { $('searchBtn').disabled = false; }
}

function renderTones() {
  let tones = [...searchState.tones];
  if ($('sort').value === 'ranked') tones.sort((a, b) => toneScore(b) - toneScore(a));
  $('results').innerHTML = tones.map(t => toneCard(t)).join('');
  $('moreBtn').classList.toggle('hidden', searchState.page >= searchState.total_pages);
}
function toneCard(t) {
  const by = t.user ? (t.user.display_name || t.user.username) : '';
  const makes = (t.makes || []).map(m => m.name).join(', ');
  return `<div class="tone" data-tone="${t.id}">
    <div class="top">
      <div>
        <h3><a href="${esc(t.url)}" target="_blank" rel="noopener">${esc(t.title)}</a> <span class="tag a2">A2</span></h3>
        <div class="meta">${esc(t.gear)}${makes ? ' · ' + esc(makes) : ''}${by ? ' · by ' + esc(by) + (t.user?.is_verified ? ' ✔' : '') : ''}</div>
      </div>
      <div class="score" title="log(downloads) + 1.6 × log(favorites) + verified bonus">★ ${toneScore(t).toFixed(1)}</div>
    </div>
    <div class="stats"><span>⬇ ${fmtN(t.downloads_count)}</span><span>♥ ${fmtN(t.favorites_count)}</span><span>${t.a2_models_count ?? '?'} A2 models</span></div>
    <div class="row"><button class="small" data-act="models">Show models</button></div>
    <div class="models"></div>
  </div>`;
}
async function toggleModels(card) {
  const box = card.querySelector('.models');
  if (box.classList.toggle('open') && !box.dataset.loaded) {
    box.innerHTML = '<div class="note">Loading…</div>';
    try {
      const d = await (await api(`/api/v1/models?tone_id=${card.dataset.tone}&architecture=2&page_size=100`)).json();
      const title = card.querySelector('h3 a').textContent;
      box.dataset.loaded = '1';
      box.innerHTML = (d.data || []).map(m => `<div class="mrow" data-url="${esc(m.model_url)}" data-name="${esc(m.name)}" data-tone="${esc(title)}">
        <span class="nm">${esc(m.name)}</span>
        <span><button class="small primary" data-act="play">▶ Play</button> <button class="small" data-act="check">Check</button> <button class="small" data-act="save">Save</button></span>
      </div>`).join('') || '<div class="note">No A2 models listed.</div>';
      if ((d.data || []).length > 1) box.insertAdjacentHTML('afterbegin', '<div class="row"><button class="small" data-act="checkall">Check all</button></div>');
    } catch (e) { box.innerHTML = `<div class="note" style="color:var(--bad)">${esc(e.message)}</div>`; }
  }
}
async function fetchModel(url) { return (await api(url)).text(); }
async function checkRemote(row) {
  const btn = row.querySelector('[data-act=check]');
  btn.disabled = true; btn.textContent = '…';
  try {
    const text = await fetchModel(row.dataset.url);
    addAnalysis(row.dataset.name, text, { source: row.dataset.tone, url: row.dataset.url });
    btn.textContent = 'Checked';
    $('check').scrollIntoView({ behavior: 'smooth', block: 'start' });
  } catch (e) { btn.textContent = 'Check'; btn.disabled = false; status('checkStatus', e.message, true); }
}
function saveText(name, text) {
  saveBlob(new Blob([text], { type: 'application/json' }), name.replace(/[^\w\- .()]+/g, '_').replace(/\.nam$/i, '') + '.nam');
}
async function playRemote(btn) {
  const row = btn.closest('.mrow');
  btn.disabled = true; btn.textContent = '…';
  try { addRig(row.dataset.name, await fetchModel(row.dataset.url), row.dataset.tone); }
  catch (e) { status('searchStatus', e.message, true); }
  finally { btn.disabled = false; btn.textContent = '▶ Play'; }
}
async function saveRemote(row) {
  try { saveText(row.dataset.name, await fetchModel(row.dataset.url)); }
  catch (e) { status('checkStatus', e.message, true); }
}

// Tone picked on the TONE3000 site (select flow): show its models
async function showPickedTone(id) {
  try {
    const t = await (await api(`/api/v1/tones/${id}?architecture=2`)).json();
    searchState = { page: 1, total_pages: 1, tones: [t] };
    renderTones();
    status('searchStatus', 'Tone picked on TONE3000:');
    toggleModels(document.querySelector(`.tone[data-tone="${t.id}"]`));
  } catch (e) { status('searchStatus', e.message, true); }
}

// ─── 3. Checked models ────────────────────────────────────────────────────────

let checked = [];
function addAnalysis(name, text, extra = {}) {
  try {
    const a = analyzeNam(text);
    checked = checked.filter(c => !(c.name === name && c.source === (extra.source || 'file')));
    checked.push({ name, text, a, source: extra.source || 'file', url: extra.url });
    status('checkStatus', `Read ${name}: ${a.label}.`);
  } catch (e) { status('checkStatus', `${name}: ${e.message}`, true); }
  renderRows();
}
// the largest size of a file that runs on this machine
function bestFit(a, s) {
  const js = a.variants.map(v => ({ v, ...judge(v, s) }));
  return js.find(x => x.st === 'ok') || js.find(x => x.st === 'warn') || js.at(-1);
}
function renderRows() {
  if (!checked.length) { $('tableWrap').classList.add('hidden'); $('clearBtn').classList.add('hidden'); return; }
  const s = settings();
  const rank = { ok: 0, warn: 1, bad: 2 };
  const rows = checked.map(c => ({ c, fit: bestFit(c.a, s) }));
  rows.sort((x, y) => rank[x.fit.st] - rank[y.fit.st]
    || (x.c.a.kind === 'A2' ? 0 : 1) - (y.c.a.kind === 'A2' ? 0 : 1)
    || (x.c.a.meta.esr ?? 9) - (y.c.a.meta.esr ?? 9)
    || variantUnits(x.fit.v) - variantUnits(y.fit.v));
  $('rows').innerHTML = rows.map(({ c, fit }, i) => {
    const m = c.a.meta;
    const gear = [m.make, m.model].filter(Boolean).join(' ') || m.gearType || '';
    const sizes = c.a.variants.map(v => { const j = judge(v, s); return `<span class="tag ${j.st}" title="${pct(j.share)}">${esc(v.label.replace(/^A2-/, ''))}</span>`; }).join(' ');
    return `<tr class="${i === 0 && fit.st === 'ok' ? 'best' : ''}">
      <td class="num">${i + 1}${i === 0 && fit.st === 'ok' ? ' ★' : ''}</td>
      <td>${esc(c.name)}<div class="note">${esc(c.source === 'file' ? 'local file' : c.source)}</div></td>
      <td>${c.a.kind === 'A2' ? '<span class="tag a2">A2</span>' : esc(c.a.label)}<div>${sizes}</div></td>
      <td>${esc(fit.v.label)}</td>
      <td class="num" style="color:${color(fit.st)}">${pct(fit.share)}</td>
      <td class="num">${m.esr != null ? m.esr.toFixed(4) : '–'}</td>
      <td class="num">${m.loudness != null ? m.loudness.toFixed(1) + ' dB' : '–'}</td>
      <td>${esc(gear)}${m.toneType ? `<div class="note">${esc(m.toneType)}</div>` : ''}</td>
      <td><button class="small primary" data-play="${checked.indexOf(c)}">▶ Play</button>${c.source !== 'file' ? ` <button class="small" data-save="${checked.indexOf(c)}">Save</button>` : ''}</td>
    </tr>`;
  }).join('');
  $('tableWrap').classList.remove('hidden'); $('clearBtn').classList.remove('hidden');
}
async function readFiles(list) {
  for (const f of list) addAnalysis(f.name, await f.text());
}

// ─── wiring ───────────────────────────────────────────────────────────────────

loadMachine();
for (const id of ['device', 'count', 'buffer', 'budget', 'fast']) $(id).addEventListener('input', renderMachine);
$('nativeX').addEventListener('input', () => { fillDevices(); renderMachine(); });
$('benchBtn').addEventListener('click', bench);

$('pubKey').value = store.get('pubKey', '');
$('connectBtn').addEventListener('click', () => authorize());
$('disconnectBtn').addEventListener('click', () => { sess.del('t3k_tokens'); showConnected(); });
$('pickBtn').addEventListener('click', () => {
  const extra = { prompt: 'select_tone', architecture: '2', format: 'nam', preview: 'true' };
  const g = gearsChosen(); if (g.length) extra.gears = g.join('_');
  if ($('calibrated').checked) extra.calibrated = 'true';
  authorize(extra);
});
$('searchBtn').addEventListener('click', () => search(1));
$('q').addEventListener('keydown', e => { if (e.key === 'Enter') search(1); });
$('sort').addEventListener('change', () => searchState.tones.length && search(1));
$('moreBtn').addEventListener('click', () => search(searchState.page + 1));
$('results').addEventListener('click', e => {
  const b = e.target.closest('button[data-act]'); if (!b) return;
  const act = b.dataset.act;
  if (act === 'models') { toggleModels(b.closest('.tone')); b.textContent = b.textContent === 'Show models' ? 'Hide models' : 'Show models'; }
  else if (act === 'check') checkRemote(b.closest('.mrow'));
  else if (act === 'play') playRemote(b);
  else if (act === 'save') saveRemote(b.closest('.mrow'));
  else if (act === 'checkall') b.closest('.models').querySelectorAll('.mrow').forEach(r => checkRemote(r));
});
const saved = store.get('search', null);
if (saved) {
  $('q').value = saved.q || ''; $('sort').value = saved.sort || 'ranked';
  document.querySelectorAll('#gears input').forEach(i => { i.checked = (saved.gears || ['amp']).includes(i.value); });
  $('verified').checked = !!saved.verified; $('calibrated').checked = !!saved.calibrated;
}

const drop = $('drop');
$('files').addEventListener('change', e => { readFiles([...e.target.files]); e.target.value = ''; });
drop.addEventListener('dragover', e => { e.preventDefault(); drop.classList.add('over'); });
drop.addEventListener('dragleave', () => drop.classList.remove('over'));
drop.addEventListener('drop', e => { e.preventDefault(); drop.classList.remove('over'); readFiles([...e.dataTransfer.files]); });
$('rows').addEventListener('click', e => {
  const b = e.target.closest('[data-save]'); if (b) { const c = checked[Number(b.dataset.save)]; saveText(c.name, c.text); }
  const p = e.target.closest('[data-play]'); if (p) { const c = checked[Number(p.dataset.play)]; addRig(c.name, c.text, c.source === 'file' ? 'local file' : c.source); }
});
initPlayer();
$('clearBtn').addEventListener('click', () => { checked = []; renderRows(); status('checkStatus', ''); });

renderMachine();
handleCallback().then(() => { showConnected(); if (pendingTone) showPickedTone(pendingTone); });

if ('serviceWorker' in navigator && location.protocol === 'https:' && !window.AndroidNam) navigator.serviceWorker.register('sw.js').catch(() => {});
