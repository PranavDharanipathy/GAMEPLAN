// Node smoke test of the browser build: full match through the WASI shim.
const fs = require("fs"), path = require("path");
const { create } = require("./gameplan-wasm.js");
const R = path.join(__dirname, "..");
(async () => {
  const sim = await create(fs.readFileSync(path.join(R, "build-web/gameplan.wasm")));
  const rd = (p) => fs.readFileSync(path.join(R, p), "utf8");
  const robot = rd("examples/robots/generic-launcher.robot.json");
  const n = sim.init(rd("seasons/biobuzz-2027/season.json"), rd("data/hardware/components.json"), robot, robot,
    { seed: 4, alliance: "red", mode: "competition", difficulty: 0.7, partnerInstruction: "Prioritize collecting POLLEN" });
  const t0 = Date.now();
  let done = false, steps = 0;
  while (!done && steps < 4000) {
    const h = sim.hud();
    // scripted driver: in TELEOP drive forward then spin up and feed
    const tele = h.phase === "TELEOP";
    sim.setPad({ lx: 0, ly: tele ? -0.5 : 0, rx: 0.1, ry: 0, lt: 0, rt: tele ? 1 : 0, buttons: tele ? (1 << 5) | 1 : 0 });
    done = sim.step(0.05); steps++;
  }
  const r = sim.result();
  console.log("robots", n, "steps", steps, "wall ms", Date.now() - t0);
  console.log("RED", r.red.total, "BLUE", r.blue.total, "tips", r.red.tips, r.blue.tips, "violations", r.violations.length);
  console.log("user robot distance", r.distance_m.toFixed(1), "m, min bus", r.userRobotElectrical.minHubBusV.toFixed(2), "V");
  console.log("partner directives", r.partnerDirectives.matched);
  console.log("frame length", sim.frame().length);
})();
