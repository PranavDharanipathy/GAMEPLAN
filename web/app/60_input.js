// ================= input: physical gamepad (FTC semantics), rebindable keyboard, touch =================
const keys = new Set();
const KEY_LABEL = { Space: "Space", Escape: "Esc", Enter: "Enter", Tab: "Tab", Backspace: "Backspace", ShiftLeft: "Left Shift", ShiftRight: "Right Shift",
  ControlLeft: "Left Ctrl", ControlRight: "Right Ctrl", AltLeft: "Left Alt", AltRight: "Right Alt", ArrowUp: "↑", ArrowDown: "↓",
  ArrowLeft: "←", ArrowRight: "→", Comma: ",", Period: ".", Slash: "/", Semicolon: ";", Quote: "'", BracketLeft: "[", BracketRight: "]",
  Backslash: "\\", Minus: "-", Equal: "=", Backquote: "`", CapsLock: "Caps Lock" };
function keyLabel(code) {
  if (!code) return "—";
  if (KEY_LABEL[code]) return KEY_LABEL[code];
  if (/^Key[A-Z]$/.test(code)) return code.slice(3);
  if (/^Digit\d$/.test(code)) return code.slice(5);
  if (/^Numpad/.test(code)) return "Num " + code.slice(6);
  return code.replace(/([a-z])([A-Z])/g, "$1 $2");
}
const isBound = (action) => settings.binds[action].some((c) => c && keys.has(c));
const bindOf = (action) => settings.binds[action].filter(Boolean).map(keyLabel).join(" / ") || "unbound";
// the control to name in hints: gamepad button if one is connected, on-screen button on touch screens, else the key
const PAD_NAME = { fire: "A", intake: "RT", reverse: "LT", pause: "Start", view: "Back", hoodUp: "D-pad up", hoodDown: "D-pad down" };
const TOUCH_NAME = { fire: "Fire", intake: "Intake", reverse: "Out", pause: "Pause", view: "Overhead" };
const ctlName = (action) => (firstPad() && PAD_NAME[action]) || (isTouch && TOUCH_NAME[action]) || settings.binds[action].filter(Boolean).map(keyLabel)[0] || "unbound";
let rebinding = null;   // set by the Controls settings while waiting for a key
addEventListener("keydown", (e) => {
  if (rebinding) { e.preventDefault(); e.stopPropagation(); rebinding(e.code); return; }
  const inField = /INPUT|TEXTAREA|SELECT/.test(e.target.tagName) && e.target.type !== "checkbox" && e.target.type !== "range";
  if (state.match === "replay" && !ui.top()) {   // replay controls; Esc leaves the replay
    if (e.code === "Escape") { e.preventDefault(); exitReplay(); } else replayKey(e);
    return;
  }
  const pauseKey = settings.binds.pause.includes(e.code);
  if (pauseKey && !inField) { e.preventDefault(); onPauseKey(); return; }
  if (state.running && !state.paused && !ui.top()) {
    if (inField) return;
    keys.add(e.code);
    if (settings.binds.view.includes(e.code) && !e.repeat) toggleOverhead();
    if (e.code.startsWith("Arrow") || e.code === "Space" || Object.values(settings.binds).some((b) => b.includes(e.code))) e.preventDefault();
    return;
  }
  if (ui.top()) menuKey(e, inField);
}, true);
addEventListener("keyup", (e) => keys.delete(e.code));
addEventListener("blur", () => keys.clear());
function firstPad() {
  try { return (navigator.getGamepads ? [...navigator.getGamepads()] : []).find((g) => g && g.connected) || null; } catch (_) { return null; }
}
// ---- touch controls: left stick drives (robot-centric, like the gamepad stick), right stick turns
const touch = { lx: 0, ly: 0, rx: 0, intake: false, reverse: false, fire: false };
const isTouch = matchMedia("(pointer: coarse)").matches;
function bindStick(el, onMove) {
  const knob = el.querySelector("i"); let id = null;
  const set = (e) => {
    const r = el.getBoundingClientRect(), cx = r.left + r.width / 2, cy = r.top + r.height / 2, R = r.width / 2;
    let dx = (e.clientX - cx) / R, dy = (e.clientY - cy) / R; const m = Math.hypot(dx, dy); if (m > 1) { dx /= m; dy /= m; }
    knob.style.transform = `translate(${dx * R * 0.6}px, ${dy * R * 0.6}px)`; onMove(dx, dy);
  };
  el.addEventListener("pointerdown", (e) => { id = e.pointerId; el.setPointerCapture(id); set(e); e.preventDefault(); });
  el.addEventListener("pointermove", (e) => { if (e.pointerId === id) set(e); });
  const end = (e) => { if (e.pointerId !== id) return; id = null; knob.style.transform = ""; onMove(0, 0); };
  el.addEventListener("pointerup", end); el.addEventListener("pointercancel", end);
}
bindStick($("stickL"), (x, y) => { touch.lx = x; touch.ly = y; });
bindStick($("stickR"), (x) => { touch.rx = x * 0.8; });
document.querySelectorAll(".tbtns button").forEach((b) => {
  const k = b.dataset.b, on = (v) => { touch[k] = v; b.classList.toggle("on", v); };
  b.addEventListener("pointerdown", (e) => { b.setPointerCapture(e.pointerId); on(true); e.preventDefault(); });
  ["pointerup", "pointercancel", "pointerleave"].forEach((ev) => b.addEventListener(ev, () => on(false)));
});
function rumble(seconds, strength) {
  if (!settings.rumble) return;
  try {
    const gpad = firstPad();
    if (gpad && gpad.vibrationActuator) gpad.vibrationActuator.playEffect("dual-rumble", { duration: seconds * 1000, strongMagnitude: strength, weakMagnitude: strength });
    else if (isTouch && navigator.vibrate) navigator.vibrate(seconds * 1000);
  } catch (_) { /* no haptics: the LED still shows it */ }
}
const NO_PAD = { lx: 0, ly: 0, rx: 0, ry: 0, lt: 0, rt: 0, buttons: 0 };
function readPad() {
  const p = { lx: 0, ly: 0, rx: 0, ry: 0, lt: 0, rt: 0, buttons: 0 };
  const DZ = settings.deadzone, dz = (v) => (Math.abs(v) < DZ ? 0 : (v - Math.sign(v) * DZ) / (1 - DZ));
  const gpad = firstPad();
  if (gpad) {
    p.lx = dz(gpad.axes[0] || 0); p.ly = dz(gpad.axes[1] || 0); p.rx = dz(gpad.axes[2] || 0); p.ry = dz(gpad.axes[3] || 0);
    const b = (i) => gpad.buttons[i] && gpad.buttons[i].pressed;
    p.lt = gpad.buttons[6] ? gpad.buttons[6].value : 0; p.rt = gpad.buttons[7] ? gpad.buttons[7].value : 0;
    [[0, 0], [1, 1], [2, 2], [3, 3], [4, 4], [5, 5], [12, 6], [13, 7], [14, 8], [15, 9], [8, 10], [9, 11]].forEach(([i, bit]) => { if (b(i)) p.buttons |= 1 << bit; });
  }
  const fwd = isBound("forward"), back = isBound("back"), left = isBound("left"), right = isBound("right");
  if (fwd !== back) p.ly = fwd ? -1 : 1;
  if (left !== right) p.lx = left ? -1 : 1;
  const tl = isBound("turnLeft"), tr = isBound("turnRight");
  if (tl !== tr) p.rx = (tl ? -1 : 1) * settings.keyTurn;
  if (isBound("intake")) p.rt = 1;
  if (isBound("reverse")) p.lt = 1;
  if (isBound("fire")) p.buttons |= 1;
  if (isBound("hoodUp")) p.buttons |= 1 << 6;
  if (isBound("hoodDown")) p.buttons |= 1 << 7;
  if (touch.lx || touch.ly) { p.lx = touch.lx; p.ly = touch.ly; }
  if (touch.rx) p.rx = touch.rx;
  if (touch.intake) p.rt = 1;
  if (touch.reverse) p.lt = 1;
  if (touch.fire) p.buttons |= 1;
  return p;
}
