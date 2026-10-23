/* Sky Sequencer: live air traffic -> behaviours -> musical lanes, plus a heat grid read as a step sequencer. */
(() => {
  const $ = s => document.querySelector(s), el = (t, a = {}, ...kids) => { const e = document.createElement(t); for (const k in a) k === 'class' ? e.className = a[k] : k.startsWith('on') ? e[k] = a[k] : e.setAttribute(k, a[k]); for (const c of kids) e.append(c); return e; };
  const NOTE = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const SCALES = {minor:[0, 2, 3, 5, 7, 8, 10], major:[0, 2, 4, 5, 7, 9, 11], dorian:[0, 2, 3, 5, 7, 9, 10], lydian:[0, 2, 4, 6, 7, 9, 11], phrygian:[0, 1, 3, 5, 7, 8, 10],
    'pentatonic minor':[0, 3, 5, 7, 10], 'pentatonic major':[0, 2, 4, 7, 9], 'harmonic minor':[0, 2, 3, 5, 7, 8, 11], 'whole tone':[0, 2, 4, 6, 8, 10]};
  const PLACES = [['LHR', 'London Heathrow', 51.4700, -0.4543], ['LGW', 'London Gatwick', 51.1537, -0.1821], ['JFK', 'New York JFK', 40.6413, -73.7781], ['EWR', 'Newark', 40.6895, -74.1745], ['LAX', 'Los Angeles', 33.9416, -118.4085],
    ['ATL', 'Atlanta', 33.6407, -84.4277], ['ORD', 'Chicago O\'Hare', 41.9742, -87.9073], ['DFW', 'Dallas/Fort Worth', 32.8998, -97.0403], ['DEN', 'Denver', 39.8561, -104.6737], ['SFO', 'San Francisco', 37.6213, -122.3790],
    ['SEA', 'Seattle', 47.4502, -122.3088], ['MIA', 'Miami', 25.7959, -80.2870], ['BOS', 'Boston', 42.3656, -71.0096], ['LAS', 'Las Vegas', 36.0840, -115.1537], ['PHX', 'Phoenix', 33.4352, -112.0101], ['YYZ', 'Toronto', 43.6777, -79.6248],
    ['AMS', 'Amsterdam', 52.3105, 4.7683], ['CDG', 'Paris CDG', 49.0097, 2.5479], ['FRA', 'Frankfurt', 50.0379, 8.5622], ['MAD', 'Madrid', 40.4983, -3.5676], ['IST', 'Istanbul', 41.2753, 28.7519], ['DXB', 'Dubai', 25.2532, 55.3657],
    ['SIN', 'Singapore', 1.3644, 103.9915], ['HND', 'Tokyo Haneda', 35.5494, 139.7798], ['SYD', 'Sydney', -33.9399, 151.1753]];
  const LANES = [
    {id:'arp', name:'Circling', to:'Arpeggio', col:'#ff5ec8', engine:'pluck', ch:1, vol:0.8, rev:0.25, dly:0.35,
      desc:'A plane that turns a full circle (a holding stack, a helicopter orbit) plays an arpeggio. Faster turn = faster notes, right turn runs up, left turn runs down, a bigger circle spans more notes, higher planes play higher.'},
    {id:'rise', name:'Climbing', to:'Rising line', col:'#ffd23f', engine:'glass', ch:2, vol:0.7, rev:0.35, dly:0.25,
      desc:'Departures play a note every beat. The pitch is the plane\'s altitude, so the line climbs as the plane does. Faster climb = louder.'},
    {id:'fall', name:'Descending', to:'Falling line', col:'#4fc3ff', engine:'bell', ch:3, vol:0.65, rev:0.45, dly:0.3,
      desc:'Arrivals play a note every beat on the off-beat, pitched by altitude, so the line falls as they come down to land.'},
    {id:'drone', name:'Cruising', to:'Pad', col:'#9c8cff', engine:'pad', ch:4, vol:0.75, rev:0.6, dly:0.1,
      desc:'High, level traffic holds a chord tone for a whole bar. Its flight level picks the note; the nearest three planes make the chord.'},
    {id:'grid', name:'Sky grid', to:'Sequence', col:'#4dffa6', engine:'bass', ch:5, vol:0.75, rev:0.1, dly:0.15,
      desc:'The area is split into 16 × 8 cells that fill up with heat as traffic passes through them. Columns are steps (west to east), rows are notes (south low, north high). Busy airways become a repeating pattern that slowly changes.'},
    {id:'hit', name:'Low & close', to:'Percussion', col:'#ff8a3d', engine:'perc', ch:10, vol:0.7, rev:0.15, dly:0.1,
      desc:'Planes low and near you play a hit every beat. Every plane that enters the area plays one hit when it arrives, pitched by the direction it came from.'},
  ];
  const KIND_COL = {arp:'#ff5ec8', rise:'#ffd23f', fall:'#4fc3ff', drone:'#9c8cff', hit:'#ff8a3d', level:'#b9c4c0', ground:'#56615d'};
  const KIND_NAME = {arp:'circling', rise:'climbing', fall:'descending', drone:'cruising', hit:'low & close', level:'level', ground:'on the ground'};

  /* ---- settings ---- */
  const DEF = {source:Feed.native ? 'adsblol' : 'demo', place:'LHR', placeName:'London Heathrow', lat:51.47, lon:-0.4543, radius:45, localUrl:'http://192.168.1.50:8080/data/aircraft.json',
    bpm:96, root:9, scale:'minor', window:5, thresh:0.3, labels:true, trails:true, midiOut:'', clock:false, lanes:{}};
  let S, firstRun = false;
  try { const saved = localStorage.getItem('sky-seq'); firstRun = !saved; S = Object.assign({}, DEF, JSON.parse(saved || '{}')); } catch (e) { S = Object.assign({}, DEF); }
  for (const l of LANES) S.lanes[l.id] = Object.assign({on:true, engine:l.engine, ch:l.ch, vol:l.vol, rev:l.rev, dly:l.dly}, S.lanes[l.id] || {});
  const save = () => { try { localStorage.setItem('sky-seq', JSON.stringify(S)); } catch (e) {} };

  /* ---- tracking ---- */
  const planes = new Map();   // id -> plane
  const COLS = 16, ROWS = 8, heat = new Float32Array(COLS * ROWS);
  let lastUpdate = 0, feedInfo = {ok:true, live:false}, firstBatch = true, arrivals = [], frozen = false, emergency = false;
  const hash = s => { let h = 2166136261; for (const c of String(s)) h = Math.imul(h ^ c.charCodeAt(0), 16777619); return h >>> 0; };
  const wrap = d => ((d + 540) % 360) - 180;
  const kmLat = () => 110.574, kmLon = () => 111.32 * Math.cos(S.lat * Math.PI / 180);

  function classify(p){
    if (p.gnd || p.alt < 50) return 'ground';
    // full circle within the last 6 minutes, staying in one area -> circling
    const h = p.hist, since = p.t - 6 * 60e3; let turn = 0, cx = 0, cy = 0, n = 0;
    for (let i = 1; i < h.length; i++){ if (h[i].t < since) continue; if (h[i].t - h[i - 1].t < 90e3) turn += wrap(h[i].trk - h[i - 1].trk); cx += h[i].x; cy += h[i].y; n++; }
    p.turn = turn;
    if (n > 3 && Math.abs(turn) >= 300){
      cx /= n; cy /= n; let r = 0; for (let i = 0; i < h.length; i++) if (h[i].t >= since) r = Math.max(r, Math.hypot(h[i].x - cx, h[i].y - cy));
      if (r < 16){ p.orbitR = r; return 'arp'; }
    }
    if (p.vr > 400) return 'rise';
    if (p.vr < -400) return 'fall';
    if (p.alt < 4000 && p.dist < 10) return 'hit';
    if (p.alt >= 18000) return 'drone';
    return 'level';
  }
  // current turn rate in degrees per second (positive = right)
  function turnRate(p){
    if (p.trkRate != null && p.trkRate !== 0) return p.trkRate;
    const h = p.hist; let i = h.length - 1; while (i > 0 && p.t - h[i - 1].t < 40e3) i--;
    if (i >= h.length - 1) return 0; const dt = (h[h.length - 1].t - h[i].t) / 1000; return dt > 0 ? wrap(h[h.length - 1].trk - h[i].trk) / dt : 0;
  }

  function onData(list, info){
    feedInfo = info;
    if (!list){ renderStatus(); return; }
    const now = Date.now(), dt = lastUpdate ? Math.min(15, (now - lastUpdate) / 1000) : 0; lastUpdate = now;
    const seen = new Set(), R = S.radius;
    for (const a of list){
      const x = (a.lon - S.lon) * kmLon(), y = (a.lat - S.lat) * kmLat(), dist = Math.hypot(x, y);
      if (dist > R * 1.05) continue;
      seen.add(a.id);
      let p = planes.get(a.id);
      if (!p){ p = {id:a.id, hist:[], ph:hash(a.id) % 16, arpI:0, born:now}; planes.set(a.id, p);
        if (!firstBatch && !a.gnd && dist > R * 0.6) arrivals.push({x, y, bearing:(Math.atan2(x, y) * 180 / Math.PI + 360) % 360}); }
      if (a.past && !p.hist.length) for (const h of a.past) p.hist.push({t:h.t, x:(h.lon - S.lon) * kmLon(), y:(h.lat - S.lat) * kmLat(), trk:h.trk, alt:h.alt});
      delete a.past;
      Object.assign(p, a, {x, y, dist, seenAt:now});
      const last = p.hist[p.hist.length - 1];
      if (!last || a.t > last.t) p.hist.push({t:a.t, x, y, trk:a.trk, alt:a.alt});
      while (p.hist.length && p.hist[0].t < a.t - 8 * 60e3) p.hist.shift();
      p.kind = classify(p);
    }
    for (const [id, p] of planes) if (now - p.seenAt > 60e3) planes.delete(id);
    firstBatch = false;
    // heat grid: time each airborne plane spends in each cell, fading over the chosen window
    if (!frozen && dt > 0){
      const k = Math.exp(-dt / (S.window * 60)); for (let i = 0; i < heat.length; i++) heat[i] *= k;
      for (const p of planes.values()){ if (p.kind === 'ground' || !seen.has(p.id)) continue; const c = cellOf(p.x, p.y); if (c >= 0) heat[c] += dt; }
    }
    emergency = [...planes.values()].some(p => p.emg);
    renderStatus(); if (tab === 'planes') renderPlanes();
  }
  function cellOf(x, y){ const R = S.radius, c = Math.floor((x + R) / (2 * R) * COLS), r = Math.floor((y + R) / (2 * R) * ROWS); return c < 0 || c >= COLS || r < 0 || r >= ROWS ? -1 : r * COLS + c; }

  /* ---- music ---- */
  let playing = false, nextT = 0, stepN = 0, schedTimer = null, rec = null;
  const flashes = [];   // {t, id, col} for the radar
  const stepDur = () => 60 / S.bpm / 4;
  function deg2midi(deg, base){ const sc = SCALES[S.scale], L = sc.length, o = Math.floor(deg / L); return base + S.root + (emergency ? 5 : 0) + sc[((deg % L) + L) % L] + 12 * o; }
  function play(laneId, n, v, t, dur, p){
    const L = S.lanes[laneId]; if (!L.on) return;
    const pan = p ? Math.max(-1, Math.min(1, p.x / S.radius)) : 0;
    Sound.note(laneId, L.engine, n, v, t, dur, pan, L);
    if (Sound.midiActive()) Sound.midiNote(L.ch, n, v, t, dur);
    if (rec) rec.push({lane:laneId, ch:L.ch, n:Math.round(n), v, t:t - rec.t0, d:dur});
    flashes.push({t, id:p ? p.id : null, col:LANES.find(l => l.id === laneId).col, cell:p && p.cell});
    if (flashes.length > 300) flashes.splice(0, 100);   // the radar trims these, but not while it's hidden
  }
  const near = (list, k) => list.sort((a, b) => a.dist - b.dist).slice(0, k);
  const loud = p => 0.45 + 0.55 * Math.max(0, 1 - p.dist / S.radius);
  const ARP_CHORD = [0, 3, 4, 5, 2, 6, 1, 4];   // chord degree by the compass sector the plane circles in
  function playStep(n, t){
    const sd = stepDur(), s16 = n % 16, all = [...planes.values()].filter(p => p.seenAt > Date.now() - 30e3);
    const by = k => all.filter(p => p.kind === k);
    // circling -> arpeggio
    for (const p of near(by('arp'), 4)){
      const rate = Math.abs(turnRate(p)), every = rate >= 2.5 ? 1 : rate >= 1.2 ? 2 : 4;
      if ((n + p.ph) % every) continue;
      const span = p.orbitR < 3 ? 4 : p.orbitR < 7 ? 6 : 8, sector = Math.floor(((Math.atan2(p.x, p.y) * 180 / Math.PI + 360) % 360) / 45);
      const i = p.arpI++ % span, idx = turnRate(p) >= 0 ? i : span - 1 - i, octave = p.alt < 3000 ? 48 : p.alt < 12000 ? 60 : 72;
      play('arp', deg2midi(ARP_CHORD[sector] + idx * 2, octave), loud(p) * (i === 0 ? 1 : 0.8), t, sd * every * 0.85, p);
    }
    // climbing / descending -> pitch follows altitude
    for (const p of near(by('rise'), 4)) if ((n + p.ph) % 4 === 0) play('rise', deg2midi(Math.min(21, Math.floor(p.alt / 1500)), 55), Math.min(1, 0.45 + p.vr / 6000), t, sd * 2.5, p);
    for (const p of near(by('fall'), 4)) if ((n + p.ph) % 4 === 2) play('fall', deg2midi(Math.min(21, Math.floor(p.alt / 1200)), 50), Math.min(1, 0.45 - p.vr / 6000), t, sd * 3, p);
    // cruising -> one sustained chord tone per plane per bar
    if (s16 === 0){ const tones = [0, 2, 4, 6, 7, 9];
      near(by('drone'), 3).forEach(p => play('drone', deg2midi(tones[Math.floor(p.alt / 3000) % tones.length], 48), loud(p) * 0.9, t, sd * 16, p)); }
    // heat grid column -> notes
    let mx = 0; for (const h of heat) mx = Math.max(mx, h);
    if (mx > 0){ const col = s16, hits = [];
      for (let r = 0; r < ROWS; r++){ const v = heat[r * COLS + col] / mx; if (v >= S.thresh) hits.push([r, v]); }
      hits.sort((a, b) => b[1] - a[1]).slice(0, 3).forEach(([r, v]) => play('grid', deg2midi(r, 36), 0.35 + 0.65 * v, t, sd * 0.9, {x:((col + 0.5) / COLS * 2 - 1) * S.radius, id:null, cell:r * COLS + col})); }
    // low & close -> a hit each beat; arrivals -> one hit
    for (const p of near(by('hit'), 3)) if ((n + p.ph) % 4 === 0) play('hit', 38 + hash(p.type || p.id) % 14, loud(p), t, sd, p);
    if (s16 % 2 === 0 && arrivals.length){ const a = arrivals.shift(); play('hit', 45 + Math.floor(a.bearing / 30) * 2, 0.6, t, sd, {x:a.x, id:null}); }
    if (arrivals.length > 6) arrivals.splice(0, arrivals.length - 6);
    // emergency squawk: an alarm figure on the bell every bar
    if (emergency && s16 % 8 === 0) Sound.note('alert', 'bell', deg2midi(s16 ? 4 : 7, 72), 0.6, t, sd * 2, 0, {vol:0.5, rev:0.5, dly:0.4});
  }
  function scheduler(){
    const ctx = Sound.ctx; if (!ctx) return;
    if (nextT < ctx.currentTime) nextT = ctx.currentTime + 0.05;
    while (nextT < ctx.currentTime + 0.15){
      playStep(stepN, nextT);
      if (S.clock && Sound.midiActive()) for (let k = 0; k < 6; k++) Sound.midiSend([0xF8], nextT + k * stepDur() / 6);
      nextT += stepDur(); stepN++;
    }
  }
  function start(){
    const ctx = Sound.init(); ctx.resume(); Sound.setDelayTime(S.bpm);
    playing = true; stepN = 0; nextT = ctx.currentTime + 0.1;
    if (S.clock && Sound.midiActive()) Sound.midiSend([0xFA], nextT);
    schedTimer = setInterval(scheduler, 25); scheduler(); $('#play').classList.add('on'); $('#play').textContent = '■';
  }
  function stop(){
    playing = false; clearInterval(schedTimer);
    if (Sound.midiActive()){ const t = Sound.ctx.currentTime; if (S.clock) Sound.midiSend([0xFC], t); for (let c = 0; c < 16; c++) Sound.midiSend([0xB0 | c, 123, 0], t + 0.01); }
    $('#play').classList.remove('on'); $('#play').textContent = '▶';
  }

  /* ---- radar ---- */
  const cv = $('#radar'), g = cv.getContext('2d'); let sel = null, W = 0, sweep = 0;
  function fit(){ const r = cv.getBoundingClientRect(), d = window.devicePixelRatio || 1; W = r.width; cv.width = W * d; cv.height = W * d; g.setTransform(d, 0, 0, d, 0, 0); }
  new ResizeObserver(fit).observe(cv);
  function pos(p){ // dead-reckon between updates so motion is smooth
    const el = Math.min(15, Math.max(0, (Date.now() - p.t) / 1000)), km = p.gnd ? 0 : p.gs * 1.852 / 3600 * el, a = p.trk * Math.PI / 180;
    return [p.x + Math.sin(a) * km, p.y + Math.cos(a) * km];
  }
  function draw(){
    requestAnimationFrame(draw); if (!W) return;
    const R = S.radius, half = W / 2, sc = half / R, X = x => half + x * sc, Y = y => half - y * sc, now = Sound.ctx ? Sound.ctx.currentTime : 0;
    g.fillStyle = '#04100c'; g.fillRect(0, 0, W, W);
    // heat grid
    let mx = 0; for (const h of heat) mx = Math.max(mx, h);
    const cw = W / COLS, ch = W / ROWS, col = playing ? (stepN - 1 + 16) % 16 : -1;
    for (let r = 0; r < ROWS; r++) for (let c = 0; c < COLS; c++){
      const v = mx ? heat[r * COLS + c] / mx : 0, on = v >= S.thresh;
      g.fillStyle = on ? `rgba(77,255,166,${0.1 + v * 0.45})` : `rgba(77,255,166,${v * 0.12})`;
      g.fillRect(c * cw + 1, (ROWS - 1 - r) * ch + 1, cw - 2, ch - 2);
    }
    if (col >= 0 && S.lanes.grid.on){ g.strokeStyle = 'rgba(77,255,166,.55)'; g.lineWidth = 1.5; g.strokeRect(col * cw + 0.5, 0.5, cw - 1, W - 1); }
    for (const f of flashes) if (f.cell != null && f.id == null && now - f.t < 0.25 && now >= f.t){ const c = f.cell % COLS, r = Math.floor(f.cell / COLS); g.fillStyle = 'rgba(200,255,225,.6)'; g.fillRect(c * cw + 1, (ROWS - 1 - r) * ch + 1, cw - 2, ch - 2); }
    // rings + compass
    g.strokeStyle = 'rgba(120,255,190,.18)'; g.lineWidth = 1;
    for (let k = 1; k <= 4; k++){ g.beginPath(); g.arc(half, half, half * k / 4, 0, Math.PI * 2); g.stroke(); }
    g.beginPath(); g.moveTo(half, 0); g.lineTo(half, W); g.moveTo(0, half); g.lineTo(W, half); g.stroke();
    g.fillStyle = 'rgba(150,255,200,.5)'; g.font = '11px ui-monospace,monospace'; g.textAlign = 'center';
    g.fillText('N', half, 12); g.fillText('S', half, W - 4); g.textAlign = 'left'; g.fillText('W', 3, half - 4); g.textAlign = 'right'; g.fillText('E', W - 3, half - 4);
    g.textAlign = 'left'; g.fillText(Math.round(R) + ' km radius', 6, W - 4);
    // sweep
    sweep = (sweep + 0.012) % (Math.PI * 2);
    const grd = g.createConicGradient ? g.createConicGradient(sweep - Math.PI / 2, half, half) : null;
    if (grd){ grd.addColorStop(0, 'rgba(120,255,190,.0)'); grd.addColorStop(0.92, 'rgba(120,255,190,0)'); grd.addColorStop(1, 'rgba(120,255,190,.16)'); g.fillStyle = grd; g.beginPath(); g.arc(half, half, half, 0, Math.PI * 2); g.fill(); }
    g.fillStyle = '#7fffc0'; g.beginPath(); g.arc(half, half, 3, 0, Math.PI * 2); g.fill();
    // planes
    const lit = {}; for (const f of flashes) if (f.id && now >= f.t && now - f.t < 0.3) lit[f.id] = f;
    while (flashes.length && now - flashes[0].t > 1) flashes.shift();
    for (const p of planes.values()){
      const [x, y] = pos(p), px = X(x), py = Y(y), c = p.emg && (Date.now() / 300 | 0) % 2 ? '#ff3b3b' : KIND_COL[p.kind] || '#ccc';
      if (p.gnd && p !== sel){ g.fillStyle = c; g.fillRect(px - 1.5, py - 1.5, 3, 3); continue; }   // parked and taxiing traffic stays out of the way
      if (S.trails && p.hist.length > 1){ g.strokeStyle = c + '66'; g.lineWidth = 1.2; g.beginPath(); p.hist.forEach((h, i) => i ? g.lineTo(X(h.x), Y(h.y)) : g.moveTo(X(h.x), Y(h.y))); g.lineTo(px, py); g.stroke(); }
      const f = lit[p.id];
      if (f){ const k = (now - f.t) / 0.3; g.strokeStyle = f.col; g.globalAlpha = 1 - k; g.lineWidth = 2; g.beginPath(); g.arc(px, py, 6 + k * 16, 0, Math.PI * 2); g.stroke(); g.globalAlpha = 1; }
      g.save(); g.translate(px, py); g.rotate(p.trk * Math.PI / 180); g.fillStyle = c; g.beginPath();
      g.moveTo(0, -7); g.lineTo(5, 5); g.lineTo(0, 2.5); g.lineTo(-5, 5); g.closePath(); g.fill(); g.restore();
      if (p === sel){ g.strokeStyle = '#fff'; g.lineWidth = 1.5; g.beginPath(); g.arc(px, py, 11, 0, Math.PI * 2); g.stroke(); }
      if (S.labels || p === sel){ g.fillStyle = c; g.font = '10px ui-monospace,monospace'; g.textAlign = 'left'; g.fillText(p.cs, px + 8, py - 2); g.fillStyle = 'rgba(220,255,235,.55)'; g.fillText(p.gnd ? 'GND' : 'FL' + String(Math.round(p.alt / 100)).padStart(3, '0'), px + 8, py + 9); }
    }
    if (emergency){ g.fillStyle = '#ff3b3b'; g.font = 'bold 12px system-ui'; g.textAlign = 'center'; g.fillText('EMERGENCY SQUAWK: key up a fourth', half, 28); }
  }
  cv.addEventListener('click', e => {
    const r = cv.getBoundingClientRect(), mx = e.clientX - r.left, my = e.clientY - r.top, half = W / 2, sc = half / S.radius; let best = null, bd = 26;
    for (const p of planes.values()){ const [x, y] = pos(p), d = Math.hypot(half + x * sc - mx, half - y * sc - my); if (d < bd){ bd = d; best = p; } }
    sel = best; renderSel();
  });
  function renderSel(){
    const b = $('#sel'); if (!sel || !planes.has(sel.id)){ b.hidden = true; sel = null; return; }
    const p = sel; b.hidden = false;
    b.innerHTML = `<b style="color:${KIND_COL[p.kind]}">${p.cs || p.id}</b> ${p.type || ''} · ${p.gnd ? 'on the ground' : Math.round(p.alt).toLocaleString() + ' ft'} · ${Math.round(p.gs)} kt · ${Math.round(p.trk)}° · ${p.vr > 0 ? '+' : ''}${Math.round(p.vr)} ft/min · ${p.dist.toFixed(1)} km away<br><span>${KIND_NAME[p.kind]}${p.kind === 'arp' ? ` (turned ${Math.round(Math.abs(p.turn))}° ${p.turn > 0 ? 'right' : 'left'})` : ''}${p.sq ? ' · squawk ' + p.sq : ''}${p.emg ? ' · EMERGENCY' : ''}</span>`;
  }
  setInterval(renderSel, 1000);

  /* ---- UI ---- */
  let tab = 'lanes';
  function renderStatus(){
    const src = Feed.SOURCES[S.source], ago = lastUpdate ? Math.round((Date.now() - lastUpdate) / 1000) : null, st = $('#status');
    if (!feedInfo.ok){
      st.className = 'status bad';
      st.innerHTML = `Feed error: ${feedInfo.error}. ${Feed.native ? 'Check your connection or try another feed.' : 'Browsers block the public flight feeds, so live traffic needs the Android app (or your own receiver on this computer). '}<button id="toDemo">Use demo sky</button>`;
      $('#toDemo').onclick = () => { S.source = 'demo'; save(); $('#source').value = 'demo'; restartFeed(); };
    } else { st.className = 'status'; st.textContent = `${src.live ? 'LIVE' : 'DEMO'} · ${src.name.replace(/ \(.*/, '')} · ${planes.size} aircraft${ago != null ? ` · updated ${ago}s ago` : ''}`; }
    const counts = {}; for (const p of planes.values()) counts[p.kind] = (counts[p.kind] || 0) + 1;
    $('#legend').innerHTML = Object.keys(KIND_NAME).map(k => `<span style="--c:${KIND_COL[k]}"><i></i>${KIND_NAME[k]} <b>${counts[k] || 0}</b></span>`).join('');
  }
  setInterval(renderStatus, 1000);
  // keep = leave the Sky tab as it is (so a list of search results stays up)
  function setPlace(lat, lon, name, code = '', keep = false){
    S.lat = +(+lat).toFixed(4); S.lon = +(+lon).toFixed(4); S.place = code; S.placeName = name || `${S.lat}, ${S.lon}`;
    save(); restartFeed(); renderWhere(); toast('Centred on ' + S.placeName);
    if (tab !== 'sky') return;
    if (!keep) render();
    else { const n = $('#nowPlace'); if (n) n.textContent = 'Now: ' + S.placeName; }
  }
  function renderWhere(){ const w = $('#where'); w.textContent = '📍 ' + (S.placeName || `${S.lat}, ${S.lon}`); w.title = `${S.lat}, ${S.lon} · tap to change`; }
  function restartFeed(){ planes.clear(); heat.fill(0); lastUpdate = 0; firstBatch = true; arrivals = []; sel = null; Feed.start(S, onData); renderStatus(); }

  const opt = (v, t, cur) => { const o = el('option', {value:v}, t); if (String(v) === String(cur)) o.selected = true; return o; };
  function header(){
    const key = $('#key'), scale = $('#scale');
    NOTE.forEach((n, i) => key.append(opt(i, n, S.root))); Object.keys(SCALES).forEach(s => scale.append(opt(s, s, S.scale)));
    key.onchange = () => { S.root = +key.value; save(); }; scale.onchange = () => { S.scale = scale.value; save(); };
    const bpm = $('#bpm'); bpm.value = S.bpm; bpm.onchange = () => { S.bpm = Math.max(40, Math.min(220, +bpm.value || 96)); bpm.value = S.bpm; Sound.setDelayTime(S.bpm); save(); };
    $('#play').onclick = () => playing ? stop() : start();
    $('#rec').onclick = () => {
      if (rec){ const r = rec; rec = null; $('#rec').classList.remove('on'); exportMidi(r); return; }
      if (!playing) start(); rec = []; rec.t0 = nextT; $('#rec').classList.add('on');
    };
    document.querySelectorAll('#tabs button').forEach(b => b.onclick = () => { tab = b.dataset.t; document.querySelectorAll('#tabs button').forEach(x => x.classList.toggle('on', x === b)); render(); });
  }
  function exportMidi(r){
    if (!r.length){ toast('Nothing was recorded'); return; }
    const tracks = LANES.map(l => ({name:l.name + ' - ' + l.to, notes:r.filter(n => n.lane === l.id && n.t >= 0)})).filter(t => t.notes.length);
    const bytes = Sound.smf(tracks, S.bpm), d = new Date(), name = `sky-${(S.place || S.placeName || 'here').replace(/[^\w]+/g, '-').slice(0, 24)}-${d.toISOString().slice(0, 16).replace(/[-:T]/g, '')}.mid`;
    if (window.SkyNative && SkyNative.saveFile){ let b = ''; for (const x of bytes) b += String.fromCharCode(x); toast(SkyNative.saveFile(name, btoa(b), 'audio/midi')); return; }
    const a = el('a', {href:URL.createObjectURL(new Blob([bytes], {type:'audio/midi'})), download:name}); document.body.append(a); a.click(); a.remove(); toast('Saved ' + name);
  }
  function toast(m){ const t = $('#toast'); t.textContent = m; t.hidden = false; clearTimeout(toast.k); toast.k = setTimeout(() => t.hidden = true, 3500); }

  function render(){
    const m = $('#panel'); m.innerHTML = '';
    if (tab === 'lanes') renderLanes(m); else if (tab === 'sky') renderSky(m); else if (tab === 'midi') renderMidi(m); else if (tab === 'planes') renderPlanes(); else renderAbout(m);
  }
  function range(label, min, max, step, val, fmt, on){
    const v = el('b', {}, fmt(val)), i = el('input', {type:'range', min, max, step}); i.value = val;
    i.oninput = () => { v.textContent = fmt(+i.value); on(+i.value); }; i.onchange = save;
    return el('label', {class:'ctl'}, el('span', {}, label), i, v);
  }
  function renderLanes(m){
    for (const l of LANES){
      const L = S.lanes[l.id], eng = el('select'), chs = el('select');
      for (const k in Sound.ENGINES) eng.append(opt(k, Sound.ENGINES[k].name, L.engine));
      for (let c = 1; c <= 16; c++) chs.append(opt(c, 'Ch ' + c, L.ch));
      eng.onchange = () => { L.engine = eng.value; save(); }; chs.onchange = () => { L.ch = +chs.value; save(); };
      const tog = el('button', {class:'sw' + (L.on ? ' on' : '')}, L.on ? 'On' : 'Off');
      tog.onclick = () => { L.on = !L.on; tog.classList.toggle('on', L.on); tog.textContent = L.on ? 'On' : 'Off'; save(); };
      m.append(el('div', {class:'lane', style:`--c:${l.col}`},
        el('div', {class:'lhead'}, el('h3', {}, l.name, el('small', {}, ' → ' + l.to)), tog),
        el('p', {class:'hint'}, l.desc),
        el('div', {class:'row'}, el('label', {}, 'Sound ', eng), el('label', {}, 'MIDI ', chs)),
        range('Volume', 0, 1.5, 0.01, L.vol, v => Math.round(v * 100), v => L.vol = v),
        range('Reverb', 0, 1, 0.01, L.rev, v => Math.round(v * 100), v => L.rev = v),
        range('Delay', 0, 1, 0.01, L.dly, v => Math.round(v * 100), v => L.dly = v)));
    }
  }
  function renderSky(m){
    const src = el('select', {id:'source'}); for (const k in Feed.SOURCES) src.append(opt(k, Feed.SOURCES[k].name, S.source));
    src.onchange = () => { S.source = src.value; save(); restartFeed(); render(); };
    const place = el('select'); place.append(opt('', 'Pick an airport', S.place)); for (const [c, n] of PLACES) place.append(opt(c, `${c} · ${n}`, S.place));
    const lat = el('input', {type:'number', step:'0.0001'}), lon = el('input', {type:'number', step:'0.0001'}); lat.value = S.lat; lon.value = S.lon;
    place.onchange = () => { const p = PLACES.find(x => x[0] === place.value); if (p) setPlace(p[2], p[3], p[1], p[0]); };
    const setLL = () => { const a = +lat.value, b = +lon.value; if (lat.value !== '' && lon.value !== '' && Math.abs(a) <= 90 && Math.abs(b) <= 180) setPlace(a, b, ''); else toast('Latitude is -90 to 90, longitude -180 to 180'); };
    lat.onchange = setLL; lon.onchange = setLL;
    const q = el('input', {type:'search', placeholder:'Any place, address or airport, or coordinates', style:'flex:1;min-width:200px', enterkeyhint:'search'});
    const results = el('div', {class:'results'}), find = el('button', {}, 'Find');
    const doFind = async () => {
      const text = q.value.trim(); if (!text) return;
      const c = Feed.parseCoords(text); results.innerHTML = '';
      if (c){ setPlace(c.lat, c.lon, ''); return; }
      find.textContent = 'Searching…';
      try {
        const list = await Feed.searchPlaces(text);
        if (!list.length){ toast('Nothing found for "' + text + '"'); return; }
        setPlace(list[0].lat, list[0].lon, list[0].name, '', true);
        if (list.length > 1) list.forEach((p, i) => { const b = el('button', {class:'sw' + (i ? '' : ' on')}, p.name); b.onclick = () => { setPlace(p.lat, p.lon, p.name, '', true); results.querySelectorAll('button').forEach(x => x.classList.toggle('on', x === b)); }; results.append(b); });
      } catch (e) { toast('Search failed: ' + e.message + (Feed.native ? '' : ' (check your connection)')); }
      finally { find.textContent = 'Find'; }
    };
    find.onclick = doFind; q.onkeydown = e => { if (e.key === 'Enter') doFind(); };
    const gps = el('button', {}, '◎ GPS');
    gps.onclick = () => { if (!navigator.geolocation){ toast('GPS location is not available here, try "Internet"'); return; } gps.textContent = 'Finding you…';
      navigator.geolocation.getCurrentPosition(p => { gps.textContent = '◎ GPS'; setPlace(+p.coords.latitude.toFixed(4), +p.coords.longitude.toFixed(4), 'My location'); },
        e => { gps.textContent = '◎ GPS'; toast('Could not get your location: ' + e.message + '. Try "Internet" or search.'); }, {enableHighAccuracy:false, timeout:15000, maximumAge:600000}); };
    const ip = el('button', {}, '⌖ Internet');
    ip.onclick = async () => { ip.textContent = 'Finding you…'; try { const p = await Feed.locateByIp(); setPlace(p.lat, p.lon, p.name + ' (approximate)'); } catch (e) { toast('Could not find you from your connection. Search or type coordinates instead.'); } ip.textContent = '⌖ Internet'; };
    const local = el('input', {type:'url', value:S.localUrl, style:'flex:1;min-width:220px'}); local.onchange = () => { S.localUrl = local.value.trim(); save(); if (S.source === 'local') restartFeed(); };
    const win = el('select'); for (const w of [1, 2, 5, 10, 15, 30, 60]) win.append(opt(w, w + ' min', S.window)); win.onchange = () => { S.window = +win.value; save(); };
    const frz = el('button', {class:'sw' + (frozen ? ' on' : '')}, frozen ? 'Frozen' : 'Freeze grid'); frz.onclick = () => { frozen = !frozen; frz.classList.toggle('on', frozen); frz.textContent = frozen ? 'Frozen' : 'Freeze grid'; };
    const lab = el('button', {class:'sw' + (S.labels ? ' on' : '')}, 'Labels'); lab.onclick = () => { S.labels = !S.labels; lab.classList.toggle('on', S.labels); save(); };
    const trl = el('button', {class:'sw' + (S.trails ? ' on' : '')}, 'Trails'); trl.onclick = () => { S.trails = !S.trails; trl.classList.toggle('on', S.trails); save(); };
    const feedCard = el('div', {class:'lane'},
      el('h3', {}, 'Feed'), el('div', {class:'row'}, src),
      S.source === 'local' ? el('div', {class:'row'}, el('span', {class:'hint'}, 'Receiver address'), local) : '',
      el('p', {class:'hint'}, Feed.native ? 'adsb.lol, airplanes.live and adsb.fi are free community ADS-B networks. OpenSky updates every 10 seconds. With an RTL-SDR stick running readsb or dump1090 on your network, pick "My own receiver" and enter its aircraft.json address.'
        : 'In a browser the public feeds are blocked (they don\'t allow requests from other websites), so use the Android app for live traffic. Your own receiver works if it runs on this computer (http://localhost…).'));
    m.append(el('div', {class:'lane'}, el('h3', {}, 'Where'), el('p', {class:'hint', id:'nowPlace'}, 'Now: ' + (S.placeName || `${S.lat}, ${S.lon}`)),
        el('div', {class:'row'}, q, find), results,
        el('p', {class:'hint'}, 'Search any town, address or landmark, or type coordinates (51.47, -0.45 or 51.47N 0.45W), or paste a Google/Apple Maps link.'),
        el('div', {class:'row'}, el('span', {class:'hint'}, 'Find me:'), gps, ip), el('div', {class:'row'}, place),
        el('div', {class:'row'}, el('label', {}, 'Lat ', lat), el('label', {}, 'Lon ', lon)),
        range('Radius', 5, 150, 1, S.radius, v => v + ' km', v => { S.radius = v; clearTimeout(renderSky.k); renderSky.k = setTimeout(restartFeed, 400); })),
      feedCard,
      el('div', {class:'lane'}, el('h3', {}, 'Grid'), el('div', {class:'row'}, el('label', {}, 'Collect over ', win), frz, el('button', {onclick:() => heat.fill(0)}, 'Clear grid')),
        range('Threshold', 0.05, 0.9, 0.01, S.thresh, v => Math.round(v * 100) + '%', v => S.thresh = v),
        el('p', {class:'hint'}, 'Cells hotter than the threshold (compared with the hottest cell) play. Lower = busier pattern.'),
        el('div', {class:'row'}, lab, trl)));
  }
  function renderMidi(m){
    const out = el('select'); out.append(opt('', 'Off (internal sounds only)', S.midiOut));
    for (const o of Sound.midi.list) out.append(opt(o.id, o.name, S.midiOut));
    out.onchange = () => { S.midiOut = out.value; Sound.midiSelect(S.midiOut); save(); };
    const clk = el('button', {class:'sw' + (S.clock ? ' on' : '')}, 'Send MIDI clock'); clk.onclick = () => { S.clock = !S.clock; clk.classList.toggle('on', S.clock); save(); };
    m.append(el('div', {class:'lane'}, el('h3', {}, 'MIDI out'), el('div', {class:'row'}, out, el('button', {onclick:() => { Sound.midiRefresh(); render(); }}, 'Refresh'), clk),
      el('p', {class:'hint'}, Sound.midi.list.length ? 'Each lane sends on its own channel (set it on the Lanes tab), so every behaviour can drive a different synth. Set a lane\'s sound to "MIDI only" to hear just the external synth.'
        : Feed.native ? 'No MIDI devices found. Plug in a USB MIDI interface or synth. To drive a computer DAW, connect the phone by USB and set Android\'s USB mode to "MIDI", then pick the USB port here.'
        : 'No MIDI outputs found (or this browser has no Web MIDI). Chrome on a computer can send to a virtual port (IAC Driver on Mac, loopMIDI on Windows) and into your DAW.')),
      el('div', {class:'lane'}, el('h3', {}, 'Record to a MIDI file'), el('p', {class:'hint'}, 'Press ● in the top bar to start recording, press it again to stop and save a .mid file with one track per lane. Drag it into your DAW.')));
  }
  function renderPlanes(){
    if (tab !== 'planes') return; const m = $('#panel');
    const list = [...planes.values()].sort((a, b) => a.gnd - b.gnd || a.dist - b.dist);
    m.innerHTML = `<div class="lane"><table><tr><th>Flight</th><th>Type</th><th>Alt ft</th><th>kt</th><th>km</th><th>Doing</th></tr>${list.map(p => `<tr data-id="${p.id}"><td style="color:${KIND_COL[p.kind]}">${p.cs || p.id}</td><td>${p.type || ''}</td><td>${p.gnd ? 'GND' : Math.round(p.alt).toLocaleString()}</td><td>${Math.round(p.gs)}</td><td>${p.dist.toFixed(1)}</td><td>${KIND_NAME[p.kind]}${p.emg ? ' ⚠' : ''}</td></tr>`).join('') || '<tr><td colspan="6" class="hint">No aircraft yet</td></tr>'}</table></div>`;
    m.querySelectorAll('tr[data-id]').forEach(r => r.onclick = () => { sel = planes.get(r.dataset.id); renderSel(); window.scrollTo({top:0, behavior:'smooth'}); });
  }
  function renderAbout(m){
    m.append(el('div', {class:'lane'}, el('h3', {}, 'How it works'),
      el('p', {class:'hint'}, 'Sky Sequencer watches live air traffic around a point and turns what the planes are doing into music in time with the tempo. Each plane keeps a few minutes of its track, and from that it gets a behaviour: circling, climbing, descending, cruising, or low and close. Each behaviour is a lane with its own sound and MIDI channel.'),
      el('p', {class:'hint'}, 'The green grid is the sky over time: every cell collects heat while planes pass through it, and the sequencer reads it column by column, west to east, 16 steps per bar. An emergency squawk (7500, 7600, 7700) shifts the key up a fourth and adds an alarm figure until it clears.'),
      el('p', {class:'hint'}, 'Positions come from ADS-B receivers run by volunteers (adsb.lol, airplanes.live, adsb.fi, OpenSky Network). Tap a plane on the radar for its details. Busy places with holding stacks, like London Heathrow, have the most circling traffic.')));
  }

  header(); render(); renderStatus(); renderWhere(); requestAnimationFrame(draw);
  $('#where').onclick = () => document.querySelector('#tabs button[data-t=sky]').click();
  // first open: centre on the listener from their internet connection (no permission prompt); GPS and search are on the Sky tab
  if (firstRun) Feed.locateByIp().then(p => setPlace(p.lat, p.lon, p.name + ' (approximate)')).catch(() => {});
  Sound.midiInit(() => { if (tab === 'midi') render(); }).then(() => { if (S.midiOut) Sound.midiSelect(S.midiOut); if (tab === 'midi') render(); });
  Feed.start(S, onData);
  if ('serviceWorker' in navigator && !Feed.native && location.protocol === 'https:') navigator.serviceWorker.register('sw.js').catch(() => {});
  document.addEventListener('keydown', e => { if (e.key === ' ' && e.target === document.body){ e.preventDefault(); playing ? stop() : start(); } });
})();
