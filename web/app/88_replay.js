// ================= replays: watch a match again with a timeline, events and free cameras =================
// Two sources, one viewer. Your browser matches are captured while you play (20 view frames a second,
// in memory until the next match). Matches recorded with GamePlan on a computer - including ones driven
// by a team's real code - are exported with `gp-sim replay-export` (format: core/include/gameplan/replay.hpp)
// and opened from a file. Frames are interpolated, so playback is smooth at any speed.
const capture = { frames: [], next: 0, infos: null, alliance: "red", stand: null, setup: null, violations: [] };
const replay = { frames: [], times: null, infos: null, buf: null, cur: null, t: 0, start: 3, end: 0, playing: true, speed: 1,
  cam: "broadcast", follow: 0, events: [], marks: [], timing: null, alliance: "red", stand: { u: 0.5, v: 0.3 }, title: "", returnTo: "menu",
  camPos: new THREE.Vector3(), camTgt: new THREE.Vector3(), camReady: false, orbit: { yaw: 0.9, pitch: 0.62, dist: 6.2 }, drag: null, lastUi: 0 };
const PHASES = ["PRE-MATCH", "AUTO", "TRANSITION", "TELEOP", "POST", "FINAL"];

// ---- capture while you play (called from afterStep / startMatch / matchOver)
function captureStart() {
  capture.frames = []; capture.next = 0; capture.violations = [];
  capture.infos = []; for (let i = 0; i < 4; i++) { const info = state.sim.robotInfo(i); if (info && info.label) capture.infos.push(info); }
  capture.alliance = state.alliance; capture.stand = Object.assign({}, state.stand); capture.setup = Object.assign({}, state.setup);
}
function captureFrame(fr, force) {
  if (!force && fr[0] < capture.next) return;
  capture.frames.push(fr); capture.next = fr[0] + 0.046;   // ~20 a second; a frame up to 4 ms early still counts
}
function captureEnd(result) {
  const f = state.sim.frame(); if (!capture.frames.length || f[0] > capture.frames[capture.frames.length - 1][0]) captureFrame(f, true);
  capture.violations = (result && result.violations) || [];
}

// ---- timer text exactly as the rules engine formats it (MatchRules::timerText)
function timerAt(t, phase, start, tm) {
  const mt = t - start, total = tm.auto_s + tm.transition_s + tm.teleop_s;
  let shown = 0;
  if (phase === 0) shown = tm.teleop_s + tm.auto_s;
  else if (phase === 1) shown = tm.teleop_s + Math.max(0, tm.auto_s - mt);
  else if (phase === 2) shown = Math.ceil(Math.max(0, tm.auto_s + tm.transition_s - mt));
  else if (phase === 3) shown = Math.max(0, total - mt);
  const s = Math.ceil(shown - 1e-9);
  return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0");
}
const clockText = (s) => { s = Math.max(0, s); return Math.floor(s / 60) + ":" + String(Math.floor(s % 60)).padStart(2, "0"); };

