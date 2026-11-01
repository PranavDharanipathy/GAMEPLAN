// ================= per-frame pose update from the simulation =================
// Frame layout (apps/web/gp_web.cpp gp_frame): header 7, robots RS each, elements ES each, cells CS each,
// then hiveAngle[2] and walls 4 x (flex, speed).
const RS = 19, ES = 14, CS = 5;
const _e = new THREE.Euler(), _piv = new THREE.Vector3(), _tmp = new THREE.Vector3(), _cam = new THREE.Vector3(), wallPrevVel = [0, 0, 0, 0];
const userRobot = { wheel: 0, fly: 0, intake: 0, held: 0, status: 0, x: 0, y: 0 };   // speeds (rad/s) for the robot sounds and HUD
let lastFrameT = -1;
function draw(fr) {
  const nR = fr[4], nE = fr[5], nC = fr[6], t = fr[0];
  const dts = lastFrameT >= 0 && t > lastFrameT ? t - lastFrameT : 0; lastFrameT = t;
  let o = 7;
  ensureRobots(nR);
  for (let i = 0; i < nR; i++, o += RS) {
    const r = robots[i];
    r.g.position.set(fr[o], fr[o + 1], 0); r.g.rotation.set(0, 0, fr[o + 2]);
    // rocking: pitch and roll about the edge the robot is tipping onto (hard stops, crashes)
    const pitch = fr[o + 16], roll = fr[o + 18];
    _q.setFromEuler(_e.set(fr[o + 5] ? 1.2 : roll, -pitch, 0, "XYZ"));
    _piv.set(pitch > 0 ? -r.L / 2 : r.L / 2, roll > 0 ? -r.W / 2 : r.W / 2, 0);
    r.body.quaternion.copy(_q); r.body.position.copy(_piv).sub(_tmp.copy(_piv).applyQuaternion(_q));
    if (r.roller) r.roller.rotation.y = -fr[o + 9];
    if (r.feeder) r.feeder.userData.wheels.forEach((w) => { w.rotation.y = fr[o + 9] * 1.25; });
    if (r.fly) r.fly.rotation.y = fr[o + 10];
    if (r.hood) r.hood.rotation.y = -(Math.PI / 2 + fr[o + 11]);   // hood end at 90 deg + launch angle around the flywheel
    r.wheels.forEach((w) => { const k = (w.x > 0 ? 0 : 2) + (w.y > 0 ? 0 : 1); w.spin.rotation.y = fr[o + 12 + k]; });
    if (i === 0) {
      if (r.prev && dts > 0) {
        const k = 1 - Math.exp(-dts / 0.08);
        userRobot.wheel += (Math.abs(fr[o + 12] - r.prev[2]) + Math.abs(fr[o + 13] - r.prev[3]) + Math.abs(fr[o + 14] - r.prev[4]) + Math.abs(fr[o + 15] - r.prev[5])) / 4 / dts * k - userRobot.wheel * k;
        userRobot.fly += Math.abs(fr[o + 10] - r.prev[0]) / dts * k - userRobot.fly * k;
        userRobot.intake += Math.abs(fr[o + 9] - r.prev[1]) / dts * k - userRobot.intake * k;
      }
      r.prev = [fr[o + 10], fr[o + 9], fr[o + 12], fr[o + 13], fr[o + 14], fr[o + 15]];
      userRobot.held = fr[o + 4]; userRobot.status = fr[o + 17]; userRobot.x = fr[o]; userRobot.y = fr[o + 1];
    }
    const st = fr[o + 17];
    if (st !== r.status) {
      r.led.material.emissive.setHex(st === 2 ? 0x22ff55 : st === 1 ? 0xffa000 : 0x000000);
      if (st === 2 && i === 0 && state.running) rumble(0.12, 0.6);   // the driver feels "ready" through the controller
      r.status = st;
    }
  }
  const cnt = { 0: 0, 1: 0, 2: 0 }, cntFar = { 0: 0, 1: 0, 2: 0 };
  _cam.set(camera.position.x, -camera.position.z, camera.position.y);   // camera in the field frame
  const lodDist2 = settings.preset === "ultra" ? 16 : settings.preset === "low" ? 1 : settings.preset === "balanced" ? 2.25 : 6.25;
  for (let i = 0; i < nE; i++, o += ES) {
    if (!fr[o + 5]) continue;
    const k = fr[o + 3];
    const dx = fr[o] - _cam.x, dy = fr[o + 1] - _cam.y, dzz = fr[o + 2] - _cam.z;
    const far = dx * dx + dy * dy + dzz * dzz > lodDist2;
    const im = far ? ballIMfar[k] : ballIM[k]; if (!im) continue;
    im.setMatrixAt(far ? cntFar[k]++ : cnt[k]++, ballMatrix(fr, o, im.userData.unitR));
  }
  [0, 1, 2].forEach((k) => {
    if (ballIM[k]) { ballIM[k].count = cnt[k]; ballIM[k].instanceMatrix.needsUpdate = true; }
    if (ballIMfar[k]) { ballIMfar[k].count = cntFar[k]; ballIMfar[k].instanceMatrix.needsUpdate = true; }
  });
  o += nC * CS;
  for (let h = 0; h < 2; h++) if (hives[h]) hives[h].rotation.x = fr[o + h] - hiveCadAngle[h];
  o += 2;
  // perimeter walls flex outward under impacts and ring down; a hard hit makes a thud
  for (let w = 0; w < 4; w++) {
    const disp = fr[o + 2 * w], vel = fr[o + 2 * w + 1], sgn = w % 2 ? 1 : -1;
    if (walls[w]) walls[w].position.set(w < 2 ? sgn * disp : 0, w >= 2 ? sgn * disp : 0, 0);
    if (Math.abs(vel) > 0.04 && Math.abs(wallPrevVel[w]) <= 0.04 && state.match === "live") thud(Math.min(1, Math.abs(vel) / 0.35));
    wallPrevVel[w] = vel;
  }
}

