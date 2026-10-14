/* Drawings of each unit (my own illustrations of their front panels and colours, no photos or logos).
   gearSVG(id) returns an inline SVG, viewBox 0 0 120 64. */
const GEAR = (() => {
  const keys = (x, y, w, h, n, black = '#111', white = '#f2eee4') => {
    let s = '', kw = w / n; for (let i = 0; i < n; i++) s += `<rect x="${(x + i * kw).toFixed(2)}" y="${y}" width="${(kw - .4).toFixed(2)}" height="${h}" fill="${white}" rx=".4"/>`;
    for (let i = 0; i < n - 1; i++) if ([0, 1, 3, 4, 5].includes(i % 7)) s += `<rect x="${(x + (i + 1) * kw - kw * .3).toFixed(2)}" y="${y}" width="${(kw * .6).toFixed(2)}" height="${h * .6}" fill="${black}"/>`;
    return s; };
  const knob = (x, y, r, c = '#1a1a1a', cap = '#ddd') => `<circle cx="${x}" cy="${y}" r="${r}" fill="${c}"/><line x1="${x}" y1="${y}" x2="${x}" y2="${y - r * .8}" stroke="${cap}" stroke-width="${Math.max(.6, r * .25)}" stroke-linecap="round"/>`;
  const row = (n, f) => Array.from({length:n}, (_, i) => f(i)).join('');
  const slider = (x, y, h, cap) => `<rect x="${x - .5}" y="${y}" width="1" height="${h}" fill="#000"/><rect x="${x - 1.6}" y="${y + h * .35}" width="3.2" height="2.4" fill="${cap}" rx=".4"/>`;
  return {
    d808:`<rect x="2" y="8" width="116" height="48" rx="3" fill="#2a2a2a"/><rect x="2" y="8" width="116" height="20" rx="3" fill="#3a3a38"/>
      ${row(11, i => knob(10 + i * 10, 15, 2.6, '#e9e2cf', '#333') + knob(10 + i * 10, 23, 2.2, '#e9e2cf', '#333'))}
      <rect x="6" y="31" width="108" height="3" fill="#cfc6ae"/>
      ${row(16, i => `<rect x="${6 + i * 6.8}" y="38" width="5.6" height="9" rx=".8" fill="${['#d8332a', '#ef7d22', '#f4c51e', '#f1ebdb'][Math.floor(i / 4)]}"/>`)}
      <rect x="6" y="50" width="108" height="2" fill="#555"/>`,
    dmx:`<rect x="2" y="10" width="116" height="44" rx="3" fill="#161616"/><rect x="8" y="15" width="24" height="9" rx="1" fill="#300"/><text x="10" y="22.5" font-size="7" font-family="monospace" fill="#ff3b2f">120</text>
      ${row(5, i => knob(40 + i * 9, 19.5, 2.4, '#555', '#eee'))}<rect x="88" y="15" width="24" height="9" rx="1" fill="#2b2b2b"/>
      ${row(10, i => `<rect x="${8 + i * 10.6}" y="30" width="8.6" height="8" rx="1" fill="#d9d9d4"/>`)}${row(10, i => `<rect x="${8 + i * 10.6}" y="41" width="8.6" height="8" rx="1" fill="#9c9c96"/>`)}`,
    tb303:`<rect x="6" y="8" width="108" height="48" rx="6" fill="#c9c9c2"/><rect x="6" y="8" width="108" height="16" rx="6" fill="#b7b7b0"/>
      ${row(7, i => knob(18 + i * 13, 16, 3.6, '#202020', '#eee'))}
      ${row(13, i => `<rect x="${12 + i * 7.4}" y="${[1, 3, 6, 8, 10].includes(i) ? 30 : 38}" width="6" height="6" rx="1" fill="${[1, 3, 6, 8, 10].includes(i) ? '#222' : '#f5f3ec'}" stroke="#888" stroke-width=".3"/>`)}
      <rect x="12" y="47" width="96" height="4" rx="1" fill="#9a9a92"/>`,
    proone:`<rect x="2" y="10" width="10" height="46" rx="2" fill="#7a4a24"/><rect x="108" y="10" width="10" height="46" rx="2" fill="#7a4a24"/>
      <rect x="12" y="10" width="96" height="22" fill="#1d1d1d"/>${row(9, i => knob(18 + i * 10.5, 16, 2.4, '#bdbdbd', '#222') + knob(18 + i * 10.5, 25, 2.4, '#bdbdbd', '#222'))}
      <rect x="12" y="32" width="96" height="24" fill="#111"/>${keys(13, 33, 94, 22, 22)}`,
    sh101:`<rect x="2" y="12" width="116" height="44" rx="3" fill="#8c9096"/><rect x="2" y="12" width="116" height="18" rx="3" fill="#a3a7ad"/>
      ${row(12, i => slider(10 + i * 8.6, 15, 12, '#f2f2f2'))}<rect x="108" y="16" width="6" height="8" fill="#333"/>
      ${keys(6, 32, 108, 22, 22, '#1c1c1c', '#f5f4ef')}`,
    juno:`<rect x="2" y="10" width="116" height="46" rx="2" fill="#191919"/><rect x="2" y="10" width="116" height="22" fill="#232323"/><rect x="2" y="30" width="116" height="1.2" fill="#c23b2f"/>
      ${row(16, i => slider(8 + i * 6.8, 13, 13, ['#e5e5e5', '#e5e5e5', '#f08a24', '#f08a24', '#f08a24', '#d33a2c', '#d33a2c', '#d33a2c', '#d33a2c', '#d33a2c', '#f2d23a', '#f2d23a', '#f2d23a', '#f2d23a', '#e5e5e5', '#e5e5e5'][i]))}
      ${keys(6, 33, 108, 22, 22)}`,
    rs09:`<rect x="2" y="10" width="116" height="46" rx="2" fill="#262422"/><rect x="2" y="10" width="116" height="20" fill="#2f2c29"/>
      ${row(8, i => `<rect x="${8 + i * 7}" y="15" width="5" height="9" rx="1" fill="${i < 4 ? '#f0ece2' : '#e6b23a'}"/>`)}${row(5, i => knob(72 + i * 9, 19.5, 2.6, '#111', '#eee'))}
      ${keys(6, 32, 108, 23, 22)}`,
    pbass:`<defs><radialGradient id="sb" cx="42%" cy="52%" r="62%"><stop offset="0" stop-color="#f4b63c"/><stop offset=".5" stop-color="#b8561c"/><stop offset="1" stop-color="#220e06"/></radialGradient></defs>
      <path d="M58 27C55 22 53 13 47 12 42 11 41 18 35 18 29 18 26 13 17 14 8 15 4 23 4 32s4 17 13 18c9 1 12-4 18-4s7 7 12 6c6-1 8-10 11-15z" fill="url(#sb)"/>
      <path d="M53 25c-3-4-5-9-9-9-3 0-4 5-10 6-6 1-9-3-14-1-5 2-7 7-6 12 1 4 4 6 8 5 6-1 7-7 13-8 5-1 9 1 12 2z" fill="#3b1a0e" opacity=".9"/>
      <rect x="22" y="27.5" width="7" height="3.2" rx="1.2" fill="#161616"/><rect x="28" y="32.5" width="7" height="3.2" rx="1.2" fill="#161616"/>
      <rect x="8" y="29" width="7" height="6" rx="1" fill="#9a9a9a"/>
      <circle cx="44" cy="44" r="2" fill="#cfcfcf"/><circle cx="50" cy="42" r="2" fill="#cfcfcf"/>
      <rect x="56" y="29.3" width="50" height="5.4" fill="#d6ad72"/>${row(10, i => `<rect x="${60 + i * 4.8}" y="29.3" width=".6" height="5.4" fill="#c9c9c9"/>`)}<circle cx="82" cy="32" r=".8" fill="#222"/>
      <path d="M105 29h4c3-3 7-4 10-3v2c-2 1-2 3 0 4v3l-14 1z" fill="#d6ad72"/>${row(4, i => `<circle cx="${107.5 + i * 3.1}" cy="27.2" r="1.3" fill="#d5d5d5"/>`)}
      ${row(4, i => `<line x1="10" y1="${30.5 + i * 1}" x2="${108 + i * 3}" y2="${30.5 + i * 1 - (i * .3)}" stroke="#f1f1f1" stroke-width=".28"/>`)}`,
    voc:`<rect x="1" y="16" width="8" height="32" rx="1" fill="#3a3a3a"/><rect x="111" y="16" width="8" height="32" rx="1" fill="#3a3a3a"/><circle cx="5" cy="22" r="1.4" fill="#111"/><circle cx="5" cy="42" r="1.4" fill="#111"/><circle cx="115" cy="22" r="1.4" fill="#111"/><circle cx="115" cy="42" r="1.4" fill="#111"/>
      <rect x="9" y="16" width="102" height="32" fill="#141414"/><rect x="9" y="16" width="102" height="5" fill="#e8e3d6"/>
      ${row(7, i => knob(20 + i * 12, 31, 3.6, '#2a2a2a', '#e8e3d6'))}${row(3, i => `<rect x="${20 + i * 14}" y="40" width="3" height="5" rx=".5" fill="#bbb"/>`)}<circle cx="104" cy="41" r="1.6" fill="#ff3b2f"/>`,
    song:`<rect x="10" y="8" width="100" height="48" rx="4" fill="#2a241f"/>${row(4, i => `<rect x="18" y="${15 + i * 10}" width="${[60, 82, 48, 70][i]}" height="6" rx="2" fill="${['#e8742b', '#7fd', '#c9c9c9', '#c23b2f'][i]}"/>`)}`,
    mix:`<rect x="10" y="8" width="100" height="48" rx="4" fill="#2a241f"/>${row(8, i => `<rect x="${20 + i * 11.5}" y="14" width="1.4" height="36" fill="#000"/><rect x="${17 + i * 11.5}" y="${18 + (i * 7 % 24)}" width="7.4" height="5" rx="1" fill="#e9e2cf"/>`)}`
  };
})();
const gearSVG = (id, cls = '') => GEAR[id] ? `<svg class="gear ${cls}" viewBox="0 0 120 64" aria-hidden="true">${GEAR[id]}</svg>` : '';
