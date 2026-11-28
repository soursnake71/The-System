/* ZOOM: a magnifier for the whole panel (the same in every Omega app; the plugins have plugins/common/Loupe.h).
   OmegaLoupe.attach({root, button, zoom}): the button switches it on and off. While it is on, a tap on the panel
   magnifies that part of it 3x in a lens: the real panel is scaled around the tap and clipped to the lens, so
   whatever is under your finger in the lens is the real control (keys press, knobs and faders drag, menus open).
   Drag on the panel and the lens follows the finger; drag the bar on top of the lens to move it like a magnifying
   glass (it shows what is under its own centre). A tap outside the lens closes it (the next tap opens it somewhere
   else). attach({avoid}): an element the lens keeps clear of (a rail of keys). attach({pick: e => element}): magnify
   whichever part of the screen the tap is on (the machine, or the open page). Works with any layout and transform. */
(function(){
  const S = {root:null, button:null, zoom:3, active:false, open:false, saved:null, lens:null, frame:null};
  const css = (e, o) => Object.assign(e.style, o);
  const GRIP = 24;
  function lensSize(){ const vw = innerWidth, vh = innerHeight; return {w:Math.min(vw - 16, Math.max(260, vw * .7), 560), h:Math.min(vh - 16, Math.max(180, vh * .55), 360)}; }
  function clampLens(x, y, w, h){
    const vw = innerWidth, vh = innerHeight;
    x = Math.max(8, Math.min(vw - w - 8, x)); y = Math.max(8, Math.min(vh - h - 8, y));
    const a = S.avoid && S.avoid.getBoundingClientRect();
    if (a && a.width && x < a.right && x + w > a.left && y < a.bottom && y + h > a.top){
      if (vw >= vh) x = Math.min(vw - w - 8, a.right + 6); else y = y + h / 2 < a.top + a.height / 2 && a.top - h - 6 >= 8 ? a.top - h - 6 : Math.min(vh - h - 8, a.bottom + 6);
    }
    return {x, y, w, h};
  }
  function lensRect(cx, cy){
    const {w, h} = lensSize(); let y = cy - h - 30; if (y < 8) y = cy + 30;
    return clampLens(cx - w / 2, y, w, h);
  }
  // magnify the point (cx, cy) of the screen in the lens L (default: a lens placed near the point)
  function openAt(cx, cy, L0){
    const r = S.cur || S.root, cs = getComputedStyle(r);
    if (!S.saved) S.saved = {transform:r.style.transform, origin:r.style.transformOrigin, clip:r.style.clipPath, z:r.style.zIndex, pos:r.style.position, transition:r.style.transition};
    else { r.style.transform = S.saved.transform; r.style.transformOrigin = S.saved.origin; r.style.clipPath = S.saved.clip; }
    r.style.transition = 'none';                      // an animated transform would put the lens in the wrong place
    // the root's current mapping: screen = box + o + M (p - o); rebuild it with origin 0 and the lens on top
    const m0 = new DOMMatrix(cs.transform === 'none' ? undefined : cs.transform), [ox, oy] = cs.transformOrigin.split(' ').map(parseFloat);
    const box0 = r.getBoundingClientRect();
    const local = m0.multiply(new DOMMatrix().translate(-ox, -oy));
    const m1 = new DOMMatrix().translate(ox, oy).multiply(local);           // the same mapping, origin 0 (relative to the layout box)
    // the layout box on the screen: the transformed box's corner minus where m1 puts the box's own corners
    const pts = [[0, 0], [r.offsetWidth, 0], [0, r.offsetHeight], [r.offsetWidth, r.offsetHeight]].map(([x, y]) => m1.transformPoint(new DOMPoint(x, y)));
    const minX = Math.min(...pts.map(p => p.x)), minY = Math.min(...pts.map(p => p.y));
    const base = {x:box0.left - minX, y:box0.top - minY};                    // screen position of the layout box
    const L = L0 || lensRect(cx, cy), z = S.zoom;
    // Z: magnify around the tap, put the tap at the lens centre (screen coordinates, made relative to the layout box)
    const Z = new DOMMatrix().translate(-base.x, -base.y).translate(L.x + L.w / 2, L.y + L.h / 2).scale(z).translate(-cx, -cy).translate(base.x, base.y);
    const m2 = Z.multiply(m1);
    r.style.transformOrigin = '0 0'; r.style.transform = m2.toString();
    // clip to the lens: its corners in the root's own coordinates
    const inv = m2.inverse(), a = inv.transformPoint(new DOMPoint(L.x - base.x, L.y - base.y)), b = inv.transformPoint(new DOMPoint(L.x + L.w - base.x, L.y + L.h - base.y));
    const W = r.offsetWidth, H = r.offsetHeight, rad = 12 / (m2.a || 1);
    r.style.clipPath = `inset(${Math.max(0, a.y)}px ${Math.max(0, W - b.x)}px ${Math.max(0, H - b.y)}px ${Math.max(0, a.x)}px round ${rad}px)`;
    if (cs.position === 'static') r.style.position = 'relative';
    r.style.zIndex = '40';
    if (!S.frame){
      S.frame = document.createElement('div'); document.body.appendChild(S.frame);
      const g = S.grip = document.createElement('div'); S.frame.appendChild(g); g.textContent = '⠿ drag to move · tap to use ⠿';
      css(g, {position:'absolute', left:0, right:0, top:0, height:GRIP + 'px', pointerEvents:'auto', touchAction:'none', cursor:'grab', borderRadius:'11px 11px 0 0',
        background:'linear-gradient(180deg, #e9e9e6, #b9b5aa)', color:'#333', font:'700 11px/' + GRIP + 'px sans-serif', textAlign:'center', letterSpacing:'.5px'});
      g.addEventListener('pointerdown', e => { e.preventDefault(); e.stopPropagation(); try { g.setPointerCapture(e.pointerId); } catch (err) {} S.drag = {mode:'grip', id:e.pointerId, dx:e.clientX - S.lens.x, dy:e.clientY - S.lens.y}; });
    }
    css(S.frame, {position:'fixed', zIndex:'41', pointerEvents:'none', left:L.x - 3 + 'px', top:L.y - 3 + 'px', width:L.w + 'px', height:L.h + 'px', borderRadius:'14px',
      border:'3px solid #e9e9e6', boxShadow:'0 0 0 2px #000, 0 14px 40px #000d', display:'block'});
    S.lens = L; S.open = true;
  }
  function close(){
    if (!S.open) return;
    const r = S.cur || S.root; r.style.transform = S.saved.transform; r.style.transformOrigin = S.saved.origin; r.style.clipPath = S.saved.clip; r.style.zIndex = S.saved.z; r.style.position = S.saved.pos;
    void r.offsetWidth; r.style.transition = S.saved.transition;
    S.saved = null; S.open = false; S.lens = null; if (S.frame) S.frame.style.display = 'none';
    dispatchEvent(new Event('resize'));          // let the app lay itself out again
  }
  const inLens = e => S.lens && e.clientX >= S.lens.x && e.clientX <= S.lens.x + S.lens.w && e.clientY >= S.lens.y && e.clientY <= S.lens.y + S.lens.h;
  function setActive(on){
    S.active = on; if (!on) close();
    if (S.button){ S.button.classList.toggle('on', on); S.button.setAttribute('aria-pressed', String(on)); }
    if (S.onChange) S.onChange(on);
  }
  function attach(o){
    Object.assign(S, {zoom:3}, o);
    if (S.button) S.button.addEventListener('click', e => { e.preventDefault(); setActive(!S.active); });
    // capture phase: decide before the app's own handlers see the tap
    let swallow = false;
    const down = e => {
      // decided before the lens opens (it may open under the finger): only a tap inside an open lens goes through
      swallow = S.active && !(S.open && inLens(e)) && !(S.button && S.button.contains(e.target));
      if (!S.active || (S.button && S.button.contains(e.target))) return;
      if (S.open && inLens(e)) return;                                   // the real control under the lens gets it
      if (S.open){ close(); e.preventDefault(); e.stopPropagation(); return; }
      const R = S.pick ? S.pick(e) : S.root;                            // attach({pick}): which part of the screen to magnify
      if (!R || !R.contains(e.target)) return;
      S.cur = R;
      e.preventDefault(); e.stopPropagation(); openAt(e.clientX, e.clientY);
      S.drag = {mode:'follow', id:e.pointerId};                         // keep the finger down and the lens follows it
    };
    addEventListener('pointerdown', down, true);
    addEventListener('pointermove', e => {
      if (!S.drag || !S.open || e.pointerId !== S.drag.id) return;
      if (S.drag.mode === 'follow') openAt(e.clientX, e.clientY);
      else { const {w, h} = lensSize(), L = clampLens(e.clientX - S.drag.dx, e.clientY - S.drag.dy, w, h); openAt(L.x + L.w / 2, L.y + GRIP + (L.h - GRIP) / 2, L); }
    }, true);
    const end = e => { if (S.drag && e.pointerId === S.drag.id) S.drag = null; };
    addEventListener('pointerup', end, true); addEventListener('pointercancel', end, true);
    // the click and mouse events that follow a magnifying tap must not press anything either
    for (const t of ['mousedown', 'mouseup', 'click', 'touchstart']) addEventListener(t, e => { if (swallow && !(S.button && S.button.contains(e.target))){ e.preventDefault(); e.stopPropagation(); } }, {capture:true, passive:false});
    addEventListener('resize', () => { if (S.open) close(); });
  }
  window.OmegaLoupe = {get drag(){ return S.drag; }, attach, setActive, close, openAt, get active(){ return S.active; }, get open(){ return S.open; }, get lens(){ return S.lens; }};
})();