// ---- events: score changes, HIVE tips, phases and fouls are marked on the timeline; the rest is a feed
function deriveEvents(frames, start, violations, fileEvents, infos) {
  const ev = [], label = (i) => (infos[i] && infos[i].label) || ("robot " + (i + 1));
  let prev = frames[0];
  const tail = (f) => 7 + f[4] * RS + f[5] * ES + f[6] * CS;
  for (let i = 1; i < frames.length; i++) {
    const f = frames[i], t = f[0];
    if (f[1] !== prev[1]) ev.push({ t, kind: "phase", mark: true, text: PHASES[f[1]] || "" });
    for (const [k, a] of [[2, "red"], [3, "blue"]]) if (f[k] > prev[k]) ev.push({ t, kind: "score", alliance: a, mark: true, text: `${a === "red" ? "Red" : "Blue"} +${f[k] - prev[k]} (${f[k]})` });
    const o = tail(f), po = tail(prev);
    for (let h = 0; h < 2; h++) {
      const was = prev[po + h], now = f[o + h], before = i > 1 ? frames[i - 2][tail(frames[i - 2]) + h] : was;
      if (Math.abs(now - was) > 1e-4 && Math.abs(was - before) < 1e-6) ev.push({ t, kind: "tip", alliance: h ? "blue" : "red", mark: true, text: `${h ? "Blue" : "Red"} HIVE tips` });
    }
    prev = f;
  }
  for (const v of violations || []) ev.push({ t: v.matchTime + start, kind: "foul", mark: true, text: `${v.penalty || "Foul"}: ${v.rule} (${v.offender})` });
  for (const e of fileEvents || []) {
    const who = e.robot >= 0 ? label(e.robot) : "";
    const text = e.kind === "launch" ? `${who} launches` : e.kind === "cell_entry" ? `${who} scores into the ${e.hive} HIVE` : e.kind === "intake" ? `${who} picks up ${e.type || "a ball"}`
      : e.kind === "flower_entry" ? `${who} fills a FLOWER` : e.kind === "flower_retrieve" ? `${who} takes from a FLOWER` : e.kind === "robot_collision" ? `${who} hits ${label(e.other)}`
      : e.kind === "fault" ? `${who}: electrical fault` : e.kind === "tip_start" ? "" : e.kind;
    if (text) ev.push({ t: e.t, kind: e.kind, mark: false, text });
  }
  ev.sort((a, b) => a.t - b.t);
  return ev;
}

// ---- start / stop
function startReplay(src) {
  if (!src.frames || src.frames.length < 2) return;
  robotSound.stop(); keys.clear();
  replay.frames = src.frames; replay.times = Float64Array.from(src.frames, (f) => f[0]);
  replay.buf = new Float32Array(src.frames[0].length); replay.cur = replay.buf;
  replay.infos = src.infos; replay.start = src.start; replay.timing = src.timing;
  replay.end = replay.times[replay.times.length - 1];
  replay.t = Math.min(replay.end, Math.max(replay.times[0], src.start - 0.5));
  replay.alliance = src.alliance || "red"; replay.stand = src.stand || { u: 0.5, v: 0.3 }; replay.title = src.title || "Replay";
  replay.returnTo = src.returnTo || "menu"; replay.events = src.events || []; replay.playing = true; replay.speed = 1; replay.camReady = false;
  replay.follow = 0;
  clearRobots(); lastFrameT = -1;
  for (const info of replay.infos) { const r = buildRobot(info); gp.add(r.g); robots.push(r); }
  state.match = "replay"; state.running = false; state.paused = false; state.overhead = false;
  ["conn", "keys", "robotHud", "touch", "hint", "viewBtns", "finalBanner"].forEach((id) => { $(id).hidden = true; });
  document.body.classList.remove("touch-on");
  $("hud").hidden = false; $("replayBar").hidden = false; $("rpTitle").textContent = replay.title;
  $("rpBot").replaceChildren(...replay.infos.map((info, i) => { const o = document.createElement("option"); o.value = String(i); o.textContent = info.label.toUpperCase(); return o; }));
  $("rpSpeed").value = "1"; $("rpCam").value = replay.cam;
  renderMarks(); ui.closeAll(); renderUpdates();
  replayRender(true);
  $("rpPlay").focus();
}
function exitReplay() {
  if (state.match !== "replay") return;
  $("replayBar").hidden = true;
  if (replay.returnTo === "report" && state.sim) {
    // back to the finished match: the live world is untouched, rebuild its robots and its final instant
    clearRobots(); lastFrameT = -1; draw(state.sim.frame());
    state.match = "over"; state.running = false;
    ["hud", "viewBtns"].forEach((id) => { $(id).hidden = false; }); $("pauseBtn").hidden = true;
    const h = state.sim.hud(); $("timer").textContent = h.timer; $("phase").textContent = h.phase; $("sRed").textContent = h.red; $("sBlue").textContent = h.blue;
    ui.show("report");
  } else toMenu();
}
function watchLastMatch() {
  if (capture.frames.length < 2) return;
  const s = capture.setup || {};
  startReplay({ frames: capture.frames, infos: capture.infos, start: 3.0, timing: season.timing, alliance: capture.alliance, stand: capture.stand,
    title: `${s.mode === "practice" ? "Practice" : "Match"} · ${capture.alliance} alliance · seed ${s.seed}`,
    events: deriveEvents(capture.frames, 3.0, capture.violations, [], capture.infos), returnTo: state.match === "over" ? "report" : "menu" });
}

