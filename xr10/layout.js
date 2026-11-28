// Omega XR10: the owner's layout (PLUGIN-LAYOUT.md). The machine fills the screen, a slim rail of page keys sits
// beside it (FOLD, MACHINE, a key per group of commands, ZOOM), and each group's commands are on a page that never
// scrolls: what doesn't fit goes on numbered pages (◀ 1 2 3 ▶, or swipe). Landscape: rail down the left, the machine or
// the page beside it. Portrait: the machine on top, the rail as two rows under it, the page under that.
'use strict';
(function () {
  const $ = id => document.getElementById(id);
  const face = $('face'), rail = $('rail'), pagesEl = $('pages');
  const PAGES = [...document.querySelectorAll('#pages .page')];
  const page = n => $('pg-' + n);
  const KEYS = [['FOLD', 'fold'], ['MACHINE', 'machine'], ['PADS', 'pads'], ['SOUNDS', 'sounds'], ['STEPS', 'steps'], ['SONG', 'song'],
    ['MIXER', 'mixer'], ['FX', 'fx'], ['SETUP', 'setup'], ['HELP', 'help'], ['ZOOM', 'zoom']];
  let view = 'machine', folded = false, land = true;
  try { view = localStorage.xr10view || 'machine'; folded = localStorage.xr10fold === '1'; } catch (e) { }
  if (!KEYS.some(k => k[1] === view) || view === 'fold' || view === 'zoom') view = 'machine';
  const at = {};          // the numbered page each page is on

  // the rail
  const keyEls = {};
  for (const [t, k] of KEYS) {
    const b = document.createElement('button'); b.type = 'button'; b.textContent = t; b.dataset.k = k; if (k === 'zoom') b.className = 'zoom';
    if (k === 'fold') b.onclick = () => setFold(true);
    else if (k !== 'zoom') b.onclick = () => show(k);
    rail.appendChild(b); keyEls[k] = b;
  }
  $('expand').onclick = () => setFold(false);

  // each page: a header (title, numbered pages), the source blocks (hidden), and the sheets they are poured into
  for (const pg of PAGES) {
    const src = document.createElement('div'); src.className = 'src';
    while (pg.firstChild) src.appendChild(pg.firstChild);
    const ph = document.createElement('div'); ph.className = 'ph';
    ph.innerHTML = `<span class="tt">${pg.dataset.title}</span><span class="nav"></span>`;
    const sh = document.createElement('div'); sh.className = 'sheets';
    pg.append(ph, src, sh);
    pg._src = src; pg._sheets = sh; pg._leaves = null; pg._n = 1;
    // swipe between numbered pages
    let sx = null, sy = 0;
    sh.addEventListener('pointerdown', e => { sx = e.target.closest('button,input,select,canvas,.fader,.mknob,.cell,.bigpad,.pick') ? null : e.clientX; sy = e.clientY; });
    sh.addEventListener('pointerup', e => {
      if (sx === null) return; const dx = e.clientX - sx; sx = null;
      if (Math.abs(dx) > 50 && Math.abs(dx) > Math.abs(e.clientY - sy)) go(pg, (at[pg.id] || 0) + (dx < 0 ? 1 : -1));
    });
  }
  const name = pg => pg.id.slice(3);
  const shownPage = () => folded ? null : page(land ? (view === 'machine' ? null : view) || 'x' : (view === 'machine' ? 'pads' : view));

  // ---- pouring blocks into sheets
  const isSplit = el => el.tagName === 'SECTION' || el.hasAttribute('data-split');
  function leavesOf(el, path, out) {
    if (isSplit(el) && el.children.length) for (const c of [...el.children]) leavesOf(c, path.concat([el]), out);
    else out.push({ el, path, parent: el.parentNode });
  }
  function unflow(pg) {
    if (!pg || !pg._leaves) return;
    for (const L of pg._leaves) { L.el.style.zoom = ''; L.parent.appendChild(L.el); }
    pg._leaves = null; pg._sheets.innerHTML = '';
  }
  function flow(pg) {
    unflow(pg);
    const sh = pg._sheets, leaves = [], clones = [];
    for (const c of [...pg._src.children]) leavesOf(c, [], leaves);
    pg._leaves = leaves;
    const g = $('grid'); if (g && name(pg) === 'steps') g.style.setProperty('--rowh', '8px');   // poured small, then grown to fill
    let sheet, open, count = 0;
    const start = () => { sheet = document.createElement('div'); sheet.className = 'sheet'; sh.appendChild(sheet); open = []; count = 0; };
    const target = path => {
      let k = 0; while (k < open.length && k < path.length && open[k][0] === path[k]) k++;
      open.length = k;
      let parent = k ? open[k - 1][1] : sheet;
      for (; k < path.length; k++) {
        const c = path[k].cloneNode(false); c.removeAttribute('id'); c.removeAttribute('data-split');
        parent.appendChild(c); open.push([path[k], c]); clones.push(c); parent = c;
      }
      return parent;
    };
    const over = () => sheet.scrollHeight > sheet.clientHeight + 1;
    start();
    if (pg.classList.contains('fit')) { for (const L of leaves) sheet.appendChild(L.el); }
    else for (let i = 0; i < leaves.length; i++) {
      const L = leaves[i];
      target(L.path).appendChild(L.el);
      if (count > 0 && over()) {
        const carry = [L], prev = leaves[i - 1];
        if (count > 1 && prev && /^H\d$/.test(prev.el.tagName)) carry.unshift(prev);   // a heading never ends a page
        start();
        for (const c of carry) { target(c.path).appendChild(c.el); count++; }
      } else count++;
      if (count === 1 && over()) { L.el.style.zoom = ''; const z = Math.max(0.4, (sheet.clientHeight - 14) / Math.max(1, sheet.scrollHeight)); if (z < 1) L.el.style.zoom = z.toFixed(3); }
      if (L.el.hasAttribute('data-break-after') && i < leaves.length - 1) start();
    }
    for (const c of clones.reverse()) if (!c.children.length) c.remove();
    [...sh.children].forEach(s => { if (!s.children.length) s.remove(); });
    pg._n = Math.max(1, sh.children.length);
    if (name(pg) === 'steps') growGrid();
    go(pg, at[pg.id] || 0);
  }
  // the step grid: its rows fill the room left on its page
  function growGrid() {
    const g = $('grid'); if (!g || !g.parentNode) return;
    const sheet = g.closest('.sheet'); if (!sheet) return;
    const steps = (g.style.gridTemplateColumns.match(/repeat\((\d+)/) || [0, 16])[1];
    const rows = Math.max(1, Math.round(g.children.length / (+steps + 1)));
    let bottom = 0; for (const c of sheet.querySelectorAll(':scope *')) { const b = c.getBoundingClientRect().bottom; if (b > bottom) bottom = b; }
    const free = sheet.getBoundingClientRect().bottom - 14 - bottom;
    g.style.setProperty('--rowh', Math.max(8, Math.min(34, 8 + free / rows)).toFixed(1) + 'px');
  }
  function go(pg, i) {
    i = Math.max(0, Math.min(pg._n - 1, i)); at[pg.id] = i;
    [...pg._sheets.children].forEach((s, k) => s.classList.toggle('hide', k !== i));
    const nav = pg.querySelector('.ph .nav'); nav.innerHTML = '';
    if (pg._n > 1) {
      const b = (t, to, on) => { const x = document.createElement('button'); x.type = 'button'; x.textContent = t; if (on) x.className = 'on'; x.onclick = () => go(pg, to); nav.appendChild(x); };
      b('◀', i - 1); for (let k = 0; k < pg._n; k++) b(String(k + 1), k, k === i); b('▶', i + 1);
    }
  }

  // ---- placing the machine, the rail and the page
  const box = (el, x, y, w, h) => Object.assign(el.style, { left: x + 'px', top: y + 'px', width: w + 'px', height: h + 'px' });
  const FW = 800, FH = 549;
  function fitFace(x, y, w, h) {
    const s = Math.min(w / FW, h / FH), fw = Math.floor(FW * s), fh = Math.floor(FH * s);
    box(face, Math.round(x + (w - fw) / 2), Math.round(y + (h - fh) / 2), fw, fh);
  }
  function layout() {
    if (window.OmegaLoupe && OmegaLoupe.open) return;
    const vw = innerWidth, vh = innerHeight;
    land = vw >= vh * 1.05;
    document.body.classList.toggle('folded', folded); document.body.classList.toggle('port', !land);
    for (const [, k] of KEYS) if (k !== 'zoom') keyEls[k].classList.toggle('on', k === view);
    let pg = null;
    if (folded) { face.style.display = ''; fitFace(0, 0, vw, vh); }
    else if (land) {
      const cols = vh / KEYS.length < 29 ? 2 : 1, rw = cols === 1 ? Math.max(64, Math.min(96, Math.round(vw * 0.085))) : Math.max(110, Math.round(vw * 0.15));
      box(rail, 0, 0, rw, vh);
      rail.style.gridTemplateColumns = `repeat(${cols}, 1fr)`; rail.style.gridTemplateRows = `repeat(${Math.ceil(KEYS.length / cols)}, 1fr)`;
      if (view === 'machine') { face.style.display = ''; pagesEl.style.display = 'none'; fitFace(rw, 0, vw - rw, vh); }
      else { face.style.display = 'none'; pagesEl.style.display = ''; box(pagesEl, rw, 0, vw - rw, vh); pg = page(view); }
    } else {
      const fh = Math.round(vw * FH / FW), rh = 2 * 34 + 12;
      face.style.display = ''; pagesEl.style.display = '';
      box(face, 0, 0, vw, fh);
      box(rail, 0, fh, vw, rh);
      rail.style.gridTemplateColumns = `repeat(${Math.ceil(KEYS.length / 2)}, 1fr)`; rail.style.gridTemplateRows = 'repeat(2, 1fr)';
      box(pagesEl, 0, fh + rh, vw, vh - fh - rh);
      pg = page(view === 'machine' ? 'pads' : view);
    }
    for (const p of PAGES) p.classList.toggle('show', p === pg);
    if (pg) flow(pg);
    if (typeof drawLcd === 'function') drawLcd();
  }
  function show(k) { view = k; try { localStorage.xr10view = k; } catch (e) { } layout(); }
  function setFold(on) { folded = on; try { localStorage.xr10fold = on ? '1' : '0'; } catch (e) { } if (window.OmegaLoupe) OmegaLoupe.close(); layout(); }

  // reflow a page soon after its contents change (if it is the one showing)
  const pending = new Set(); let raf = 0;
  function soon(n) {
    pending.add(n);
    if (!raf) raf = requestAnimationFrame(() => {
      raf = 0; const cur = shownPage();
      for (const p of pending) { const pg = page(p); if (pg === cur) flow(pg); else unflow(pg); }
      pending.clear();
    });
  }
  window.Lay = { layout, soon, unflow: n => unflow(page(n)), show, setFold, get view() { return view; }, get folded() { return folded; }, go: (n, i) => go(page(n), i), pages: n => page(n)._n };

  let rt = 0;
  const onResize = () => { clearTimeout(rt); rt = setTimeout(layout, 60); };
  addEventListener('resize', onResize);
  addEventListener('orientationchange', () => setTimeout(layout, 250));
  if (window.visualViewport) visualViewport.addEventListener('resize', onResize);
  if (window.OmegaLoupe) OmegaLoupe.attach({ button: keyEls.zoom, avoid: rail, root: face,
    pick: e => face.contains(e.target) ? face : pagesEl.contains(e.target) ? pagesEl : null });
  layout();
})();
