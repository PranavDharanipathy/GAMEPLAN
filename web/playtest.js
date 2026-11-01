// Scripted "human" playtest of the browser build through the same gamepad API the page uses.
// Collects POLLEN, drives to a shooting spot facing its own HIVE, spins up, fires at several hood
// settings and reports how many balls land in its CELLS. Usage: node web/playtest.js [seed]
const fs = require("fs"), path = require("path");
const { create } = require("./gameplan-wasm.js");
const R = path.join(__dirname, "..");
(async () => {
  const sim = await create(fs.readFileSync(path.join(R, "build-web/gameplan.wasm")), () => {});
  const rd = (p) => fs.readFileSync(path.join(R, p), "utf8");
  const robot = rd("examples/robots/generic-launcher.robot.json");
  const season = JSON.parse(rd("seasons/biobuzz-2027/season.json"));
  sim.init(rd("seasons/biobuzz-2027/season.json"), rd("data/hardware/components.json"), robot, robot,
    { seed: +(process.argv[2] || 3), alliance: "red", mode: "practice", difficulty: 0.6, partnerInstruction: "do nothing" });
  const RS = 18, ES = 14, DT = 0.02;
  let t = 0, fired = 0, log = [];
  const frame = () => {
    const f = sim.frame(), nR = f[4], nE = f[5];
    const me = { x: f[7], y: f[8], h: f[9], held: f[11] };
    const els = []; let o = 7 + nR * RS;
    for (let i = 0; i < nE; i++, o += ES) els.push({ x: f[o], y: f[o + 1], z: f[o + 2], kind: f[o + 3], vis: f[o + 5] });
    return { me, els };
  };
  const wrap = (a) => Math.atan2(Math.sin(a), Math.cos(a));
  const pad = (p) => sim.setPad(Object.assign({ lx: 0, ly: 0, rx: 0, ry: 0, lt: 0, rt: 0, buttons: 0 }, p));
  // field-relative drive toward (gx, gy) while turning to heading gh (what a driver does with the sticks)
  const drive = (me, gx, gy, gh, extra = {}) => {
    const dx = gx - me.x, dy = gy - me.y, d = Math.hypot(dx, dy), sp = Math.min(1, d / 0.4) * 0.8;
    const vx = d > 0.02 ? dx / d * sp : 0, vy = d > 0.02 ? dy / d * sp : 0;
    const fwd = vx * Math.cos(me.h) + vy * Math.sin(me.h), left = -vx * Math.sin(me.h) + vy * Math.cos(me.h);
    const eh = wrap(gh - me.h);
    pad(Object.assign({ ly: -fwd, lx: -left, rx: Math.max(-0.6, Math.min(0.6, -eh * 1.5)) }, extra));
    return d;
  };
  const step = (n = 1) => { for (let i = 0; i < n; i++) { sim.step(DT); t += DT; } };
  const hive = season.field.hives[0], cellY = 0.36;   // red: audience cell up at the start
  const inCells = () => frame().els.filter((e) => e.vis && e.z > 0.95 && Math.abs(e.x - hive.pivot[0]) < 0.35).length;
  while (sim.hud().phase !== "AUTO") { pad({}); step(); }
  const start = inCells();
  // 1. collect balls until 4 held
  for (let k = 0; k < 4000 && frame().me.held < 4; k++) {
    const { me, els } = frame();
    const free = els.filter((e) => e.vis && e.kind === 0 && e.z < 0.06 && Math.abs(e.x) < 1.7 && Math.abs(e.y) < 1.7);
    if (!free.length) break;
    free.sort((a, b) => Math.hypot(a.x - me.x, a.y - me.y) - Math.hypot(b.x - me.x, b.y - me.y));
    const b = free[0], a = Math.atan2(b.y - me.y, b.x - me.x);
    drive(me, b.x, b.y, a, { rt: 1 }); step();
  }
  log.push(`collected: holding ${frame().me.held} after ${t.toFixed(1)} s`);
  const tgt = { x: hive.pivot[0], y: -cellY };   // red AUDIENCE cell (y<0) is up at the start
  // 2. shoot like a player: drive to a spot, hold RB (shooter assist), turn to face the up CELL,
  //    wait for the ready LED, press A. Repeat from different distances, refilling between shots.
  const results = [];
  const status = () => sim.frame()[7 + 17];
  const spots = (process.argv[3] || "1.2,1.5,1.8,2.1,1.0,1.6").split(",").map(Number);
  for (const dist of spots) {
    for (let k = 0; k < 3000 && frame().me.held === 0; k++) {   // refill
      const { me, els } = frame();
      const free = els.filter((e) => e.vis && e.kind === 0 && e.z < 0.06 && Math.abs(e.x) < 1.7 && Math.abs(e.y) < 1.7 && e.x < 0);
      if (!free.length) break;
      free.sort((a, b) => Math.hypot(a.x - me.x, a.y - me.y) - Math.hypot(b.x - me.x, b.y - me.y));
      drive(me, free[0].x, free[0].y, Math.atan2(free[0].y - me.y, free[0].x - me.x), { rt: 1 }); step();
    }
    if (frame().me.held === 0) break;
    const ang = Math.PI + (results.length % 3 - 1) * 0.5;   // approach from different directions on our side
    const spot = { x: Math.max(-1.6, tgt.x + dist * Math.cos(ang)), y: Math.max(-1.6, Math.min(1.6, tgt.y + dist * Math.sin(ang))) };
    let readyAt = null, st = [0, 0, 0], lastD = 0, lastE = 0;
    for (let k = 0; k < 900; k++) {
      const { me } = frame(); const gh = Math.atan2(tgt.y - me.y, tgt.x - me.x);
      const d = drive(me, spot.x, spot.y, gh, { buttons: 1 << 5 }); step();
      st[status()]++; lastD = d; lastE = Math.abs(wrap(gh - me.h));
      if (d < 0.06 && status() === 2) { readyAt = t; break; }
    }
    if (process.env.DBG) console.log("spot", JSON.stringify(spot), "status counts", st, "dist to spot", lastD.toFixed(3), "aim err", lastE.toFixed(3));
    const before = inCells(), held0 = frame().me.held;
    for (let k = 0; k < 40 && frame().me.held === held0; k++) { const { me } = frame(); drive(me, spot.x, spot.y, Math.atan2(tgt.y - me.y, tgt.x - me.x), { buttons: (1 << 5) | 1 }); step(); }
    // follow the launched ball: the one that rises above 0.5 m first
    let ballIdx = -1, best = null, launchPt = null;
    const me0 = frame().me; launchPt = { x: me0.x, y: me0.y };
    const rangeT = Math.hypot(tgt.x - me0.x, tgt.y - me0.y);
    for (let k = 0; k < 110; k++) {
      const { me, els } = frame(); drive(me, spot.x, spot.y, Math.atan2(tgt.y - me.y, tgt.x - me.x), { buttons: 1 << 5 }); step();
      const E = frame().els;
      if (ballIdx < 0) ballIdx = E.findIndex((e) => e.vis && e.z > 0.5 && e.z < 2.5 && Math.hypot(e.x - me.x, e.y - me.y) < 0.6);
      if (ballIdx >= 0) { const b = E[ballIdx]; const r = Math.hypot(b.x - launchPt.x, b.y - launchPt.y); if (!best && r >= rangeT) best = { zAtTarget: +b.z.toFixed(2), lateral: +(((b.x - tgt.x) ** 2 + (b.y - tgt.y) ** 2) ** 0.5).toFixed(2) }; }
    }
    if (process.env.DBG) console.log("range", rangeT.toFixed(2), "ball at target range:", JSON.stringify(best), "target z 1.43");
    results.push({ dist, ready: readyAt !== null, fired: held0 - frame().me.held, scored: inCells() - before });
  }
  log.push("shots: " + JSON.stringify(results));
  console.log(log.join("\n"));
})();
