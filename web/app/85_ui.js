// ================= screens: main menu, match setup, settings, how to play, about, pause, report =================
// A stack of overlay screens. Esc / gamepad B goes back; during a match the stack is empty and Esc pauses.
const SCREENS = ["menu", "setup", "settings", "help", "about", "pause", "report", "replays"];
const ui = {
  stack: [],
  top() { return this.stack[this.stack.length - 1] || null; },
  show(name) { this.stack = [name]; this.render(true); },
  open(name) { if (this.top() !== name) this.stack.push(name); this.render(true); },
  back() {
    const t = this.top();
    if (t === "pause") { resumeMatch(); return; }
    if (t === "menu") return;
    if (t === "report") { toMenu(); return; }
    this.stack.pop();
    if (!this.stack.length) { if (state.paused) this.stack = ["pause"]; else if (state.match !== "live") this.stack = ["menu"]; }
    this.render(true);
  },
  closeAll() { this.stack = []; this.render(false); },
  render(focus) {
    const t = this.top();
    for (const s of SCREENS) $("scr-" + s).hidden = s !== t;
    if (t === "settings") renderSettings();
    if (t === "help") renderHelp();
    if (t === "setup") { drawStandMap(); showParse(); }
    if (t === "about") renderUpdates();
    if (t === "replays") renderReplaysScreen();
    renderUpdates();
    if (focus && t) {
      const el = $("scr-" + t), first = el.querySelector("[data-autofocus]") || el.querySelector(".mbtn, button:not([disabled]), [role='tab'][aria-selected='true'], input, select");
      if (first) first.focus({ preventScroll: true });
    }
  },
};
document.addEventListener("click", (e) => {
  const go = e.target.closest("[data-go]"), back = e.target.closest("[data-back]");
  if (go) { audio.start(); uiClick(); ui.open(go.dataset.go); }
  else if (back) { uiClick(); ui.back(); }
  else if (e.target.closest(".screen button")) { audio.start(); uiClick(); }
});
$("mQuick").onclick = () => startMatch({ alliance: state.alliance, mode: "practice", seed: 1 + (Math.random() * 9999 | 0), difficulty: +$("diff").value, opponent: $("opp").value, instruction: $("instr").value });
$("pResume").onclick = resumeMatch;
$("pRestart").onclick = restartMatch;
$("pLeave").onclick = toMenu;
$("pauseBtn").onclick = pauseMatch;
$("viewBtn").onclick = toggleOverhead;
$("rAgain").onclick = playAgain;
$("rSetup").onclick = () => { robotSound.stop(); state.match = "none"; showHud(false); ui.show("menu"); ui.open("setup"); startDemo(); };
$("rMenu").onclick = toMenu;

// ---- match setup form ----
const seg = (a, b, key, va, vb, after) => {
  $(a).onclick = () => { state[key] = va; $(a).setAttribute("aria-pressed", "true"); $(b).setAttribute("aria-pressed", "false"); if (after) after(); };
  $(b).onclick = () => { state[key] = vb; $(b).setAttribute("aria-pressed", "true"); $(a).setAttribute("aria-pressed", "false"); if (after) after(); };
};
seg("aRed", "aBlue", "alliance", "red", "blue", () => { placeDriver(); drawStandMap(); });
seg("mComp", "mPrac", "mode", "competition", "practice", () => {
  $("modeText").textContent = state.mode === "competition"
    ? "Competition: your robot sits still during AUTO (no autonomous program in the browser build); you drive in TELEOP."
    : "Practice: you may drive during AUTO. Results are labelled practice and are not competition-accurate.";
});
function showParse() {
  if (!state.simReady || !state.sim) return;
  const d = state.sim.parseInstruction($("instr").value);
  $("parsed").innerHTML = d.matched.length
    ? d.matched.map((m) => "<b>✓</b> " + esc(m)).join("<br>")
    : (d.text ? "Not understood by the partner AI yet; it will play its default strategy. Try phrases like \"defend\", \"prioritize pollen\", \"stay out of my way\", \"do nothing\"." : "");
}
$("instr").addEventListener("input", showParse);
$("seedRnd").onclick = () => { $("seed").value = 1 + (Math.random() * 99999 | 0); };
$("setupForm").addEventListener("submit", (e) => { e.preventDefault(); startMatch(setupFromForm()); });

