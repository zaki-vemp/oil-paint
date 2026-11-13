// Keyboard, mouse and gamepad, read once a frame into one plain state.
//   move: {x, y} (x right, y forward, length 0..1), walk, sprint, strafe (held),
//   jump, attack, heavy, power, dash, flare (pressed this frame),
//   look: {x, y} (radians this frame), zoom (wheel steps)
export class Input {
  constructor(el, { pads = true } = {}) {
    this.el = el;
    this.pads = pads; // off in headless checks: this PC's real gamepad shows up in headless browsers too
    this.keys = new Set();
    this.pressed = new Set();
    this.mouse = { x: 0, y: 0, wheel: 0, right: false, drag: false, attack: false };
    this.walkToggle = false;
    this.pad = { a: false, l3: false, sprint: false, x: false, y: false, b: false, lb: false, start: false };
    this.sensitivity = 0.0022;
    this.override = null; // scripted input for captures
    this.lockable = true;
    this.touch = { x: 0, y: 0 };
    const typing = () => /INPUT|TEXTAREA|SELECT/.test(document.activeElement?.tagName) || document.activeElement?.isContentEditable;
    const down = (e) => {
      if (typing() || window.__paused) return;
      if (e.code === 'Space' && document.activeElement?.tagName === 'BUTTON') return;
      if (['Space', 'ArrowUp', 'ArrowDown', 'AltLeft', 'AltRight'].includes(e.code)) e.preventDefault();
      if (!this.keys.has(e.code)) this.pressed.add(e.code);
      this.keys.add(e.code);
      if (e.code === 'CapsLock') this.walkToggle = !this.walkToggle;
    };
    const up = (e) => this.keys.delete(e.code);
    addEventListener('keydown', down);
    addEventListener('keyup', up);
    addEventListener('blur', () => this.reset());
    document.addEventListener('visibilitychange', () => { if (document.hidden) this.reset(); });
    el.addEventListener('mousedown', (e) => {
      if (e.button === 2) this.mouse.right = true;
      // a click locks the mouse for looking, except while a panel or the replay wants the mouse
      if (e.button === 0) {
        if (document.pointerLockElement === el) this.mouse.attack = true;
        else if (this.lockable) el.requestPointerLock?.()?.catch?.(() => {});
      }
    });
    addEventListener('mouseup', (e) => { if (e.button === 2) this.mouse.right = false; });
    el.addEventListener('contextmenu', (e) => e.preventDefault());
    addEventListener('mousemove', (e) => {
      if (document.pointerLockElement === el || this.mouse.right) { this.mouse.x += e.movementX; this.mouse.y += e.movementY; }
    });
    el.addEventListener('wheel', (e) => { this.mouse.wheel += Math.sign(e.deltaY); e.preventDefault(); }, { passive: false });
    // A thumbstick and a camera drag on touch devices feed the same state as keys and mouse.
    const stick = document.getElementById('stick');
    let stickId = null, lookId = null, prevX = 0, prevY = 0;
    const moveStick = e => {
      if (e.pointerId !== stickId) return;
      const r = stick.getBoundingClientRect(), dx = e.clientX - r.left - r.width / 2, dy = e.clientY - r.top - r.height / 2;
      const length = Math.max(32, Math.hypot(dx, dy));
      this.touch.x = dx / length; this.touch.y = -dy / length;
      stick.firstElementChild.style.transform = `translate(${this.touch.x * 30}px, ${-this.touch.y * 30}px)`;
    };
    stick?.addEventListener('pointerdown', e => { stickId = e.pointerId; stick.setPointerCapture(e.pointerId); moveStick(e); });
    stick?.addEventListener('pointermove', moveStick);
    const releaseStick = () => { stickId = null; this.touch.x = this.touch.y = 0; stick.firstElementChild.style.transform = ''; };
    stick?.addEventListener('pointerup', releaseStick); stick?.addEventListener('pointercancel', releaseStick);
    el.addEventListener('pointerdown', e => {
      if (e.pointerType !== 'touch') return;
      lookId = e.pointerId; prevX = e.clientX; prevY = e.clientY; el.setPointerCapture(e.pointerId);
    });
    el.addEventListener('pointermove', e => {
      if (e.pointerId !== lookId) return;
      this.mouse.x += (e.clientX - prevX) * 1.7; this.mouse.y += (e.clientY - prevY) * 1.7;
      prevX = e.clientX; prevY = e.clientY;
    });
    el.addEventListener('pointerup', () => { lookId = null; });
    el.addEventListener('pointercancel', () => { lookId = null; });
    const jump = document.getElementById('touch-jump');
    jump?.addEventListener('pointerdown', e => { jump.setPointerCapture(e.pointerId); this.pressed.add('Space'); this.keys.add('Space'); });
    jump?.addEventListener('pointerup', () => this.keys.delete('Space'));
    jump?.addEventListener('pointercancel', () => this.keys.delete('Space'));
  }