// ---- interpolated frame at time t (positions and angles blended; teleports and flags snap)
const _qa = [0, 0, 0, 0];
function frameAt(t) {
  const F = replay.frames, T = replay.times, n = F.length;
  if (t <= T[0]) return F[0];
  if (t >= T[n - 1]) return F[n - 1];
  let lo = 0, hi = n - 1;
  while (hi - lo > 1) { const mid = (lo + hi) >> 1; if (T[mid] <= t) lo = mid; else hi = mid; }
  const a = F[lo], b = F[hi], u = (t - T[lo]) / Math.max(1e-9, T[hi] - T[lo]), out = replay.buf;
  out.set(a); out[0] = t;
  const L = (k) => { out[k] = a[k] + (b[k] - a[k]) * u; };
  const nR = a[4], nE = a[5], nC = a[6];
  let o = 7;
  for (let i = 0; i < nR; i++, o += RS) {
    L(o); L(o + 1);
    let dh = b[o + 2] - a[o + 2]; dh = Math.atan2(Math.sin(dh), Math.cos(dh)); out[o + 2] = a[o + 2] + dh * u;
    for (const k of [9, 10, 11, 12, 13, 14, 15, 16, 18]) L(o + k);
  }
  for (let i = 0; i < nE; i++, o += ES) {
    if (!a[o + 5] || !b[o + 5]) continue;
    const dx = b[o] - a[o], dy = b[o + 1] - a[o + 1], dz = b[o + 2] - a[o + 2];
    if (dx * dx + dy * dy + dz * dz > 0.36) continue;          // moved off the field and back: no streak
    out[o] = a[o] + dx * u; out[o + 1] = a[o + 1] + dy * u; out[o + 2] = a[o + 2] + dz * u;
    const d = a[o + 6] * b[o + 6] + a[o + 7] * b[o + 7] + a[o + 8] * b[o + 8] + a[o + 9] * b[o + 9], sg = d < 0 ? -1 : 1;
    let len = 0;
    for (let k = 0; k < 4; k++) { _qa[k] = a[o + 6 + k] + (sg * b[o + 6 + k] - a[o + 6 + k]) * u; len += _qa[k] * _qa[k]; }
    len = Math.sqrt(len) || 1; for (let k = 0; k < 4; k++) out[o + 6 + k] = _qa[k] / len;
    L(o + 10);
  }
  o += nC * CS;
  for (let k = 0; k < 10; k++) L(o + k);                         // HIVE angles, wall flex
  return out;
}

