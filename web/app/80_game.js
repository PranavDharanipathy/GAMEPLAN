// ================= matches: AI demo behind the menus, your match, pause, restart, final buzzer =================
const seasonText = $("seasonJson").textContent, compsText = $("compsJson").textContent, robotText = $("robotJson").textContent;
async function createSim(opts) {
  const sim = await GamePlanWasm.create(wasmBytes, (l) => console.log(l));   // fresh world per match
  clearRobots(); lastFrameT = -1; userRobot.wheel = userRobot.fly = userRobot.intake = 0;
  sim.init(seasonText, compsText, robotText, robotText, opts);
  return sim;
}
let demoSeed = 101, demoBusy = false;
async function startDemo() {   // four AI robots play a competition match behind the menus
  if (!wasmBytes || demoBusy || state.match === "live") return;
  demoBusy = true;
  try {
    const sim = await createSim({ seed: demoSeed++, alliance: "red", mode: "competition", difficulty: 0.9, opponentPersonality: "aggressive",
      partnerPersonality: "coordinated", partnerInstruction: "", uncertainty: "realistic", demo: true });
    if (state.match === "live") return;
    state.sim = sim; state.match = "demo"; state.running = false; state.paused = false; state.overhead = false;
  } finally { demoBusy = false; }
}
function setupFromForm() {
  return { alliance: state.alliance, mode: state.mode, seed: Math.max(1, +$("seed").value | 0 || 1), difficulty: +$("diff").value,
    opponent: $("opp").value, instruction: $("instr").value };
}
async function startMatch(cfg) {
  cfg = Object.assign({ alliance: "red", mode: "competition", seed: 7, difficulty: 0.6, opponent: "aggressive", instruction: "" }, cfg || setupFromForm());
  if (!wasmBytes) return;
  audio.start();
  state.setup = cfg; state.alliance = cfg.alliance; state.mode = cfg.mode;
  state.match = "live"; state.loading = true;   // claims the sim before the async create, so a finishing demo can't replace it
  let sim = null;
  try { sim = await createSim({ seed: cfg.seed, alliance: cfg.alliance, mode: cfg.mode, difficulty: cfg.difficulty, opponentPersonality: cfg.opponent,
    partnerPersonality: "coordinated", partnerInstruction: cfg.instruction, uncertainty: "realistic", aimAssist: settings.aimAssist }); }
  catch (e) { console.error(e); }
  finally { state.loading = false; }
  if (!sim) { state.match = "none"; toast("The match could not start. Reload the page and try again."); ui.show("menu"); startDemo(); return; }
  state.sim = sim;
  state.running = true; state.paused = false; state.overhead = false; state.elec = []; elecT = -1; lastMatchT = -99; hintSeen.clear(); prevHeld = 0;
  $("viewBtn").textContent = "Overhead"; $("pauseBtn").hidden = false;
  placeDriver(); ui.closeAll(); showHud(true); perf.reset(); robotSound.start(); captureStart();
  $("finalBanner").hidden = true;
  last = performance.now();
  canvas.focus();
}
function pauseMatch() {
  if (state.match !== "live" || state.paused) return;
  state.paused = true; keys.clear(); robotSound.stop();
  $("pauseTime").textContent = $("timer").textContent + " · " + $("phase").textContent;
  ui.open("pause");
}
function resumeMatch() {
  if (!state.paused) return;
  state.paused = false; ui.closeAll(); robotSound.start(); last = performance.now(); canvas.focus();
}
function restartMatch() { if (state.setup) startMatch(state.setup); }
function playAgain() { if (state.setup) { const s = Object.assign({}, state.setup, { seed: state.setup.seed + 1 }); $("seed").value = s.seed; startMatch(s); } }
function toMenu() {
  robotSound.stop(); state.running = false; state.paused = false; state.match = "none";
  showHud(false); $("finalBanner").hidden = true; ui.show("menu"); startDemo();
}
function onPauseKey() {
  if (rebinding) return;
  if (state.match === "replay" && !ui.top()) { exitReplay(); return; }
  if (state.match === "live" && !state.paused && !ui.top()) { pauseMatch(); return; }
  if (state.paused && ui.top() === "pause") { resumeMatch(); return; }
  if (ui.top()) ui.back();
}
function matchOver() {
  state.running = false; state.match = "over"; robotSound.stop(); keys.clear();
  captureEnd(state.sim.result());
  ["keys", "touch", "hint", "robotHud", "pauseBtn"].forEach((id) => { $(id).hidden = true; });
  const h = state.sim.hud();
  $("finalRed").textContent = h.red; $("finalBlue").textContent = h.blue;
  const mine = state.alliance === "red" ? h.red : h.blue, theirs = state.alliance === "red" ? h.blue : h.red;
  $("finalWord").textContent = mine > theirs ? "Win" : mine < theirs ? "Loss" : "Tie";
  $("finalBanner").hidden = false;
  setTimeout(() => { if (state.match === "over" && !ui.top()) { $("finalBanner").hidden = true; openReport(); } }, 2600);
  renderUpdates();
}
function showHud(on) {
  ["hud", "conn", "viewBtns"].forEach((id) => { $(id).hidden = !on; });
  $("keys").hidden = !on || !settings.keysHint || isTouch;
  $("robotHud").hidden = !on || !settings.robotHud;
  $("fps").hidden = !on || !settings.fps;
  $("touch").hidden = !on || !isTouch; document.body.classList.toggle("touch-on", on && isTouch);
  if (!on) $("hint").hidden = true;
  renderKeysHint(); renderUpdates();
}
onSettings((k) => { if (state.running) showHud(true); if (k === "aimAssist" && state.running) toast("Auto-aim changes take effect at the next match"); });
function renderKeysHint() {
  $("keys").textContent = `${bindOf("forward")}${bindOf("left")}${bindOf("back")}${bindOf("right")} drive · ${bindOf("turnLeft")}/${bindOf("turnRight")} turn · ${bindOf("intake")} intake · ${bindOf("reverse")} reverse · hold ${bindOf("fire")} to fire${settings.aimAssist ? " (auto-aim)" : ""} · ${bindOf("pause")} pause`;
}
// ---- everything that follows one simulation step: poses, HUD, hints, sound, electrical log, end
let last = performance.now(), elecT = -1;
const hintSeen = new Set();
function afterStep(done) {
  const fr = state.sim.frame();
  draw(fr);
  if (state.match === "live") {
    captureFrame(fr);
    const h = state.sim.hud();
    $("timer").textContent = h.timer; $("phase").textContent = h.phase.replace("_", " ") + (state.mode === "practice" ? " · practice" : "");
    $("sRed").textContent = h.red; $("sBlue").textContent = h.blue;
    $("connText").textContent = h.connection; $("conn").classList.toggle("bad", h.connection !== "Connected");
    const mt = fr[0] - 3.0;   // match time (the browser build starts the match after 3 s of pre-match)
    cueAt(mt);
    updateRobotHud(h);
    showHint(h);
    const el = state.sim.elec();
    if (el[0] - elecT >= 0.1) { elecT = el[0]; state.elec.push(Array.from(el)); }   // stored now, shown only after the match
    robotSound.update();
  }
  if (done) { if (state.match === "live") matchOver(); else if (state.match === "demo") startDemo(); }
}
function updateRobotHud(h) {
  if ($("robotHud").hidden) return;
  const st = userRobot.status, cap = 4;
  $("rhState").textContent = st === 2 ? "Ready" : st === 1 ? "Spinning up" : (h.phase === "TELEOP" || (state.mode === "practice" && h.phase === "AUTO") ? "Shooter idle" : "Disabled");
  $("rhLed").className = "led" + (st === 2 ? " ok" : st === 1 ? " busy" : "");
  const held = Math.round(userRobot.held), dots = $("rhBalls").children;
  for (let i = 0; i < dots.length; i++) dots[i].classList.toggle("on", i < Math.min(cap, held));
}
let prevHeld = 0;
function showHint(h) {
  const driving = h.phase === "TELEOP" || (state.mode === "practice" && h.phase === "AUTO");
  const held = Math.round(userRobot.held);
  if (held > 0) hintSeen.add("intake");
  if (held < prevHeld) hintSeen.add("fire");   // a ball has left the robot: the driver knows how to shoot
  prevHeld = held;
  let hint = "";
  if (settings.hints) {
    if (h.phase === "AUTO" && state.mode === "competition") hint = "AUTO: your robot has no autonomous program here, so it waits. You drive when TELEOP starts at 2:00.";
    else if (h.phase === "TRANSITION") hint = "Pick up the controller: TELEOP starts in a few seconds.";
    else if (driving && !hintSeen.has("intake")) hint = `Drive over POLLEN with ${ctlName("intake")} held to pick it up.`;
    else if (driving && held > 0 && !hintSeen.has("fire")) hint = `Hold ${ctlName("fire")} to shoot. ${settings.aimAssist ? "The robot turns to your HIVE and fires when its light is green." : "Aim at your HIVE; the light turns green when the shot is lined up."}`;
  }
  if ($("hint").textContent !== hint) $("hint").textContent = hint;
  $("hint").hidden = !hint;
}
// ---- keep it playable: if frames are slow during a match, step the graphics down one preset
const perf = { n: 0, sum: 0, reset() { this.n = 0; this.sum = 0; }, sample(dt) {
  if (!settings.autoQuality) return;
  this.n++; this.sum += dt;
  if (this.n < 150) return;
  const ms = this.sum / this.n * 1000; this.reset();
  if (ms <= 33) return;
  const cur = settings.preset === "custom" ? "high" : settings.preset, i = PRESET_ORDER.indexOf(cur);
  if (i < 0 || i >= PRESET_ORDER.length - 1) return;
  setSetting("preset", PRESET_ORDER[i + 1]);
  toast(`Graphics lowered to ${PRESET_ORDER[i + 1]} for smoother play (${Math.round(1000 / ms)} fps). Change it in Settings.`);
} };
let toastTimer = 0;
function toast(msg) { const t = $("note2"); t.textContent = msg; t.hidden = false; clearTimeout(toastTimer); toastTimer = setTimeout(() => { t.hidden = true; }, 6000); }
const fpsMeter = { n: 0, t: 0, sample(dt) { this.n++; this.t += dt; if (this.t >= 0.5) { $("fps").textContent = Math.round(this.n / this.t) + " fps"; this.n = 0; this.t = 0; } } };