  reset() {
    this.keys.clear(); this.pressed.clear();
    this.mouse.x = this.mouse.y = this.mouse.wheel = 0;
    this.mouse.right = this.mouse.attack = false;
    this.touch.x = this.touch.y = 0;
  }

  read(dt) {
    if (this.override) return this.override(dt);
    const k = this.keys;
    let x = (k.has('KeyD') || k.has('ArrowRight') ? 1 : 0) - (k.has('KeyA') || k.has('ArrowLeft') ? 1 : 0);
    let y = (k.has('KeyW') || k.has('ArrowUp') ? 1 : 0) - (k.has('KeyS') || k.has('ArrowDown') ? 1 : 0);
    let len = Math.hypot(x, y);
    if (len > 1) { x /= len; y /= len; }
    if (Math.hypot(this.touch.x, this.touch.y) > 0.05) { x = this.touch.x; y = this.touch.y; }
    const s = {
      move: { x, y },
      walk: this.walkToggle !== (k.has('AltLeft') || k.has('AltRight')),
      sprint: k.has('ShiftLeft') || k.has('ShiftRight'),
      strafe: this.mouse.right && document.pointerLockElement === this.el,
      jump: this.pressed.has('Space'),
      jumpHeld: k.has('Space'),
      attack: this.mouse.attack || this.pressed.has('KeyJ'),
      heavy: this.pressed.has('KeyF'),
      power: this.pressed.has('KeyQ'),
      dash: this.pressed.has('KeyE'),
      flare: this.pressed.has('KeyR'),
      look: { x: this.mouse.x * this.sensitivity, y: this.mouse.y * this.sensitivity },
      zoom: this.mouse.wheel,
      device: 'keyboard',
    };
    this.mouse.x = this.mouse.y = this.mouse.wheel = 0;
    this.mouse.attack = false;
    this.pressed.clear();

    // gamepad: left stick moves (its tilt sets the speed), right stick looks, L3 sprints,
    // A jumps, LT strafes, d-pad up/down zooms
    const pad = this.pads ? [...(navigator.getGamepads?.() ?? [])].find((p) => p && p.connected) : null;
    if (pad) {
      const dz = (v) => (Math.abs(v) < 0.15 ? 0 : Math.sign(v) * (Math.abs(v) - 0.15) / 0.85);
      let px = dz(pad.axes[0] ?? 0), py = -dz(pad.axes[1] ?? 0);
      const pl = Math.hypot(px, py);
      if (pl > 1) { px /= pl; py /= pl; }
      const btn = (i) => !!pad.buttons[i]?.pressed;
      if (pl > 0.01) { s.move = { x: px, y: py }; s.device = 'pad'; s.walk = false; }
      const rx = dz(pad.axes[2] ?? 0), ry = dz(pad.axes[3] ?? 0);
      s.look.x += rx * Math.abs(rx) * 3.2 * dt;
      s.look.y += ry * Math.abs(ry) * 2.2 * dt;
      if (btn(10) && !this.pad.l3) this.pad.sprint = !this.pad.sprint;
      if (pl < 0.2) this.pad.sprint = false;
      if (this.pad.sprint || btn(5)) s.sprint = true;
      if (btn(0) && !this.pad.a) s.jump = true;
      s.jumpHeld ||= btn(0);
      if (btn(2) && !this.pad.x) s.attack = true;
      if (btn(3) && !this.pad.y) s.power = true;
      if (btn(4) && !this.pad.lb) s.heavy = true;
      if (btn(1) && !this.pad.b) s.dash = true;
      if (btn(9) && !this.pad.start) s.flare = true;
      if ((pad.buttons[6]?.value ?? 0) > 0.4) s.strafe = true;
      if (btn(12)) s.zoom -= 4 * dt;
      if (btn(13)) s.zoom += 4 * dt;
      this.pad.a = btn(0); this.pad.l3 = btn(10); this.pad.x = btn(2); this.pad.y = btn(3);
      this.pad.b = btn(1); this.pad.lb = btn(4); this.pad.start = btn(9);
      if (rx || ry || pl > 0.01 || btn(0)) s.device = 'pad';
    }
    return s;
  }
}
