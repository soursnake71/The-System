/* Omega Five: faceplate, LCD menus, AudioWorklet host, MIDI, files. ES module. */
const $ = s => document.querySelector(s);
const P = () => $('#p');
function el(tag, cls, css, html){ const e = document.createElement(tag); if (cls) e.className = cls; if (css) Object.assign(e.style, css); if (html != null) e.innerHTML = html; return e; }

/* ================= panel geometry (from the photo, 1300 x 973) ================= */
const W = 1300, H = 973;
const GX = k => 98.3 + 32.9 * k;
const LEFT_SL = [['VOLUME', GX(0), 0], ['CLICK', GX(1), 1], ['TEMPO', GX(2), 0], ['DATA', GX(3), 1]];
const CH_X = Array.from({length:12}, (_, i) => GX(23 + i));
const CH_LBL = ['BD', 'SD', 'RIM', 'TOM 1', 'TOM 2', 'TOM 3', 'TOM 4', 'HI HAT', 'RIDE', 'CRASH', 'TMBRN<br>CLAPS', 'SHKR<br>CWBEL'];
const MODE_KEYS = [['PTN', 140], ['SONG', 191], ['EDIT\nVOICE', 243], ['CAS', 294], ['CART', 345], ['MIDI', 396], ['REV', 447], ['DAMP', 497], ['DLY', 547],
  ['MULTI', 820], ['INT\nSYNC', 921], ['MIDI\nSYNC', 972], ['TAPE\nSYNC', 1022], ['EXT\nSYNC', 1073], ['RUN', 1174]];
const FX = [328, 423, 518, 613, 707], FY = [571, 606, 641, 676];
const FKEYS = [
  ['PATTERN', 'SONG'], ['REAL TIME WRITE', 'EDIT SONG'], ['STEP WRITE', 'INSERT'], ['EDIT VOICE'], ['KEY ASSIGN'],
  ['QUANTIZE', 'DELETE'], ['SWING', 'REPEAT'], ['CLICK', 'TEMPO CHANGE'], ['CHAIN'], ['CASSETTE', 'CARTRIDGE'],
  ['EDIT PATTERN', 'VOLUME CHANGE'], ['CLEAR'], ['COPY'], ['SYNC'], ['MIDI'],
  ['REVERSE'], ['DAMP'], ['TEMPO'], ['UTILITY'], ['JOB']];
const KX = [800, 893, 987];
const KEYPAD = [['7', '1/32'], ['8', '1/48'], ['9', 'OFF'], ['4', '1/12'], ['5', '1/16'], ['6', '1/24'], ['1', '1/4'], ['2', '1/6'], ['3', '1/8'], ['-1', 'NO'], ['0', '1/2'], ['+1', 'YES']];
const PAD_X = [135, 231, 327, 422, 517, 612, 707, 801, 896, 991, 1085, 1180];
const PAD_LBL = ['BD', 'SD', 'RIM', 'TOM 1', 'TOM 2', 'TOM 3', 'TOM 4', 'HI HAT', 'RIDE', 'CRASH', 'TAMBOURINE<br>CLAPS', 'SHAKER<br>COWBELL'];
// printed legend: [x, [header, menu id, [[text, item] ...]] ...]; an item may be a list of [text, item] shown on one line
const LEGEND = [
  [128, [['SONG', 'song', [['01 SEARCH MARK', 0]]], ['EDIT SONG', 'esong', [['01 SEARCH MARK', 0], ['02 SET MARK', 1], ['03 SONG NAME', 2], ['04 INITIAL TEMPO', 3]]]]],
  [200, [['EDIT VOICE', 'voice', [['01 SELECT VOICE', 0], ['02 PITCH', 1], ['03 EG', 2], [[['( 1 AR', 2], [' 2 D1R', 3], [' 3 D1L', 4]]], [[['  4 D2R', 5], [' 5 RR', 6], [' 6 GT)', 7]]], ['04 BEND', 8], [[['( 1 RATE', 8], [' 2 RANGE)', 9]]]]]]],
  [292, [[' ', 'voice', [['05 VOICE LEVEL', 10], ['06 LOOP', 11], ['07 STORE VOICE', 12], ['08 INITIALIZE VOICE', 13], ['09 RECALL EDIT', 14], ['10 REVERSE', 15], ['11 USER WAV', 16]]]]],
  [372, [['CHAIN', 'chain', [['01 PLAY', 0], ['02 EDIT', 1], ['03 CHAIN NAME', 2], ['04 CLEAR', 3]]]]],
  [430, [['UTILITY', 'util', [['01 REMAINING PATTERN', 0], ['02 REMAINING SONG', 1], ['03 MEMORY PROTECT', 2], ['04 CHANGE VOICE', 3], ['05 CLEAR ALL PATTERNS', 4], ['06 CLEAR ALL SONGS', 5]]]]],
  [521, [['KEY ASSIGN', 'key', [['01 VOICE ASSIGN', 0], ['02 PAN', 1], ['03 MULTI VOICE', 2], ['04 MULTI STEP', 3], ['05 ACCENT LEVEL', 4], ['06 OUTPUT CH ASSIGN', 5]]]]],
  [604, [[' ', 'key', [['07 SAVE KEY DATA', 6], ['08 LOAD KEY DATA', 7], ['09 COPY VOICE', 8], ['10 REV SEND', 9], ['11 DLY SEND', 10]]]]],
  [688, [['CASSETTE/CARTRIDGE', 'cart', [['01-04 SAVE SLOT', 0], ['11-14 LOAD SLOT', 4], ['21 SAVE FILE', 8], ['22 LOAD FILE', 9], ['23 LOAD WAV VOICE', 10], ['24 SYSEX FILE', 11], ['30 FORMAT SLOT', 13]]]]],
  [790, [['MIDI', 'midi', [['01 CH MESSAGE', 0], ['02 RECEIVE CH', 1], ['03 TRANSMIT CH', 2], ['04 NOTE NUMBER ASSIGN', 3], ['05 INITIALIZE NOTE', 4], ['06 EG VELOCITY', 5], ['07 GATE TIME', 6]]]]],
  [893, [[' ', 'midi', [['08 RECEIVE BULK', 7], ['09 DEVICE NUMBER', 8], ['10 TRANSMIT BULK', 9], ['11 CLOCK OUT', 10], ['12 NOTES OUT', 11], ['13 INPUT PORT', 12], ['14 OUTPUT PORT', 13]]]]],
  [972, [['JOB', 'job', [['01 IMPORT MIDI', 0], ['02 EXPORT MIDI', 1], ['03 EXPORT WAV', 2], ['04 RANDOMIZE', 3], ['05 INIT ALL', 4]]]]],
];