// ================= post-match report =================
function chart(cv, series, unit) {
  const dpr = Math.min(2, devicePixelRatio || 1), W = cv.clientWidth * dpr, Hh = cv.clientHeight * dpr;
  cv.width = W; cv.height = Hh; const c = cv.getContext("2d");
  const pad = { l: 44 * dpr, r: 10 * dpr, t: 12 * dpr, b: 22 * dpr };
  const xs = state.elec.map((r) => r[0]);
  let lo = Infinity, hi = -Infinity; series.forEach((s) => state.elec.forEach((r) => { lo = Math.min(lo, r[s.i]); hi = Math.max(hi, r[s.i]); }));
  if (!isFinite(lo)) return; if (hi - lo < 1e-6) { hi += 1; lo -= 1; } const m = (hi - lo) * 0.08; lo -= m; hi += m;
  const X = (x) => pad.l + (x - xs[0]) / Math.max(1e-6, xs[xs.length - 1] - xs[0]) * (W - pad.l - pad.r);
  const Y = (y) => pad.t + (1 - (y - lo) / (hi - lo)) * (Hh - pad.t - pad.b);
  c.font = `${11 * dpr}px IBM Plex Mono, monospace`; c.fillStyle = "#8e9aa5"; c.strokeStyle = "#2b343d"; c.lineWidth = dpr;
  for (let k = 0; k <= 4; k++) { const v = lo + (hi - lo) * k / 4, y = Y(v); c.beginPath(); c.moveTo(pad.l, y); c.lineTo(W - pad.r, y); c.stroke(); c.fillText(v.toFixed(1), 4 * dpr, y + 4 * dpr); }
  c.fillText(unit, 4 * dpr, 10 * dpr);
  [38, 98, 138].forEach((t) => { const x = X(t); if (x > pad.l && x < W - pad.r) { c.setLineDash([3 * dpr, 4 * dpr]); c.beginPath(); c.moveTo(x, pad.t); c.lineTo(x, Hh - pad.b); c.stroke(); c.setLineDash([]); } });
  series.forEach((s) => { c.strokeStyle = s.color; c.lineWidth = 1.6 * dpr; c.beginPath(); state.elec.forEach((r, i) => { const x = X(r[0]), y = Y(r[s.i]); if (i) c.lineTo(x, y); else c.moveTo(x, y); }); c.stroke(); });
}
function openReport() {
  const r = state.sim.result(), u = r.userRobotElectrical;
  const rows = (a) => Object.entries(r[a].breakdown).map(([k, v]) => `<tr><td>${esc(k.replace(/_/g, " "))}</td><td>${v}</td></tr>`).join("") + `<tr><td>Foul credit</td><td>${r[a].foulCredit}</td></tr>`;
  const viol = r.violations.length ? r.violations.map((v) => `<tr><td>${v.matchTime.toFixed(1)} s</td><td>${esc(v.rule)} <span class="tag">${v.judgment ? "judgment" : "objective"}</span></td><td>${esc(v.offender)}</td><td>${esc(v.penalty)}</td><td>${esc(v.rationale)}</td></tr>`).join("") : `<tr><td colspan="5">No violations assessed.</td></tr>`;
  const motors = u.motors.map((m) => `<tr><td>${esc(m.name)}</td><td>${m.energy_J.toFixed(0)}</td><td>${m.peak_A.toFixed(1)}</td><td>${m.winding_C.toFixed(0)}</td></tr>`).join("");
  const pd = r.partnerDirectives || { matched: [], notUnderstood: [] };
  const mine = state.alliance === "red" ? r.red.total : r.blue.total, theirs = state.alliance === "red" ? r.blue.total : r.red.total;
  $("reportBody").innerHTML = `
    <div class="row"><h1>${mine > theirs ? "Win" : mine < theirs ? "Loss" : "Tie"} <span class="tag">${esc(state.alliance)} alliance</span></h1><span class="tag">${esc(season.manual.teamUpdate)} · ${esc(season.rulesEngineVersion)} · seed ${state.setup ? state.setup.seed : ""}${state.mode === "practice" ? " · PRACTICE" : ""}</span></div>
    <div class="bigscore"><span class="r">${r.red.total}</span><span class="vs">vs</span><span class="b">${r.blue.total}</span><span class="tag">RP red ${r.red.rp} · blue ${r.blue.rp}</span></div>
    <div class="grid"><div class="tbl-wrap"><table><tr><th>Red</th><th>pts</th></tr>${rows("red")}</table></div><div class="tbl-wrap"><table><tr><th>Blue</th><th>pts</th></tr>${rows("blue")}</table></div></div>
    <h2>Violations</h2><div class="tbl-wrap"><table><tr><th>Time</th><th>Rule</th><th>Alliance</th><th>Penalty</th><th>Basis</th></tr>${viol}</table></div>
    <h2>Your robot's electrical system</h2>
    <p class="lede">Collected during the match and shown only now. Min hub bus ${u.minHubBusV.toFixed(2)} V, peak battery current ${u.peakBatteryA.toFixed(1)} A, ${(u.energy_J / 1000).toFixed(2)} kJ used, final charge ${(u.finalSoc * 100).toFixed(1)} %, ${r.distance_m.toFixed(1)} m driven. Values are calculated by the coupled model; component data carries provenance in the GamePlan repository.</p>
    <canvas class="chart" id="cV" aria-label="Battery terminal and hub bus voltage over the match"></canvas>
    <canvas class="chart" id="cI" aria-label="Battery current over the match"></canvas>
    <p class="parsed">Dashed lines: TELEOP start, FLOWER unlock (1:00), final 20 s.</p>
    <div class="tbl-wrap"><table><tr><th>Motor</th><th>Energy J</th><th>Peak A</th><th>Winding °C</th></tr>${motors}</table></div>
    <h2>Alliance partner</h2>
    <p class="lede">${pd.matched.length ? "Followed: " + pd.matched.map(esc).join("; ") : "Played its default strategy."}${pd.notUnderstood && pd.notUnderstood.length ? " Not understood: " + pd.notUnderstood.map(esc).join("; ") : ""}</p>`;
  ui.show("report");
  chart($("cV"), [{ i: 1, color: "#f2c230" }, { i: 3, color: "#4fbf7a" }], "V  battery · hub");
  chart($("cI"), [{ i: 2, color: "#e0453a" }], "A  battery");
}