// ================= cameras =================
// Driver: a person standing in the alliance area (spot picked on the top view before the match). Like a
// real driver's head, the view turns to follow the robot and tips down to keep it in sight when it comes
// close to the wall. A drag looks somewhere else for a moment; the view then drifts back.
// Menus: a slow orbit around the field while the AI match plays.
const look = { yaw: 0, pitch: -0.32, drag: null, offYaw: 0, offPitch: 0, ready: false };
const eyeGP = new THREE.Vector3();
function standPoint() {
  const A = F.allianceAreas[state.alliance], red = state.alliance === "red";
  const y = A.yMin + 0.15 + (A.yMax - A.yMin - 0.3) * state.stand.u;
  const depth = 0.25 + state.stand.v * ((A.xMax - A.xMin) - 0.4);   // from the field wall back into the area
  return { x: red ? -H - depth : H + depth, y };
}
// The FOV setting is the vertical angle on a 16:9 screen; narrower screens (portrait phones) keep its
// horizontal view instead of cropping the sides away.
function effectiveFov(base) {
  const a = camera.aspect || 16 / 9;
  if (a >= 16 / 9) return base;
  const h = 2 * Math.atan(Math.tan(base * Math.PI / 360) * 16 / 9);
  return Math.min(118, 2 * Math.atan(Math.tan(h / 2) / a) * 180 / Math.PI);
}
function setFov(f) { if (Math.abs(camera.fov - f) > 0.01) { camera.fov = f; camera.updateProjectionMatrix(); } }
function placeDriver() {
  const sp = standPoint();
  eyeGP.set(sp.x, sp.y, settings.eye);
  look.ready = false;
  setFov(effectiveFov(settings.fov));
}
onSettings((k) => { if (k === "*" || k === "fov" || k === "eye") placeDriver(); });
function headTarget() {   // where the driver looks: their robot, with a little of the field beyond it
  const f = state.sim && state.match !== "none" ? state.sim.frame() : null;
  const rx = f ? f[7] : 0, ry = f ? f[8] : 0, k = settings.headFollow;
  return { x: rx * 0.85 * k, y: ry * 0.85 * k + (1 - k) * 0, z: 0.3 };
}
let attractT = 0;
function updateCamera(dt) {
  if (state.match === "demo" || state.match === "none") {   // menus: slow orbit, or still if motion is reduced
    attractT += settings.reducedMotion ? 0 : dt;
    const a = 2.35 + attractT * 0.045, R = 5.4, h = 2.3 + 0.25 * Math.sin(attractT * 0.11);
    camera.position.set(Math.cos(a) * R, h, Math.sin(a) * R); camera.lookAt(0, 0.35, 0);
    setFov(effectiveFov(50));
    look.ready = false;
    return;
  }
  setFov(effectiveFov(settings.fov));
  if (state.overhead) { camera.position.set(0, 6.4, 0.01); camera.lookAt(0, 0, 0); return; }
  camera.position.copy(T(eyeGP.x, eyeGP.y, eyeGP.z));
  const tgt = headTarget(), dx = tgt.x - eyeGP.x, dy = tgt.y - eyeGP.y, dz = tgt.z - eyeGP.z;
  const wantPitch = clamp(Math.atan2(dz, Math.hypot(dx, dy)), -1.3, 0.2);
  const wy = Math.atan2(-dx, dy);   // three.js yaw (camera looks along -Z) for the field direction (dx, dy)
  if (!look.ready) { look.yaw = wy; look.pitch = wantPitch; look.ready = true; }
  const tau = settings.reducedMotion ? Math.max(0.35, settings.headSpeed) : settings.headSpeed;
  const k = 1 - Math.exp(-dt / Math.max(0.03, tau));          // human head: ~0.2 s to settle on the robot
  let dyaw = wy - look.yaw; dyaw = Math.atan2(Math.sin(dyaw), Math.cos(dyaw));
  look.yaw += dyaw * k; look.pitch += (wantPitch - look.pitch) * k;
  if (!look.drag) { const kb = 1 - Math.exp(-dt / 1.2); look.offYaw -= look.offYaw * kb; look.offPitch -= look.offPitch * kb; }
  camera.rotation.set(clamp(look.pitch + look.offPitch, -1.45, 0.5), look.yaw + look.offYaw, 0, "YXZ");
}
canvas.addEventListener("pointerdown", (e) => { if (!state.running) return; look.drag = [e.clientX, e.clientY]; canvas.setPointerCapture(e.pointerId); });
canvas.addEventListener("pointermove", (e) => {
  if (!look.drag) return;
  const s = 0.004 * settings.lookSens, inv = settings.invertLook ? -1 : 1;
  look.offYaw -= (e.clientX - look.drag[0]) * s; look.offPitch -= (e.clientY - look.drag[1]) * s * inv; look.drag = [e.clientX, e.clientY];
});
canvas.addEventListener("pointerup", () => { look.drag = null; });
function toggleOverhead() {
  state.overhead = !state.overhead;
  $("viewBtn").textContent = state.overhead ? "Driver view" : "Overhead";
  if (!state.overhead) placeDriver();
}