// ---- per-frame: advance the clock, draw, update the HUD and bar
function replayTick(dt) {
  if (replay.playing) {
    replay.t = Math.min(replay.end, replay.t + dt * replay.speed);
    if (replay.t >= replay.end) setPlaying(false);
  }
  replayRender(false);
}
function replayRender(force) {
  const f = frameAt(replay.t); replay.cur = f;
  draw(f);
  $("timer").textContent = timerAt(f[0], f[1], replay.start, replay.timing);
  $("phase").textContent = "Replay · " + (PHASES[f[1]] || "");
  $("sRed").textContent = f[2]; $("sBlue").textContent = f[3];
  const now = performance.now();
  if (!force && now - replay.lastUi < 90) return;                // the bar updates ~10 times a second
  replay.lastUi = now;
  const span = Math.max(1e-6, replay.end - replay.times[0]);
  if (document.activeElement !== $("rpSeek") || !replay.seeking) $("rpSeek").value = Math.round(1000 * (replay.t - replay.times[0]) / span);
  $("rpTime").textContent = `${clockText(replay.t - replay.start)} / ${clockText(replay.end - replay.start)}`;
  let latest = null;
  for (const e of replay.events) { if (e.t > replay.t) break; if (replay.t - e.t < 2.5) latest = e; }
  const txt = latest ? latest.text : "";
  if ($("rpEvent").textContent !== txt) $("rpEvent").textContent = txt;
}
function setPlaying(on) {
  if (on && replay.t >= replay.end - 1e-3) replay.t = replay.times[0];
  replay.playing = on;
  $("rpPlay").innerHTML = on ? "&#10074;&#10074;" : "&#9654;";
  $("rpPlay").setAttribute("aria-label", on ? "Pause" : "Play");
}
function seekTo(t) { replay.t = clamp(t, replay.times[0], replay.end); replayRender(true); }
function jumpEvent(dir) {
  const marks = replay.events.filter((e) => e.mark);
  const next = dir > 0 ? marks.find((e) => e.t > replay.t + 0.05) : [...marks].reverse().find((e) => e.t < replay.t - 0.6);
  if (next) seekTo(next.t - (dir > 0 ? 0.6 : 0.6));
}
function renderMarks() {
  const span = Math.max(1e-6, replay.end - replay.times[0]), box = $("rpMarks");
  box.replaceChildren(...replay.events.filter((e) => e.mark).map((e) => {
    const m = document.createElement("i");
    m.className = "mk " + e.kind + (e.alliance ? " " + e.alliance : "");
    m.style.left = (100 * (e.t - replay.times[0]) / span).toFixed(2) + "%";
    m.title = `${clockText(e.t - replay.start)} ${e.text}`;
    return m;
  }));
}

// ---- cameras: broadcast orbit, overhead, chase behind a robot, the driver station, free orbit
const _v = new THREE.Vector3(), _w = new THREE.Vector3();
function replayCamera(dt) {
  const f = replay.cur; if (!f) return;
  const o = 7 + clamp(replay.follow, 0, f[4] - 1) * RS, rx = f[o], ry = f[o + 1], rh = f[o + 2];
  let pos, tgt, fov = 55;
  if (replay.cam === "overhead") { pos = _v.set(0, 6.4, 0.01); tgt = _w.set(0, 0, 0); fov = settings.fov; }
  else if (replay.cam === "follow") { pos = T(rx - Math.cos(rh) * 1.0, ry - Math.sin(rh) * 1.0, 0.62); tgt = T(rx + Math.cos(rh) * 0.5, ry + Math.sin(rh) * 0.5, 0.12); fov = 60; }
  else if (replay.cam === "driver") {
    const A = F.allianceAreas[replay.alliance], red = replay.alliance === "red";
    const y = A.yMin + 0.15 + (A.yMax - A.yMin - 0.3) * replay.stand.u, depth = 0.25 + replay.stand.v * ((A.xMax - A.xMin) - 0.4);
    pos = T(red ? -H - depth : H + depth, y, settings.eye); tgt = T(rx * 0.85, ry * 0.85, 0.3); fov = settings.fov;
  } else if (replay.cam === "free") {
    const ob = replay.orbit;
    pos = T(Math.cos(ob.yaw) * Math.cos(ob.pitch) * ob.dist, Math.sin(ob.yaw) * Math.cos(ob.pitch) * ob.dist, 0.3 + Math.sin(ob.pitch) * ob.dist); tgt = T(0, 0, 0.3);
  } else {                                                     // broadcast: slow orbit tied to the replay clock
    const a = -2.35 + (replay.t - replay.times[0]) * 0.035;
    pos = T(Math.cos(a) * 5.3, Math.sin(a) * 5.3, 2.5); tgt = T(rx * 0.35, ry * 0.35, 0.35);
  }
  const k = replay.camReady && !(replay.cam === "free" || replay.cam === "overhead") ? 1 - Math.exp(-dt / (settings.reducedMotion ? 0.5 : 0.18)) : 1;
  if (!replay.camReady) { replay.camPos.copy(pos); replay.camTgt.copy(tgt); replay.camReady = true; }
  replay.camPos.lerp(pos, k); replay.camTgt.lerp(tgt, k);
  camera.position.copy(replay.camPos); camera.lookAt(replay.camTgt);
  setFov(effectiveFov(fov));
}
canvas.addEventListener("pointerdown", (e) => { if (state.match !== "replay" || replay.cam !== "free") return; replay.drag = [e.clientX, e.clientY]; canvas.setPointerCapture(e.pointerId); });
canvas.addEventListener("pointermove", (e) => {
  if (!replay.drag) return;
  replay.orbit.yaw -= (e.clientX - replay.drag[0]) * 0.006 * settings.lookSens;
  replay.orbit.pitch = clamp(replay.orbit.pitch + (e.clientY - replay.drag[1]) * 0.005 * settings.lookSens * (settings.invertLook ? -1 : 1), 0.05, 1.45);
  replay.drag = [e.clientX, e.clientY];
});
canvas.addEventListener("pointerup", () => { replay.drag = null; });
canvas.addEventListener("wheel", (e) => { if (state.match !== "replay" || replay.cam !== "free") return; e.preventDefault(); replay.orbit.dist = clamp(replay.orbit.dist * Math.exp(e.deltaY * 0.001), 1.2, 14); }, { passive: false });