// ---- settings: described once, rendered as tabs of rows ----
const pct = (v) => Math.round(v * 100) + " %";
const SETTINGS_UI = [
  { tab: "Graphics", keys: ["preset", ...GFX_KEYS, "toneMap", "exposure", "autoQuality", "fps"], rows: [
    { k: "preset", type: "select", label: "Quality preset", help: "Sets everything below. Ultra needs a fast graphics card; Low suits phones and older laptops.", opts: [["low", "Low"], ["balanced", "Balanced"], ["high", "High"], ["ultra", "Ultra"], ["custom", "Custom"]] },
    { k: "res", type: "select", label: "Render resolution", help: "Native draws every screen pixel on high-density displays (sharpest, slowest).", opts: [[0.5, "50 %"], [0.67, "67 %"], [0.85, "85 %"], [1, "100 %"], [2, "Native"]] },
    { k: "shadowLights", type: "select", label: "Shadow-casting lights", help: "How many of the 8 overhead fixtures cast shadows. Real fields show several soft shadows under every robot.", opts: [[0, "None"], [1, "1"], [2, "2"], [4, "4"], [8, "All 8"]] },
    { k: "shadowMap", type: "select", label: "Shadow detail", opts: [[512, "Low"], [1024, "Medium"], [2048, "High"]] },
    { k: "softShadows", type: "toggle", label: "Soft contact shadows", help: "Shadows are sharp where an object touches the floor and soften with height, as under real stage lights." },
    { k: "ao", type: "toggle", label: "Ambient occlusion", help: "Darkens creases and the floor right under robots and game pieces." },
    { k: "bloom", type: "toggle", label: "Light glow", help: "Glare around the fixture lenses and the robot status lights." },
    { k: "film", type: "toggle", label: "Camera grain and vignette", help: "The fine noise and darker corners of a real camera." },
    { k: "vegetation", type: "select", label: "Forest detail", opts: [["full", "Full"], ["light", "Light (no grass or bushes)"]] },
    { k: "toneMap", type: "select", label: "Colour", help: "Natural keeps bright lights and colours closer to what an eye sees; Filmic is punchier.", opts: [["agx", "Natural"], ["aces", "Filmic"]] },
    { k: "exposure", type: "range", label: "Brightness", min: 0.6, max: 1.6, step: 0.05, fmt: pct },
    { k: "autoQuality", type: "toggle", label: "Lower quality if the game stutters", help: "During a match, steps the preset down when the frame rate stays under 30." },
    { k: "fps", type: "toggle", label: "Show frame rate" },
  ] },
  { tab: "Camera", keys: ["fov", "eye", "headFollow", "headSpeed", "lookSens", "invertLook"], rows: [
    { k: "fov", type: "range", label: "Field of view", min: 50, max: 110, step: 1, fmt: (v) => v + "°" },
    { k: "eye", type: "range", label: "Eye height", help: "Your eye level at the driver station.", min: 1.0, max: 2.1, step: 0.05, fmt: (v) => v.toFixed(2) + " m" },
    { k: "headFollow", type: "range", label: "Head follows your robot", help: "0 looks at the middle of the field; 100 keeps your robot centred.", min: 0, max: 1, step: 0.05, fmt: pct },
    { k: "headSpeed", type: "range", label: "Head turn time", help: "How quickly the view catches up with your robot.", min: 0.05, max: 0.8, step: 0.01, fmt: (v) => v.toFixed(2) + " s" },
    { k: "lookSens", type: "range", label: "Drag-to-look sensitivity", min: 0.3, max: 2.5, step: 0.1, fmt: (v) => v.toFixed(1) + "×" },
    { k: "invertLook", type: "toggle", label: "Invert vertical look" },
  ] },
  { tab: "Controls", keys: ["binds", "deadzone", "keyTurn", "rumble"], rows: [
    { type: "binds" },
    { k: "keyTurn", type: "range", label: "Keyboard turn speed", min: 0.3, max: 1, step: 0.05, fmt: pct },
    { k: "deadzone", type: "range", label: "Gamepad stick dead zone", min: 0, max: 0.25, step: 0.01, fmt: pct },
    { k: "rumble", type: "toggle", label: "Controller rumble", help: "A short buzz when the shot is ready (gamepads and phones that support it)." },
  ] },
  { tab: "Audio", keys: ["vol", "volCues", "volRobot", "volImpacts", "mute"], rows: [
    { k: "vol", type: "range", label: "Master volume", min: 0, max: 1, step: 0.05, fmt: pct },
    { k: "volCues", type: "range", label: "Match sounds", help: "Start, end of AUTO, TELEOP bells, endgame and final buzzer.", min: 0, max: 1, step: 0.05, fmt: pct },
    { k: "volRobot", type: "range", label: "Your robot", help: "Drive motors, flywheel and intake.", min: 0, max: 1, step: 0.05, fmt: pct },
    { k: "volImpacts", type: "range", label: "Impacts", min: 0, max: 1, step: 0.05, fmt: pct },
    { k: "mute", type: "toggle", label: "Mute everything" },
  ] },
  { tab: "Gameplay", keys: ["aimAssist", "hints", "keysHint", "robotHud", "hudScale"], rows: [
    { k: "aimAssist", type: "toggle", label: "Auto-aim while firing", help: "Holding Fire turns the robot to your HIVE. Off: you aim; the light still turns green when the shot is lined up. Applies from the next match." },
    { k: "hints", type: "toggle", label: "Hints", help: "Short tips at the top of the screen during a match." },
    { k: "keysHint", type: "toggle", label: "Show the controls strip" },
    { k: "robotHud", type: "toggle", label: "Show robot status", help: "Shooter light and the balls your robot holds." },
    { k: "hudScale", type: "range", label: "Scoreboard size", min: 0.8, max: 1.5, step: 0.05, fmt: pct },
  ] },
  { tab: "Accessibility", keys: ["reducedMotion", "highContrast", "uiScale"], rows: [
    { k: "reducedMotion", type: "toggle", label: "Reduce motion", help: "Still camera behind the menus, slower head turns, no menu animations." },
    { k: "highContrast", type: "toggle", label: "High-contrast interface" },
    { k: "uiScale", type: "range", label: "Menu text size", min: 0.85, max: 1.4, step: 0.05, fmt: pct },
  ] },
];
let setTab = 0;
function renderSettings() {
  const tabs = $("setTabs"), panel = $("setPanel");
  tabs.replaceChildren(...SETTINGS_UI.map((t, i) => {
    const b = document.createElement("button"); b.type = "button"; b.setAttribute("role", "tab"); b.id = "setTab" + i;
    b.setAttribute("aria-selected", String(i === setTab)); b.tabIndex = i === setTab ? 0 : -1; b.textContent = t.tab;
    b.onclick = () => { setTab = i; renderSettings(); $("setTab" + i).focus(); };
    return b;
  }));
  panel.setAttribute("aria-labelledby", "setTab" + setTab);
  panel.replaceChildren(...SETTINGS_UI[setTab].rows.flatMap((r) => (r.type === "binds" ? bindRows() : [settingRow(r)])));
}
function settingRow(r) {
  const row = document.createElement("div"); row.className = "srow";
  const id = "set-" + r.k, lab = document.createElement("label"); lab.className = "k"; lab.htmlFor = id; lab.textContent = r.label;
  const v = document.createElement("div"); v.className = "v";
  let ctl;
  if (r.type === "select") {
    ctl = document.createElement("select");
    for (const [val, text] of r.opts) { const o = document.createElement("option"); o.value = String(val); o.textContent = text; ctl.append(o); }
    ctl.value = String(settings[r.k]);
    ctl.onchange = () => { const raw = ctl.value, val = typeof DEFAULTS[r.k] === "number" ? +raw : raw; setSetting(r.k, val); if (r.k === "preset" || GFX_KEYS.includes(r.k)) renderSettings(); };
  } else if (r.type === "toggle") {
    ctl = document.createElement("input"); ctl.type = "checkbox"; ctl.className = "switch"; ctl.setAttribute("role", "switch"); ctl.checked = !!settings[r.k];
    ctl.onchange = () => { setSetting(r.k, ctl.checked); if (GFX_KEYS.includes(r.k)) renderSettings(); };
  } else {
    ctl = document.createElement("input"); ctl.type = "range"; ctl.min = r.min; ctl.max = r.max; ctl.step = r.step; ctl.value = settings[r.k];
    const out = document.createElement("output"); out.htmlFor = id; out.textContent = r.fmt(settings[r.k]);
    ctl.oninput = () => { const val = +ctl.value; out.textContent = r.fmt(val); setSetting(r.k, val); };
    v.append(ctl, out); ctl.id = id; row.append(lab, v); if (r.help) row.append(helpText(r.help));
    return row;
  }
  ctl.id = id; v.append(ctl); row.append(lab, v);
  if (r.help) row.append(helpText(r.help));
  return row;
}
function helpText(t) { const h = document.createElement("div"); h.className = "h"; h.textContent = t; return h; }
function bindRows() {
  return KEY_ACTIONS.map(([action, label]) => {
    const row = document.createElement("div"); row.className = "srow";
    const k = document.createElement("div"); k.className = "k"; k.textContent = label;
    const v = document.createElement("div"); v.className = "v";
    for (let slot = 0; slot < 2; slot++) {
      const b = document.createElement("button"); b.type = "button"; b.className = "kb";
      const fixed = action === "pause" && slot === 0;
      b.textContent = keyLabel(settings.binds[action][slot]); b.disabled = fixed;
      b.setAttribute("aria-label", `${label}, ${slot ? "second" : "first"} key: ${keyLabel(settings.binds[action][slot])}${fixed ? " (fixed)" : ". Press to change"}`);
      b.onclick = () => {
        b.textContent = "Press a key…"; b.classList.add("wait");
        rebinding = (code) => {
          rebinding = null;
          if (code !== "Escape") {
            if (code === "Backspace" || code === "Delete") code = "";
            const binds = JSON.parse(JSON.stringify(settings.binds));
            if (code) for (const a of Object.keys(binds)) binds[a] = binds[a].map((c, i) => (c === code && !(a === action && i === slot) && !(a === "pause" && i === 0) ? "" : c));
            binds[action][slot] = code;
            setSetting("binds", binds);
          }
          renderSettings(); renderKeysHint();
          const again = $("setPanel").querySelectorAll(".kb")[KEY_ACTIONS.findIndex((x) => x[0] === action) * 2 + slot]; if (again) again.focus();
        };
      };
      v.append(b);
    }
    row.append(k, v);
    if (action === "fire") row.append(helpText("Backspace clears a key. A key used elsewhere moves here."));
    return row;
  });
}
$("setResetTab").onclick = () => { resetSettings(SETTINGS_UI[setTab].keys); renderSettings(); renderKeysHint(); };
onSettings((k) => {
  document.body.classList.toggle("hc", settings.highContrast);
  document.body.classList.toggle("reduce-motion", settings.reducedMotion);
  document.documentElement.style.setProperty("--ui-scale", settings.uiScale);
  document.documentElement.style.setProperty("--hud-scale", settings.hudScale);
  $("fps").hidden = !settings.fps || !state.running;
  if (k === "binds" || k === "*") renderKeysHint();
});

