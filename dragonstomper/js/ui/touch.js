// On-screen touch controls for phones and tablets: a floating stick on the left to move
// (push it to the rim to sprint), drag on the right to look, and thumb buttons for the rest.
// They show on touchscreens while you play, and hide when a controller or keyboard is used.
import { input } from '../core/input.js';

const BUTTONS = [
  // id, label, action, hold (keeps the action held while pressed)
  ['attack', 'ATTACK', 'attack', true],
  ['block', 'BLOCK', 'block', true],
  ['jump', 'JUMP', 'jump', true],
  ['dodge', 'DODGE', 'dodge', false],
  ['use', 'USE', 'interact', false],
  ['bow', 'BOW', 'potion', false],
  ['bag', 'BAG', 'inventory', false],
  ['view', '1ST/3RD', 'view', false],
  ['pause', '❚❚', 'pause', false],
];
const STICK_R = 60; // px from centre to rim

// read by pollInput() in core/input.js
export const touch = input.touch = { mx: 0, my: 0, lookDX: 0, lookDY: 0, sprint: false, active: false };

export function initTouch(canHasBow) {
  if (!('ontouchstart' in window) && !(navigator.maxTouchPoints > 0)) return;
  const root = document.createElement('div');
  root.id = 'touch';
  root.innerHTML = `<div class="stick"><i></i></div>` +
    BUTTONS.map(([id, label]) => `<button class="tb tb-${id}" data-id="${id}">${label}</button>`).join('');
  document.body.appendChild(root);
  const stick = root.querySelector('.stick'), knob = stick.querySelector('i');
  let stickId = null, sx = 0, sy = 0, lookId = null, lx = 0, ly = 0;

  const moveStick = (x, y) => {
    let dx = x - sx, dy = y - sy;
    const d = Math.hypot(dx, dy);
    if (d > STICK_R) { dx *= STICK_R / d; dy *= STICK_R / d; }
    knob.style.transform = `translate(${dx}px, ${dy}px)`;
    touch.mx = dx / STICK_R; touch.my = -dy / STICK_R;
    touch.sprint = d > STICK_R * 1.15;
  };
  const endStick = () => {
    stickId = null; touch.mx = touch.my = 0; touch.sprint = false;
    knob.style.transform = ''; stick.classList.remove('on');
  };

  // the open screen: left half moves (the stick appears where your thumb lands), right half looks
  root.addEventListener('pointerdown', (e) => {
    if (e.target.closest('.tb')) return;
    e.preventDefault();
    input.usingPad = false; touch.active = true;
    if (e.clientX < innerWidth * 0.45 && stickId == null) {
      stickId = e.pointerId; sx = e.clientX; sy = e.clientY;
      stick.style.left = sx + 'px'; stick.style.top = sy + 'px'; stick.classList.add('on');
      moveStick(sx, sy);
    } else if (lookId == null) { lookId = e.pointerId; lx = e.clientX; ly = e.clientY; }
    try { root.setPointerCapture(e.pointerId); } catch (err) {}
  });
  root.addEventListener('pointermove', (e) => {
    if (e.pointerId === stickId) moveStick(e.clientX, e.clientY);
    else if (e.pointerId === lookId) { touch.lookDX += e.clientX - lx; touch.lookDY += e.clientY - ly; lx = e.clientX; ly = e.clientY; }
  });
  const up = (e) => { if (e.pointerId === stickId) endStick(); if (e.pointerId === lookId) lookId = null; };
  root.addEventListener('pointerup', up); root.addEventListener('pointercancel', up);

  // buttons: press on touch-down (no 300 ms tap delay); held ones stay held until release
  for (const b of root.querySelectorAll('.tb')) {
    const [, , action, hold] = BUTTONS.find(x => x[0] === b.dataset.id);
    let pid = null;
    b.addEventListener('pointerdown', (e) => {
      e.preventDefault(); e.stopPropagation();
      input.usingPad = false; touch.active = true;
      pid = e.pointerId; try { b.setPointerCapture(pid); } catch (err) {} b.classList.add('down');
      input.pressed.add(action);
      if (hold) input.held.add(action);
      navigator.vibrate?.(8);
    });
    const release = (e) => { if (e.pointerId !== pid) return; pid = null; b.classList.remove('down'); if (hold) input.held.delete(action); };
    b.addEventListener('pointerup', release); b.addEventListener('pointercancel', release);
    b.addEventListener('contextmenu', (e) => e.preventDefault());
  }

  // visible only while playing, on touch, with no controller in use
  const playing = () => document.getElementById('hud')?.classList.contains('on') && !document.querySelector('.screen.on') && !document.getElementById('dialog')?.classList.contains('on');
  const tick = () => {
    const show = playing() && !input.usingPad;
    root.classList.toggle('on', show);
    document.body.classList.toggle('touchmode', show);
    root.querySelector('.tb-bow').hidden = !canHasBow();
    if (!show && stickId != null) endStick();
    requestAnimationFrame(tick);
  };
  tick();
}