// ---- controls
$("rpPlay").onclick = () => setPlaying(!replay.playing);
$("rpBack").onclick = () => seekTo(replay.t - 5);
$("rpFwd").onclick = () => seekTo(replay.t + 5);
$("rpPrevEv").onclick = () => jumpEvent(-1);
$("rpNextEv").onclick = () => jumpEvent(1);
$("rpExit").onclick = exitReplay;
$("rpSeek").addEventListener("input", () => { replay.seeking = true; seekTo(replay.times[0] + (replay.end - replay.times[0]) * $("rpSeek").value / 1000); });
$("rpSeek").addEventListener("change", () => { replay.seeking = false; });
$("rpSpeed").onchange = () => { replay.speed = +$("rpSpeed").value; };
$("rpCam").onchange = () => { replay.cam = $("rpCam").value; replay.camReady = false; $("rpBot").hidden = replay.cam === "free" || replay.cam === "overhead"; };
$("rpBot").onchange = () => { replay.follow = +$("rpBot").value; replay.camReady = false; };
$("rWatch").onclick = watchLastMatch;
$("rlWatch").onclick = watchLastMatch;
const SPEEDS = [0.25, 0.5, 1, 2, 4], CAMS = ["broadcast", "follow", "driver", "overhead", "free"];
function replayKey(e) {
  if (e.target && /INPUT|SELECT/.test(e.target.tagName) && e.target.type !== "range") return false;
  const k = e.code;
  if (e.target && e.target.tagName === "BUTTON" && (k === "Space" || k === "Enter")) return false;   // the focused button acts
  if (k === "Space" || k === "KeyK") setPlaying(!replay.playing);
  else if (k === "ArrowLeft") { if (e.shiftKey) jumpEvent(-1); else seekTo(replay.t - 5); }
  else if (k === "ArrowRight") { if (e.shiftKey) jumpEvent(1); else seekTo(replay.t + 5); }
  else if (k === "Comma") seekTo(replay.t - 0.05);
  else if (k === "Period") seekTo(replay.t + 0.05);
  else if (k === "BracketLeft" || k === "BracketRight") { const i = SPEEDS.indexOf(replay.speed); replay.speed = SPEEDS[clamp(i + (k === "BracketLeft" ? -1 : 1), 0, SPEEDS.length - 1)]; $("rpSpeed").value = String(replay.speed); }
  else if (k === "KeyC") { replay.cam = CAMS[(CAMS.indexOf(replay.cam) + 1) % CAMS.length]; $("rpCam").value = replay.cam; $("rpCam").dispatchEvent(new Event("change")); }
  else if (/^Digit[1-4]$/.test(k)) { const i = +k.slice(5) - 1; if (i < replay.infos.length) { $("rpBot").value = String(i); $("rpBot").dispatchEvent(new Event("change")); } }
  else return false;
  e.preventDefault(); return true;
}
function replayPad(edge) {   // gamepad: A play/pause, LB/RB -/+5 s, d-pad left/right events, Y camera, B exit
  if (edge(0)) setPlaying(!replay.playing);
  if (edge(4)) seekTo(replay.t - 5);
  if (edge(5)) seekTo(replay.t + 5);
  if (edge(14)) jumpEvent(-1);
  if (edge(15)) jumpEvent(1);
  if (edge(3)) replayKey({ code: "KeyC", preventDefault() {}, target: null });
  if (edge(1) || edge(9)) exitReplay();
}