// ---- how to play (controls from the current key bindings; points from the season package) ----
function renderHelp() {
  const rows = KEY_ACTIONS.map(([a, label]) => `<dt>${settings.binds[a].filter(Boolean).map((c) => `<kbd>${esc(keyLabel(c))}</kbd>`).join(" ") || "—"}</dt><dd>${esc(label)}</dd>`);
  $("helpKeys").innerHTML = rows.join("") + "<dt><kbd>Drag</kbd></dt><dd>Look around (the view drifts back)</dd>";
  const tm = season.timing;
  $("helpMatch").textContent = `${tm.auto_s} s AUTO, ${tm.transition_s} s to pick up the controllers, then ${tm.teleop_s / 60} minutes of TELEOP. `
    + `Collect POLLEN and NECTAR and launch them into your alliance's HIVE; a full HIVE tips. FLOWERS unlock with ${tm.flowerUnlockRemaining_s} s left; `
    + `the endgame warning sounds at ${tm.endgameWarningRemaining_s} s. In Competition rules your robot waits during AUTO; Practice lets you drive all match.`;
  const P = season.points, name = (k) => k.replace(/_/g, " ").toLowerCase().replace(/^./, (c) => c.toUpperCase());
  const rowsP = Object.entries(P).filter(([k]) => k !== "source").map(([k, v]) => {
    const when = typeof v === "number" ? (k.includes("FOUL") ? "against" : "") : Object.entries(v).map(([ph, pts]) => `${ph === "end" ? "end of match" : ph.toUpperCase()}: ${pts}`).join(" · ");
    return `<tr><td>${esc(name(k))}</td><td>${typeof v === "number" ? `${v} to the other alliance` : esc(when)}</td></tr>`;
  });
  $("helpPoints").innerHTML = "<tr><th>Item</th><th>Points</th></tr>" + rowsP.join("");
  $("helpSrc").textContent = `From ${season.manual.title} ${season.manual.teamUpdate} (${P.source}). The manual is authoritative.`;
}