const VOICE_NAMES = ['BD 1', 'SD 1', 'RIM 1', 'TOM 1A', 'TOM 2A', 'TOM 3A', 'TOM 4A', 'HH CLS', 'RIDE', 'CRASH', 'TAMB', 'SHAKER',
  'BD 2', 'SD 2', 'RIM 2', 'TOM 1B', 'TOM 2B', 'TOM 3B', 'TOM 4B', 'HH OPN', 'RIDE CP', 'CHINA', 'CLAPS', 'COWBELL'];
const QUANT = [['1/2', 48], ['1/4', 24], ['1/6', 16], ['1/8', 12], ['1/12', 8], ['1/16', 6], ['1/24', 4], ['1/32', 3], ['1/48', 2], ['1/96', 1], ['OFF', 1]];
const KEY_Q = {0:0, 1:1, 2:2, 3:3, 4:4, 5:5, 6:6, 7:7, 8:8, 9:10};

/* ================= build the faceplate ================= */
const sliders = {};
function buildPanel(){
  const sl = $('#sliders'), p = P();
  // wells cut into the ribs, then the sliders and their printed names on the lip
  // the ribs: grooves every 32.9 px, the sliders run in grooves 0-3 and 23-34 (cut wider there, with scale notches)
  const slotG = k => (k >= 0 && k <= 3) || (k >= 23 && k <= 34);
  for (let k = -3; k <= 36; k++){
    const g = GX(k), left = g + 4.5, w = 24;
    if (slotG(k) || slotG(k + 1)){
      p.insertBefore(el('div', 'rib cut', {left:left + 'px', width:w + 'px'}), sl);
      const nl = slotG(k) ? left + 3 : left, nw = w - (slotG(k) ? 3 : 0) - (slotG(k + 1) ? 3 : 0);
      p.insertBefore(el('div', 'rib lo', {left:nl + 'px', width:nw + 'px'}), sl);
    } else p.insertBefore(el('div', 'rib', {left:left + 'px', width:w + 'px'}), sl);
  }
  for (const [name, x, lo] of LEFT_SL){
    makeSlider(name, x - 15, 160, 138);
    p.appendChild(el('div', 'sl', {left:x + 'px', top:(lo ? 326 : 316) + 'px'}, name));
  }
  CH_X.forEach((x, i) => { makeSlider('CH' + i, x - 15, 160, 138); p.appendChild(el('div', 'sl', {left:x + 'px', top:'317px', lineHeight:'1.1'}, CH_LBL[i])); });
  const mk = $('#modekeys');
  for (const [name, x] of MODE_KEYS){
    const b = el('button', 'mk', {left:x + 'px'}); b.textContent = name; b.dataset.k = name.replace('\n', ' ');
    mk.appendChild(b);
  }
  // legend
  const lg = $('#legend');
  for (const [x, groups] of LEGEND){
    const col = el('div', 'legend', {left:x + 'px', top:'460px'});
    for (const [h, m, items] of groups){
      const hd = el('b', h.trim() ? 'h' : '', null, h.trim() || '&nbsp;'); hd.dataset.m = m; col.appendChild(hd);
      for (const it of items){
        if (Array.isArray(it[0])){ const line = el('em'); for (const [t, n] of it[0]){ const u = el('u', '', {display:'inline'}, t.replace(/ /g, '&nbsp;')); u.dataset.m = m; u.dataset.i = n; line.appendChild(u); } col.appendChild(line); }
        else { const u = el('u', '', null, it[0]); u.dataset.m = m; u.dataset.i = it[1]; col.appendChild(u); }
      }
    }
    lg.appendChild(col);
  }
  // lcd cells
  for (const r of [0, 1]){ const row = $('#lcd' + r); for (let i = 0; i < 16; i++) row.appendChild(el('i')); }
  // accent, function keys, keypad, transport
  const k = $('#keys');
  const a1 = el('button', 'big acc', {left:'95px', top:'563px'}, '<small>Y</small>ACCENT 1'); a1.dataset.k = 'ACC1';
  const a2 = el('button', 'big acc', {left:'95px', top:'634px'}, '<small>Z</small>ACCENT 2'); a2.dataset.k = 'ACC2';
  k.append(a1, a2, el('div', 'sl', {left:'136px', top:'696px', fontSize:'11px'}, '&harr;'));
  FKEYS.forEach((lb, i) => {
    const b = el('button', 'fk', {left:FX[i % 5] + 'px', top:FY[i / 5 | 0] + 'px'}, lb.map(t => `<span>${t}</span>`).join(''));
    b.dataset.k = lb[0]; k.appendChild(b);
  });
  k.append(el('div', 'rule', {left:'566px', top:'558px', width:'1px', height:'132px'}), el('div', 'rule', {left:'283px', top:'658px', width:'283px', height:'1px'}),
    el('div', 'rule', {left:'283px', top:'658px', width:'1px', height:'8px'}), el('div', 'sl', {left:'389px', top:'657px', fontSize:'10px'}, '&bull;'));
  KEYPAD.forEach(([n, q], i) => {
    const red = n === '-1' || n === '+1';
    const b = el('button', 'kp' + (red ? ' red' : ''), {left:KX[i % 3] + 'px', top:FY[i / 3 | 0] + 'px'});
    b.textContent = red ? `${n}    ${q}` : `${n}      ${q}`; b.dataset.k = red ? (n === '-1' ? 'NO' : 'YES') : 'N' + n; k.appendChild(b);
  });
  const st = el('button', 'big stop', {left:'1140px', top:'562px'}, 'STOP/<br>CONTINUE'); st.dataset.k = 'STOP';
  const go = el('button', 'big start', {left:'1140px', top:'634px'}, 'START'); go.dataset.k = 'START';
  k.append(st, go, el('div', 'sl', {left:'1181px', top:'624px'}, 'SHIFT/COMPARE'), el('div', 'sl', {left:'1181px', top:'696px'}, 'ENTER'));
  // pads
  const pd = $('#pads');
  for (let i = 0; i < 24; i++){
    const b = el('div', 'pad', {left:PAD_X[i % 12] + 'px', top:(i < 12 ? 721 : 794) + 'px'}, `<b>${String.fromCharCode(65 + i)}</b><i class="fl"></i>`);
    b.dataset.pad = i; pd.appendChild(b);
  }
  PAD_X.forEach((x, i) => pd.appendChild(el('div', 'pl', {left:x + 'px', top:'867px'}, `${PAD_LBL[i]}<br>${i + 1}`)));
}
function makeSlider(id, left, top, h){
  const s = el('div', 'slider', {left:left + 'px', top:top + 'px', height:h + 'px'});
  const cap = el('div', 'cap'); s.appendChild(cap);
  s.dataset.s = id; $('#sliders').appendChild(s);
  sliders[id] = {el:s, cap, h, v:0};
}
function setSlider(id, v){ const s = sliders[id]; if (!s) return; s.v = Math.max(0, Math.min(1, v)); s.cap.style.top = (22 + (s.h - 40) * (1 - s.v)) + 'px'; }

/* ================= fit to the screen ================= */
let scale = 1;
function fit(){
  const vw = innerWidth, vh = innerHeight; scale = Math.min(vw / W, vh / H);
  P().style.transform = `translate(${Math.round((vw - W * scale) / 2)}px, ${Math.round((vh - H * scale) / 2)}px) scale(${scale})`;
  $('#hint').style.display = vh > vw * 1.2 ? 'block' : 'none';
}
buildPanel(); for (const k in sliders) setSlider(k, .7); fit(); addEventListener('resize', fit);
