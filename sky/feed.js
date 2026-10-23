/* Live air traffic: feed adapters that all return the same plane records, plus a simulated sky. */
const Feed = (() => {
  // In the Android app the requests go through the app (the public feeds don't allow browser requests from other sites)
  const native = window.SkyNative || null;
  const waiting = {}; let seq = 0;
  window.__skyReply = (id, ok, body) => { const w = waiting[id]; if (!w) return; delete waiting[id]; ok ? w.res(body) : w.rej(new Error(body)); };
  async function getJSON(url){
    if (native){
      const body = await new Promise((res, rej) => { const id = 'r' + (++seq); waiting[id] = {res, rej}; native.fetch(id, url); setTimeout(() => { if (waiting[id]){ delete waiting[id]; rej(new Error('timed out')); } }, 15000); });
      return JSON.parse(body);
    }
    const r = await fetch(url, {cache:'no-store'});
    if (!r.ok) throw new Error('HTTP ' + r.status);
    return r.json();
  }

  const KM_NM = 1.852, FT_M = 3.28084, KT_MS = 1.94384;
  // readsb / tar1090 JSON (adsb.lol, airplanes.live, adsb.fi, your own receiver); also older dump1090 field names
  function fromReadsb(j){
    const now = (j.now ? j.now * 1000 : Date.now()), list = j.ac || j.aircraft || [];
    return list.filter(a => a.lat != null && a.lon != null).map(a => {
      const alt = a.alt_baro ?? a.altitude ?? a.alt_geom, gnd = alt === 'ground';
      return {id:a.hex, cs:(a.flight || a.r || a.hex || '').trim(), type:a.t || '', lat:a.lat, lon:a.lon,
        alt:gnd ? 0 : (+alt || 0), gnd, gs:a.gs ?? a.speed ?? 0, trk:a.track ?? a.true_heading ?? a.mag_heading ?? 0,
        vr:a.baro_rate ?? a.geom_rate ?? a.vert_rate ?? 0, trkRate:a.track_rate ?? null, sq:a.squawk || '',
        emg:(a.emergency && a.emergency !== 'none') || ['7500', '7600', '7700'].includes(a.squawk), t:now - (a.seen_pos || a.seen || 0) * 1000};
    });
  }
  function fromOpenSky(j){
    return (j.states || []).filter(s => s[5] != null && s[6] != null).map(s => ({id:s[0], cs:(s[1] || s[0]).trim(), type:'', lat:s[6], lon:s[5],
      alt:s[8] ? 0 : (s[7] ?? s[13] ?? 0) * FT_M, gnd:!!s[8], gs:(s[9] || 0) * KT_MS, trk:s[10] || 0, vr:(s[11] || 0) * FT_M * 60,
      trkRate:null, sq:s[14] || '', emg:['7500', '7600', '7700'].includes(s[14]), t:(s[3] || j.time) * 1000}));
  }

  const SOURCES = {
    adsblol:  {name:'adsb.lol (live, every 2 s)', every:2000, live:true, url:(c) => `https://api.adsb.lol/v2/point/${c.lat.toFixed(4)}/${c.lon.toFixed(4)}/${Math.min(250, Math.ceil(c.radius / KM_NM))}`, parse:fromReadsb},
    airplanes:{name:'airplanes.live (live, every 3 s)', every:3000, live:true, url:(c) => `https://api.airplanes.live/v2/point/${c.lat.toFixed(4)}/${c.lon.toFixed(4)}/${Math.min(250, Math.ceil(c.radius / KM_NM))}`, parse:fromReadsb},
    adsbfi:   {name:'adsb.fi (live, every 3 s)', every:3000, live:true, url:(c) => `https://opendata.adsb.fi/api/v2/lat/${c.lat.toFixed(4)}/lon/${c.lon.toFixed(4)}/dist/${Math.min(250, Math.ceil(c.radius / KM_NM))}`, parse:fromReadsb},
    opensky:  {name:'OpenSky Network (live, every 10 s)', every:10000, live:true, url:(c) => { const dl = c.radius / 110.574, dn = c.radius / (111.32 * Math.cos(c.lat * Math.PI / 180));
      return `https://opensky-network.org/api/states/all?lamin=${(c.lat - dl).toFixed(4)}&lomin=${(c.lon - dn).toFixed(4)}&lamax=${(c.lat + dl).toFixed(4)}&lomax=${(c.lon + dn).toFixed(4)}`; }, parse:fromOpenSky},
    local:    {name:'My own receiver (readsb / dump1090)', every:1000, live:true, url:(c) => c.localUrl, parse:fromReadsb},
    demo:     {name:'Demo sky (simulated)', every:1000, live:false},
  };

  /* ---- Demo sky: overflights, departures, arrivals, holding stacks, a helicopter orbit ---- */
  const sim = {planes:[], last:0, n:0, started:false};
  const AIRLINES = ['BAW', 'DLH', 'AFR', 'KLM', 'UAL', 'AAL', 'EZY', 'RYR', 'VIR', 'SWR', 'IBE', 'UAE'], TYPES = ['A320', 'A20N', 'B738', 'B77W', 'A359', 'B789', 'A388', 'E190', 'A21N', 'B38M'];
  const rnd = (a, b) => a + Math.random() * (b - a), pick = a => a[Math.floor(Math.random() * a.length)];
  function spawn(kind, R){
    const p = {kind, id:'sim' + (++sim.n).toString(16), cs:pick(AIRLINES) + Math.floor(rnd(10, 999)), type:pick(TYPES), sq:String(Math.floor(rnd(1000, 6999)))};
    const edge = (deg) => { const a = deg * Math.PI / 180; return [Math.sin(a) * R * 0.98, Math.cos(a) * R * 0.98]; };
    if (kind === 'over'){ const b = rnd(0, 360); [p.x, p.y] = edge(b); p.hdg = (b + 180 + rnd(-35, 35)) % 360; p.alt = Math.round(rnd(28, 41)) * 1000; p.gs = rnd(420, 520); p.vr = 0; }
    else if (kind === 'dep'){ p.x = rnd(-2, 2); p.y = rnd(-2, 2); p.hdg = pick([90, 270]) + rnd(-40, 40); p.alt = 300; p.gs = 170; p.vr = rnd(1800, 3000); }
    else if (kind === 'arr'){ const b = rnd(0, 360); [p.x, p.y] = edge(b); p.hdg = (b + 180) % 360; p.alt = rnd(9000, 16000); p.gs = 280; p.vr = -rnd(900, 1500); }
    else if (kind === 'hold'){ const a = rnd(0, Math.PI * 2), d = R * rnd(0.35, 0.7); p.cx = Math.sin(a) * d; p.cy = Math.cos(a) * d; p.r = rnd(4, 7); p.ang = rnd(0, 360); p.dir = Math.random() < 0.5 ? 1 : -1; p.alt = Math.round(rnd(7, 14)) * 1000; p.gs = 220; p.vr = 0; p.life = rnd(240, 600); }
    else if (kind === 'heli'){ p.cs = pick(['POL', 'HEMS', 'NEWS']) + Math.floor(rnd(1, 99)); p.type = pick(['EC35', 'A109', 'H145']); p.cx = rnd(-R / 3, R / 3); p.cy = rnd(-R / 3, R / 3); p.r = rnd(1, 2); p.ang = rnd(0, 360); p.dir = 1; p.alt = rnd(1200, 2200); p.gs = 90; p.vr = 0; p.life = rnd(200, 400); }
    if (p.cx != null){ p.x = p.cx + Math.sin(p.ang * Math.PI / 180) * p.r; p.y = p.cy + Math.cos(p.ang * Math.PI / 180) * p.r; p.hdg = (p.ang + 90 * p.dir + 360) % 360; }
    return p;
  }
  function simStep(cfg){
    const now = Date.now(), dt = sim.last ? Math.min(5, (now - sim.last) / 1000) : 0, R = cfg.radius; sim.last = now;
    const want = {over:5, dep:2, arr:3, hold:3, heli:1}, have = {};
    sim.planes = sim.planes.filter(p => Math.hypot(p.x, p.y) < R * 1.02 && !(p.life != null && (p.life -= dt) < 0) && !(p.kind === 'arr' && p.alt < 200) && !(p.kind === 'dep' && p.alt > 24000));
    for (const p of sim.planes) have[p.kind] = (have[p.kind] || 0) + 1;
    // a full sky straight away, then new traffic now and then
    for (const k in want) for (let i = have[k] || 0; i < want[k]; i++) if (!sim.started || Math.random() < 0.015) sim.planes.push(spawn(k, R));
    sim.started = true;
    for (const p of sim.planes){
      const km = p.gs * KM_NM / 3600 * dt;
      if (p.cx != null){ p.ang = (p.ang + p.dir * km / p.r * 180 / Math.PI + 360) % 360; p.x = p.cx + Math.sin(p.ang * Math.PI / 180) * p.r; p.y = p.cy + Math.cos(p.ang * Math.PI / 180) * p.r; p.hdg = (p.ang + 90 * p.dir + 360) % 360; p.trkRate = p.dir * p.gs * KM_NM / 3600 / p.r * 180 / Math.PI; }
      else { p.x += Math.sin(p.hdg * Math.PI / 180) * km; p.y += Math.cos(p.hdg * Math.PI / 180) * km; p.alt += p.vr / 60 * dt;
        if (p.kind === 'arr'){ const toC = (Math.atan2(-p.x, -p.y) * 180 / Math.PI + 360) % 360, d = ((toC - p.hdg + 540) % 360) - 180; p.hdg = (p.hdg + Math.max(-3 * dt, Math.min(3 * dt, d)) + 360) % 360; p.gs = Math.max(140, p.gs - dt * 0.6); }
        if (p.kind === 'dep') p.gs = Math.min(450, p.gs + dt * 1.5); }
    }
    const kmLat = 110.574, kmLon = 111.32 * Math.cos(cfg.lat * Math.PI / 180);
    return sim.planes.map(p => {
      const a = {id:p.id, cs:p.cs, type:p.type, lat:cfg.lat + p.y / kmLat, lon:cfg.lon + p.x / kmLon, alt:Math.max(0, p.alt), gnd:false, gs:p.gs, trk:p.hdg, vr:p.vr, trkRate:p.trkRate ?? 0, sq:p.sq, emg:false, t:now};
      // orbiting planes arrive with the last few minutes of their track, so the demo doesn't wait a whole lap to spot them circling
      if (p.cx != null && !p.sent){ p.sent = true; a.past = [];
        const w = p.dir * p.gs * KM_NM / 3600 / p.r * 180 / Math.PI;
        for (let s = 360; s > 0; s -= 5){ const ang = (p.ang - w * s + 3600) % 360, r = ang * Math.PI / 180, x = p.cx + Math.sin(r) * p.r, y = p.cy + Math.cos(r) * p.r;
          a.past.push({t:now - s * 1000, lat:cfg.lat + y / kmLat, lon:cfg.lon + x / kmLon, trk:(ang + 90 * p.dir + 360) % 360, alt:p.alt}); } }
      return a;
    });
  }

  /* ---- finding a place: search by name (OpenStreetMap), locate by internet connection, or coordinates ---- */
  async function searchPlaces(q){
    const j = await getJSON('https://nominatim.openstreetmap.org/search?format=jsonv2&limit=8&q=' + encodeURIComponent(q.trim()));
    return (Array.isArray(j) ? j : []).map(r => ({name:r.display_name, lat:+r.lat, lon:+r.lon}));
  }
  async function locateByIp(){
    try { const j = await getJSON('https://ipwho.is/'); if (j.success) return {name:[j.city, j.region, j.country].filter(Boolean).join(', '), lat:+j.latitude, lon:+j.longitude}; } catch (e) {}
    const j = await getJSON('https://get.geojs.io/v1/ip/geo.json');
    if (j && j.latitude) return {name:[j.city, j.country].filter(Boolean).join(', '), lat:+j.latitude, lon:+j.longitude};
    throw new Error('no answer');
  }
  // "51.47, -0.45", "51.47N 0.45W", map links (…/@51.47,-0.45,12z, ?q=51.47,-0.45, geo:51.47,-0.45); null if it's a place name
  function parseCoords(text){
    let s = String(text).trim();
    const link = /https?:|@|geo:/i.test(s);
    if (link){ const m = s.match(/@(-?\d+(?:\.\d+)?),\s*(-?\d+(?:\.\d+)?)/) || s.match(/(?:[?&](?:q|ll|query|center)=|geo:)(-?\d+(?:\.\d+)?)(?:,|%2C)\s*(-?\d+(?:\.\d+)?)/i); if (!m) return null; s = m[1] + ',' + m[2]; }
    else if (!/^[\d.\-+\s,;:\/°NSEWnsew]+$/.test(s)) return null;
    const parts = [...s.matchAll(/([-+]?\d+(?:\.\d+)?)\s*°?\s*([NSEWnsew])?/g)].slice(0, 2).map(m => (/[SWsw]/.test(m[2] || '') ? -Math.abs(+m[1]) : +m[1]));
    if (parts.length < 2 || Math.abs(parts[0]) > 90 || Math.abs(parts[1]) > 180) return null;
    return {lat:parts[0], lon:parts[1]};
  }

  let timer = null, busy = false;
  // calls onData(planes, info) on every update; info = {ok, live, error}
  function start(cfg, onData){
    stop(); sim.planes = []; sim.last = 0; sim.started = false;
    const src = SOURCES[cfg.source] || SOURCES.demo;
    const tick = async () => {
      if (busy) return;
      if (!src.live){ onData(simStep(cfg), {ok:true, live:false}); return; }
      busy = true;
      try { const url = src.url(cfg); if (!url) throw new Error('No receiver address set'); onData(src.parse(await getJSON(url)), {ok:true, live:true}); }
      catch (e) { onData(null, {ok:false, live:true, error:(e && e.message) || String(e)}); }
      finally { busy = false; }
    };
    tick(); timer = setInterval(tick, src.every);
  }
  function stop(){ clearInterval(timer); timer = null; }
  return {SOURCES, start, stop, native:!!native, searchPlaces, locateByIp, parseCoords};
})();
