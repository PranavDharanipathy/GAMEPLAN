// ================= start-up and the frame loop =================
function frame(now) {
  requestAnimationFrame(frame);
  if (window.__gpPause) { last = now; return; }   // test hook: frozen for screenshots
  const dt = Math.min(0.1, (now - last) / 1000); last = now;
  if (state.sim && !state.paused && !state.loading && (state.match === "live" || state.match === "demo")) {
    state.sim.setPad(state.match === "live" ? readPad() : NO_PAD);
    afterStep(state.sim.step(dt));
  }
  if (state.match === "replay") replayTick(dt);
  pollPad(dt);
  updateCamera(dt);
  resize();
  filmPass.uniforms.uTime.value = now / 1000;
  composer.render();
  if (state.running && !state.paused) { perf.sample(dt); if (settings.fps) fpsMeter.sample(dt); }
}
const boot = { wasm: false, field: false };
function bootProgress() {
  const parts = (boot.wasm ? 1 : 0) + load.done, total = 1 + load.total;
  $("bootBar").style.width = Math.round(100 * parts / total) + "%";
  $("bootMsg").textContent = !boot.wasm ? "Loading the simulation…" : !boot.field ? `Loading the official field CAD (${load.done}/${load.total})…` : "Ready";
  $("menuLoad").textContent = boot.field ? "" : `Loading field ${load.done}/${load.total}`;
  // the menu is usable as soon as the simulation runs; the field finishes loading behind it
  if (boot.wasm && (boot.field || load.done >= 2)) { $("boot").classList.add("gone"); setTimeout(() => { $("boot").hidden = true; }, 600); }
}
load.onProgress = bootProgress;
settingsListeners.forEach((f) => f("*", null));   // apply saved settings to every system
applyGraphics(); placeDriver(); renderKeysHint();
ui.show("menu");
bootProgress();
fetch("gameplan.wasm").then((r) => { if (!r.ok) throw new Error("HTTP " + r.status); return r.arrayBuffer(); })
  .then(async (b) => {
    wasmBytes = b;
    state.sim = await GamePlanWasm.create(b, (l) => console.log(l));   // parses partner instructions before the first match
    state.simReady = true; boot.wasm = true;
    $("loadState").textContent = "Ready"; $("startBtn").disabled = false; $("mQuick").disabled = false; $("mPlay").disabled = false;
    showParse(); bootProgress();
    startDemo();
  })
  .catch((e) => { $("bootMsg").textContent = "Could not load the simulation (" + e.message + "). Reload the page to retry."; $("loadState").textContent = $("bootMsg").textContent; });
$("mQuick").disabled = true;
loadField().then((fails) => {
  boot.field = true; bootProgress();
  $("assetState").textContent = fails.length ? "Some field CAD could not be loaded (" + fails.join(", ") + "); simplified models are shown for them." : "";
});
requestAnimationFrame(frame);
// test hooks for automated checks and screenshots (no effect on play)
window.__gpPoll = (dt) => pollPad(dt);
window.__gp = { THREE, scene, camera, renderer, state, settings, setSetting, ui, draw, placeDriver, look, robots, startMatch, pauseMatch, resumeMatch, toMenu,
  replay, capture, watchLastMatch, openReplayFile, exitReplay, seekTo, replayTick,
  ready: () => boot.wasm && boot.field && state.match !== "none",
  renderFrame: (noLook) => { if (!noLook) updateCamera(1 / 60); resize(); composer.render(); },
  advance: (s) => { for (let t = 0; t < s && (state.running || state.match === "demo"); t += 0.05) { state.sim.setPad(state.running ? readPad() : NO_PAD); afterStep(state.sim.step(0.05)); } } };
