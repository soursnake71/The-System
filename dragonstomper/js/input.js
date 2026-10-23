// Keyboard, touch joystick and gamepad, folded into one analog stick plus button presses.
export const input = {
  x: 0, y: 0,            // movement, -1..1 (y: -1 = north / up the screen)
  queue: [],             // edge presses: up down left right A B
  held: new Set(),
  touchStick: { id: null, ox: 0, oy: 0, x: 0, y: 0 },
  aHeld: false,
};

const KEYS = {
  ArrowUp: 'up', KeyW: 'up', ArrowDown: 'down', KeyS: 'down', ArrowLeft: 'left', KeyA: 'left',
  ArrowRight: 'right', KeyD: 'right', Space: 'A', Enter: 'A', KeyZ: 'A', KeyJ: 'A',
  Escape: 'B', KeyX: 'B', Backspace: 'B', KeyK: 'B',
  Digit1: 'up', Digit2: 'right', Digit3: 'left', Digit4: 'down',
};

export function press(b) { input.queue.push(b); }
export function take() { return input.queue.shift(); }
export function clearQueue() { input.queue.length = 0; }

addEventListener('keydown', e => {
  const k = KEYS[e.code];
  if (!k) return;
  e.preventDefault();
  if (!e.repeat) press(k);
  input.held.add(k);
  if (k === 'A') input.aHeld = true;
});
addEventListener('keyup', e => {
  const k = KEYS[e.code];
  if (!k) return;
  input.held.delete(k);
  if (k === 'A') input.aHeld = false;
});
addEventListener('blur', () => { input.held.clear(); input.aHeld = false; });

export function bindTouch(zone, knob, base) {
  const st = input.touchStick;
  zone.addEventListener('pointerdown', e => {
    if (st.id !== null) return;
    st.id = e.pointerId; st.ox = e.clientX; st.oy = e.clientY; st.x = st.y = 0;
    zone.setPointerCapture(e.pointerId);
    base.style.display = 'block';
    base.style.left = e.clientX + 'px'; base.style.top = e.clientY + 'px';
    knob.style.transform = 'translate(-50%,-50%)';
  });
  zone.addEventListener('pointermove', e => {
    if (e.pointerId !== st.id) return;
    const R = 50;
    let dx = e.clientX - st.ox, dy = e.clientY - st.oy;
    const l = Math.hypot(dx, dy);
    if (l > R) { dx *= R / l; dy *= R / l; }
    // a flick past half way also counts as a direction press, for menus
    const pdx = st.x, pdy = st.y;
    st.x = dx / R; st.y = dy / R;
    const dir = (x, y) => Math.hypot(x, y) < 0.6 ? null : Math.abs(x) > Math.abs(y) ? (x > 0 ? 'right' : 'left') : (y > 0 ? 'down' : 'up');
    const was = dir(pdx, pdy), now = dir(st.x, st.y);
    if (now && now !== was) press(now);
    knob.style.transform = `translate(calc(-50% + ${dx}px), calc(-50% + ${dy}px))`;
  });
  const end = e => {
    if (e.pointerId !== st.id) return;
    st.id = null; st.x = st.y = 0; base.style.display = 'none';
  };
  zone.addEventListener('pointerup', end);
  zone.addEventListener('pointercancel', end);
}

export function bindButton(el, b) {
  el.addEventListener('pointerdown', e => {
    e.preventDefault(); e.stopPropagation();
    press(b); el.classList.add('down');
    if (b === 'A') input.aHeld = true;
    navigator.vibrate?.(8);
  });
  const up = () => { el.classList.remove('down'); if (b === 'A') input.aHeld = false; };
  el.addEventListener('pointerup', up);
  el.addEventListener('pointerleave', up);
  el.addEventListener('pointercancel', up);
}

const padPrev = {};
export function pollInput() {
  let x = 0, y = 0;
  if (input.held.has('left')) x -= 1;
  if (input.held.has('right')) x += 1;
  if (input.held.has('up')) y -= 1;
  if (input.held.has('down')) y += 1;
  const st = input.touchStick;
  if (st.id !== null) { x += st.x; y += st.y; }
  for (const gp of navigator.getGamepads ? navigator.getGamepads() : []) {
    if (!gp) continue;
    const ax = gp.axes[0] || 0, ay = gp.axes[1] || 0;
    if (Math.hypot(ax, ay) > 0.2) { x += ax; y += ay; }
    const btn = (i) => gp.buttons[i] && gp.buttons[i].pressed;
    const map = { A: btn(0), B: btn(1), up: btn(12) || ay < -0.7, down: btn(13) || ay > 0.7, left: btn(14) || ax < -0.7, right: btn(15) || ax > 0.7 };
    for (const [k, v] of Object.entries(map)) {
      const id = gp.index + k;
      if (v && !padPrev[id]) press(k);
      padPrev[id] = v;
      if (k === 'A') input.aHeld = input.aHeld || v;
      if (['up', 'down', 'left', 'right'].includes(k) && v && !(Math.hypot(ax, ay) > 0.2)) {
        if (k === 'left') x -= 1; if (k === 'right') x += 1; if (k === 'up') y -= 1; if (k === 'down') y += 1;
      }
    }
  }
  const l = Math.hypot(x, y);
  if (l > 1) { x /= l; y /= l; }
  input.x = x; input.y = y;
}