// ================= pre-match: pick where you stand on a top view of the field =================
function drawStandMap() {
  const cv = $("standMap"); if (!cv || !cv.clientWidth) return;
  const dpr = Math.min(2, devicePixelRatio || 1), W = cv.clientWidth, Hh = cv.clientHeight;
  cv.width = W * dpr; cv.height = Hh * dpr;
  const g = cv.getContext("2d"); g.setTransform(dpr, 0, 0, dpr, 0, 0);
  const css = getComputedStyle(document.documentElement), col = (n) => css.getPropertyValue(n).trim();
  const span = 3.2004 + 0.15, sc = Math.min(W / (2 * span), Hh / (2 * 1.95));
  const X = (x) => W / 2 + x * sc, Y = (y) => Hh / 2 - y * sc;
  g.clearRect(0, 0, W, Hh);
  for (const a of ["red", "blue"]) {
    const A = F.allianceAreas[a]; g.fillStyle = a === "red" ? "rgba(224,69,58,.18)" : "rgba(59,130,230,.18)";
    g.fillRect(X(A.xMin), Y(A.yMax), (A.xMax - A.xMin) * sc, (A.yMax - A.yMin) * sc);
    g.strokeStyle = a === "red" ? col("--red") : col("--blue"); g.lineWidth = a === state.alliance ? 2 : 1;
    g.strokeRect(X(A.xMin), Y(A.yMax), (A.xMax - A.xMin) * sc, (A.yMax - A.yMin) * sc);
  }
  g.fillStyle = "#4b4e52"; g.fillRect(X(-H), Y(H), 2 * H * sc, 2 * H * sc);
  g.strokeStyle = "rgba(0,0,0,.35)"; g.lineWidth = 1;
  for (let i = 1; i < 6; i++) { const v = -H + i * 2 * H / 6; g.beginPath(); g.moveTo(X(v), Y(H)); g.lineTo(X(v), Y(-H)); g.moveTo(X(-H), Y(v)); g.lineTo(X(H), Y(v)); g.stroke(); }
  g.strokeStyle = "#c9ced3"; g.lineWidth = 2; g.strokeRect(X(-H), Y(H), 2 * H * sc, 2 * H * sc);
  for (const [a, c] of [["red", col("--red")], ["blue", col("--blue")]]) {
    const z = F.loadingZones[a]; g.strokeStyle = c; g.lineWidth = 1.5; g.strokeRect(X(z.xMin), Y(z.yMax), (z.xMax - z.xMin) * sc, (z.yMax - z.yMin) * sc);
    const gd = F.gardens[a]; g.fillStyle = c; g.fillRect(X(gd.xMin), Y(gd.yMax), (gd.xMax - gd.xMin) * sc, Math.max(2, (gd.yMax - gd.yMin) * sc));
  }
  g.fillStyle = "#d6d9dc"; g.fillRect(X(-0.63), Y(0.5), 1.26 * sc, 1.0 * sc);   // HIVE frame footprint
  for (const [h, c] of [[0, col("--red")], [1, col("--blue")]]) { g.fillStyle = c; g.fillRect(X(F.hives[h].pivot[0] - 0.12), Y(0.45), 0.24 * sc, 0.9 * sc); }
  g.fillStyle = "#6aa84f"; for (const f of F.flowers) { g.beginPath(); g.arc(X(f.axis[0]), Y(f.axis[1]), 0.08 * sc, 0, 7); g.fill(); }
  g.fillStyle = col("--muted"); g.font = "11px IBM Plex Mono, monospace"; g.textAlign = "left";
  g.fillText("↓ audience", 8, Hh - 8);
  const sp = standPoint();
  g.beginPath(); g.arc(X(sp.x), Y(sp.y), 7, 0, 7); g.fillStyle = col("--pollen"); g.fill();
  g.lineWidth = 2; g.strokeStyle = col("--bg"); g.stroke();
}
function pickStand(e) {
  const cv = $("standMap"), r = cv.getBoundingClientRect(), W = r.width, Hh = r.height;
  const span = 3.2004 + 0.15, sc = Math.min(W / (2 * span), Hh / (2 * 1.95));
  const x = (e.clientX - r.left - W / 2) / sc, y = -(e.clientY - r.top - Hh / 2) / sc;
  const A = F.allianceAreas[state.alliance], red = state.alliance === "red";
  const depth = red ? -H - x : x - H;
  state.stand = { u: clamp((y - A.yMin - 0.15) / (A.yMax - A.yMin - 0.3), 0, 1), v: clamp((depth - 0.25) / ((A.xMax - A.xMin) - 0.4), 0, 1) };
  store("gp.stand", JSON.stringify(state.stand));
  drawStandMap(); placeDriver();
}
{
  const cv = $("standMap"); let down = false;
  cv.addEventListener("pointerdown", (e) => { down = true; cv.setPointerCapture(e.pointerId); pickStand(e); });
  cv.addEventListener("pointermove", (e) => { if (down) pickStand(e); });
  cv.addEventListener("pointerup", () => { down = false; });
  cv.addEventListener("keydown", (e) => {   // keyboard / gamepad d-pad: arrows move the spot
    const d = 0.05;
    if (e.key === "ArrowUp") state.stand.u = Math.min(1, state.stand.u + d); else if (e.key === "ArrowDown") state.stand.u = Math.max(0, state.stand.u - d);
    else if (e.key === "ArrowLeft" || e.key === "ArrowRight") { const out = (e.key === "ArrowLeft") === (state.alliance === "red"); state.stand.v = clamp(state.stand.v + (out ? d : -d), 0, 1); } else return;
    e.preventDefault(); e.stopPropagation(); store("gp.stand", JSON.stringify(state.stand)); drawStandMap(); placeDriver();
  });
  addEventListener("resize", drawStandMap);
}
