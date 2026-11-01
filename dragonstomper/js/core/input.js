// Keyboard + mouse (pointer lock) and gamepad, mapped to game actions.
import { settings } from './gfx.js';

const KEYMAP = {
  KeyW: 'fwd', ArrowUp: 'fwd', KeyS: 'back', ArrowDown: 'back', KeyA: 'left', ArrowLeft: 'left', KeyD: 'right', ArrowRight: 'right',
  ShiftLeft: 'sprint', ShiftRight: 'sprint', Space: 'jump', KeyE: 'interact', KeyF: 'interact', KeyV: 'view', KeyQ: 'use',
  KeyI: 'inventory', Tab: 'inventory', Escape: 'pause', KeyC: 'dodge', ControlLeft: 'dodge', KeyR: 'potion', KeyH: 'sheathe',
  Digit1: 'spell1', Digit2: 'spell2', Digit3: 'spell3', Digit4: 'spell4', Digit5: 'spell5', Digit6: 'spell6', Enter: 'confirm',
};

export const input = {
  held: new Set(), pressed: new Set(), mouseDX: 0, mouseDY: 0, locked: false, wheel: 0,
  moveX: 0, moveY: 0, lookX: 0, lookY: 0, gamepad: false, padType: null, usingPad: false, menu: new Set(),
};

export function initInput(canvas) {
  addEventListener('keydown', e => {
    input.usingPad = false;
    const a = KEYMAP[e.code];
    if (!a) return;
    if (e.code === 'Tab' || e.code === 'Space') e.preventDefault();
    if (!e.repeat) input.pressed.add(a);
    input.held.add(a);
  });
  addEventListener('keyup', e => { const a = KEYMAP[e.code]; if (a) input.held.delete(a); });
  addEventListener('blur', () => input.held.clear());
  canvas.addEventListener('mousedown', e => {
    if (!input.locked) return;
    if (e.button === 0) { input.pressed.add('attack'); input.held.add('attack'); }
    if (e.button === 2) { input.held.add('block'); input.pressed.add('block'); }
  });
  addEventListener('mouseup', e => {
    if (e.button === 0) input.held.delete('attack');
    if (e.button === 2) input.held.delete('block');
  });
  canvas.addEventListener('contextmenu', e => e.preventDefault());
  addEventListener('mousemove', e => {
    if (!input.locked) return;
    input.mouseDX += e.movementX; input.mouseDY += e.movementY;
  });
  addEventListener('wheel', e => { if (input.locked) input.wheel += Math.sign(e.deltaY); }, { passive: true });
  document.addEventListener('pointerlockchange', () => {
    input.locked = document.pointerLockElement === canvas;
    if (!input.locked) input.held.clear();
    input.onLockChange?.(input.locked);
  });
}

export function lockPointer(canvas) {
  if (document.pointerLockElement !== canvas) try {
    const r = canvas.requestPointerLock?.({ unadjustedMovement: true });
    r?.catch?.(() => { try { canvas.requestPointerLock()?.catch?.(() => {}); } catch (e) {} });
  } catch (e) {}
}
export const unlockPointer = () => { if (document.pointerLockElement) document.exitPointerLock(); };