// ---- keyboard and gamepad navigation in menus ----
function focusables() {
  const t = ui.top(); if (!t) return [];
  return [...$("scr-" + t).querySelectorAll("button, input, select, textarea, canvas[tabindex='0'], [role='tab']")]
    .filter((el) => !el.disabled && el.offsetParent !== null && !(el.getAttribute("role") === "tab" && el.getAttribute("aria-selected") !== "true"));
}
function moveFocus(d) {
  const f = focusables(); if (!f.length) return;
  const i = f.indexOf(document.activeElement);
  f[i < 0 ? 0 : (i + d + f.length) % f.length].focus();
  const a = document.activeElement; if (a && a.scrollIntoView) a.scrollIntoView({ block: "nearest" });
}
function adjust(el, d) {   // left/right on a control
  if (!el) return false;
  if (el.type === "range") { el.value = clamp(+el.value + d * +el.step, +el.min, +el.max); el.dispatchEvent(new Event("input")); return true; }
  if (el.tagName === "SELECT") { const n = el.options.length; el.selectedIndex = clamp(el.selectedIndex + d, 0, n - 1); el.dispatchEvent(new Event("change")); return true; }
  if (el.getAttribute("role") === "tab") { setTab = (setTab + d + SETTINGS_UI.length) % SETTINGS_UI.length; renderSettings(); $("setTab" + setTab).focus(); return true; }
  if (el.parentElement && el.parentElement.classList.contains("seg")) { const sib = d < 0 ? el.previousElementSibling : el.nextElementSibling; if (sib) { sib.click(); sib.focus(); } return true; }
  if (el.id === "standMap") { el.dispatchEvent(new KeyboardEvent("keydown", { key: d < 0 ? "ArrowLeft" : "ArrowRight" })); return true; }
  return false;
}
function menuKey(e, inField) {
  const a = document.activeElement;
  if (inField && a && a.tagName === "TEXTAREA") return;
  if (e.key === "ArrowDown" || e.key === "ArrowUp") {
    if (a && (a.id === "standMap" || a.tagName === "SELECT")) return;   // native / map handling
    e.preventDefault(); moveFocus(e.key === "ArrowDown" ? 1 : -1);
  } else if ((e.key === "ArrowLeft" || e.key === "ArrowRight") && a && a.getAttribute("role") === "tab") {
    e.preventDefault(); adjust(a, e.key === "ArrowLeft" ? -1 : 1);
  }
}
const padNav = { prev: [], hold: 0, dir: 0 };
function pollPad(dt) {
  const gp_ = firstPad(); if (!gp_) return;
  const pressed = (i) => !!(gp_.buttons[i] && gp_.buttons[i].pressed), edge = (i) => pressed(i) && !padNav.prev[i];
  const t = ui.top();
  if (!t && state.match === "replay") replayPad(edge);
  else if (!t) {
    if (state.match === "live" && edge(9)) pauseMatch();
    if (state.match === "live" && edge(8)) toggleOverhead();
  } else {
    const ay = gp_.axes[1] || 0, ax = gp_.axes[0] || 0;
    const dir = pressed(12) || ay < -0.6 ? -1 : pressed(13) || ay > 0.6 ? 1 : 0, side = pressed(14) || ax < -0.6 ? -1 : pressed(15) || ax > 0.6 ? 1 : 0;
    const any = dir || side;
    if (any) {
      padNav.hold = padNav.dir === dir * 10 + side ? padNav.hold + dt : 0;
      const fire = padNav.dir !== dir * 10 + side || padNav.hold > 0.38;
      if (fire) {
        if (padNav.dir === dir * 10 + side) padNav.hold = 0.28;   // auto-repeat
        document.body.classList.add("pad-nav");
        if (dir) moveFocus(dir); else if (!adjust(document.activeElement, side)) moveFocus(side);
      }
    }
    padNav.dir = any ? dir * 10 + side : 0;
    if (edge(0)) { const a = document.activeElement; document.body.classList.add("pad-nav"); if (a && a.type === "checkbox") { a.checked = !a.checked; a.dispatchEvent(new Event("change")); } else if (a && a.click) a.click(); }
    if (edge(1)) ui.back();
    if (edge(9) && t === "pause") resumeMatch();
  }
  padNav.prev = gp_.buttons.map((b) => b && b.pressed);
}
addEventListener("pointerdown", () => document.body.classList.remove("pad-nav"));