// ---- opening replay files exported by gp-sim
async function readReplayFile(file) {
  let buf = await file.arrayBuffer();
  let u8 = new Uint8Array(buf);
  if (u8[0] === 0x1f && u8[1] === 0x8b) {
    if (typeof DecompressionStream === "undefined") throw new Error("this browser cannot unpack compressed files; open the replay in a current Chrome, Edge, Firefox or Safari");
    buf = await new Response(new Blob([buf]).stream().pipeThrough(new DecompressionStream("gzip"))).arrayBuffer();
    u8 = new Uint8Array(buf);
  }
  if (new TextDecoder().decode(u8.subarray(0, 10)) !== "GPREPLAY1\n") throw new Error("not a GamePlan replay (export one with gp-sim replay-export)");
  const hl = new DataView(buf).getUint32(10, true);
  const hdr = JSON.parse(new TextDecoder().decode(u8.subarray(14, 14 + hl)));
  if (hdr.schema !== "gameplan.replay/1") throw new Error("unsupported replay format " + hdr.schema);
  if (hdr.season && hdr.season.id && hdr.season.id !== season.id) throw new Error(`this replay is from season ${hdr.season.id}; this app shows ${season.id}`);
  const fl = hdr.frameLength | 0, all = new Float32Array(buf.slice(14 + hl));
  const n = Math.floor(all.length / fl);
  if (!fl || n < 2 || all[4] !== hdr.robots.length || fl !== 7 + all[4] * RS + all[5] * ES + all[6] * CS + 10) throw new Error("the replay's frames do not match its header");
  const frames = []; for (let i = 0; i < n; i++) frames.push(all.subarray(i * fl, (i + 1) * fl));
  return { hdr, frames };
}
async function openReplayFile(file) {
  const st = $("rpFileState");
  st.textContent = `Opening ${file.name}…`;
  try {
    const { hdr, frames } = await readReplayFile(file);
    const res = hdr.result || {}, red = res.red ? res.red.total : "?", blue = res.blue ? res.blue.total : "?";
    st.textContent = "";
    const ctl = hdr.robots.map((r) => r.controller).includes("runtime") ? "team code" : "AI";
    startReplay({ frames, infos: hdr.robots, start: hdr.matchStart || 3, timing: hdr.timing || season.timing, alliance: "red", stand: state.stand,
      title: `${file.name} · red ${red} – blue ${blue} · seed ${hdr.seed} · ${ctl}${hdr.reproduction && hdr.reproduction !== "EXACT" ? " · approximate" : ""}`,
      events: deriveEvents(frames, hdr.matchStart || 3, hdr.violations, hdr.events, hdr.robots), returnTo: "menu" });
  } catch (e) { st.textContent = "Could not open the replay: " + e.message; }
}
$("rpFile").addEventListener("change", () => { const f = $("rpFile").files[0]; if (f) openReplayFile(f); $("rpFile").value = ""; });
{
  const zone = $("rpDrop");
  zone.addEventListener("dragover", (e) => { e.preventDefault(); zone.classList.add("over"); });
  zone.addEventListener("dragleave", () => zone.classList.remove("over"));
  zone.addEventListener("drop", (e) => { e.preventDefault(); zone.classList.remove("over"); const f = e.dataTransfer.files[0]; if (f) openReplayFile(f); });
}
function renderReplaysScreen() {
  const has = capture.frames.length > 1, s = capture.setup || {};
  $("rlWatch").disabled = !has;
  if (!has) { $("rlLast").textContent = "Play a match and its replay appears here."; return; }
  const last = capture.frames[capture.frames.length - 1];
  $("rlLast").textContent = `${s.mode === "practice" ? "Practice" : "Competition"} · ${capture.alliance} alliance · seed ${s.seed} · red ${last[2]} – blue ${last[3]}`;
}