const padPrev = {};
let padSprint = false, sprintFromPad = false;
// call once per frame before reading
export function pollInput() {
  let mx = 0, my = 0;
  if (input.held.has('left')) mx -= 1;
  if (input.held.has('right')) mx += 1;
  if (input.held.has('fwd')) my += 1;
  if (input.held.has('back')) my -= 1;
  const s = 0.0022 * settings.sensitivity;
  let lx = input.mouseDX * s, ly = input.mouseDY * s * (settings.invertY ? -1 : 1);
  input.mouseDX = input.mouseDY = 0;
  input.gamepad = false;
  for (const gp of navigator.getGamepads ? navigator.getGamepads() : []) {
    if (!gp) continue;
    const dz = (v) => Math.abs(v) < 0.15 ? 0 : v;
    const ax = dz(gp.axes[0] || 0), ay = dz(gp.axes[1] || 0), rx = dz(gp.axes[2] || 0), ry = dz(gp.axes[3] || 0);
    // Xbox (XInput) and PlayStation (DualShock 4 / DualSense) both arrive in the browser's
    // "standard" layout: A/Cross 0, B/Circle 1, X/Square 2, Y/Triangle 3, LB/L1 4, RB/R1 5,
    // LT/L2 6, RT/R2 7, View/Share 8, Menu/Options 9, L3 10, R3 11, d-pad 12-15
    input.padType = /054c|playstation|dualsense|dualshock|wireless controller/i.test(gp.id) ? 'ps' : 'xbox';
    if (ax || ay) { mx += ax; my -= ay; input.gamepad = true; }
    lx += rx * 0.045 * settings.sensitivity; ly += ry * 0.035 * settings.sensitivity * (settings.invertY ? -1 : 1);
    const b = (i) => !!(gp.buttons[i] && gp.buttons[i].pressed);
    // Controller layout (Xbox names; PlayStation in brackets):
    //   A [✕] jump · B [○] dodge · X [□] / RT [R2] attack · Y [△] search/talk/open · LT [L2] block
    //   LB [L1] longbow · L3 sprint (toggle) · R3 first/third person · View [Share] bag · Menu [Options] pause
    //   D-pad: ↑ camera distance · ↓ sheathe/draw · ← first/third person · → bag
    //   Hold RB [R1] for spells: ↑ BLAST · → FLASH · ↓ STUN · ← UNLOCK · Y [△] VISION · B [○] PROTECT
    const rb = b(5);
    const map = rb
      ? { attack: b(7) || b(2), block: b(6), jump: b(0), potion: b(4), pause: b(9),
          spell1: b(12), spell2: b(15), spell3: b(13), spell4: b(14), spell5: b(3), spell6: b(1) }
      : { attack: b(7) || b(2), block: b(6), jump: b(0), interact: b(3), dodge: b(1), view: b(11) || b(14), inventory: b(8) || b(15), pause: b(9), potion: b(4),
          zoom: b(12), sheathe: b(13) };
    input.spellMode = rb;
    // L3 latches sprint until the stick comes back to centre
    const l3id = gp.index + 'l3';
    if (b(10) && !padPrev[l3id]) padSprint = !padSprint;
    padPrev[l3id] = b(10);
    if (Math.hypot(ax, ay) < 0.2) padSprint = false;
    if (padSprint) { input.held.add('sprint'); sprintFromPad = true; } else if (sprintFromPad) { input.held.delete('sprint'); sprintFromPad = false; }
    if (gp.buttons.some(x => x.pressed) || ax || ay || rx || ry) input.usingPad = true;
    // menu navigation edges
    const nav = { up: b(12) || ay < -0.6, down: b(13) || ay > 0.6, ok: b(0), back: b(1), start: b(9) };
    for (const [k, v] of Object.entries(nav)) { const id = gp.index + 'm' + k; if (v && !padPrev[id]) input.menu.add(k); padPrev[id] = v; }
    for (const k of ['interact', 'dodge', 'view', 'inventory', 'zoom', 'sheathe', 'spell1', 'spell2', 'spell3', 'spell4', 'spell5', 'spell6'])
      if (!(k in map) && padPrev[gp.index + k]) { input.held.delete(k); padPrev[gp.index + k] = false; }
    for (const [k, v] of Object.entries(map)) {
      const id = gp.index + k;
      if (v && !padPrev[id]) input.pressed.add(k);
      if (v) input.held.add(k); else if (padPrev[id]) input.held.delete(k);
      padPrev[id] = v;
    }
  }
  const l = Math.hypot(mx, my);
  if (l > 1) { mx /= l; my /= l; }
  input.moveX = mx; input.moveY = my; input.lookX = lx; input.lookY = ly;
}
export const pressed = (a) => input.pressed.has(a);
export const held = (a) => input.held.has(a);
export function endFrame() { input.pressed.clear(); input.menu.clear(); input.wheel = 0; }
export const glyph = (action) => {
  if (!input.usingPad) return { interact: 'E', inventory: 'I', potion: 'R', sheathe: 'H', spell1: '1', spell2: '2', spell3: '3', spell4: '4', spell5: '5', spell6: '6' }[action] || action;
  const ps = input.padType === 'ps', R = ps ? 'R1' : 'RB';
  return { interact: ps ? '△' : 'Y', inventory: ps ? 'Share' : 'View', potion: ps ? 'L1' : 'LB', sheathe: '↓',
    spell1: R + '↑', spell2: R + '→', spell3: R + '↓', spell4: R + '←', spell5: R + (ps ? '△' : 'Y'), spell6: R + (ps ? '○' : 'B') }[action] || action;
};
