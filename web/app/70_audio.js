// ================= sound: match cues, impacts, your robot's motors (synthesized; volumes in Settings > Audio) =================
const audio = {
  ctx: null, master: null, bus: {},
  start() {
    try {
      if (!this.ctx) {
        this.ctx = new (window.AudioContext || window.webkitAudioContext)();
        this.master = this.ctx.createGain(); this.master.connect(this.ctx.destination);
        for (const b of ["cues", "impacts", "robot", "ui"]) { this.bus[b] = this.ctx.createGain(); this.bus[b].connect(this.master); }
        this.apply();
      }
      this.ctx.resume();
    } catch (_) { this.ctx = null; }
  },
  apply() {
    if (!this.ctx) return;
    const t = this.ctx.currentTime;
    this.master.gain.setTargetAtTime(settings.mute ? 0 : settings.vol, t, 0.02);
    this.bus.cues.gain.setTargetAtTime(settings.volCues, t, 0.02);
    this.bus.impacts.gain.setTargetAtTime(settings.volImpacts, t, 0.02);
    this.bus.robot.gain.setTargetAtTime(settings.volRobot, t, 0.02);
    this.bus.ui.gain.setTargetAtTime(0.5, t, 0.02);
  },
};
onSettings((k) => { if (k === "*" || /^vol|^mute$/.test(k)) audio.apply(); });
function tone(freq, start, dur, type = "sine", gain = 0.18, slide = 0, bus = "cues") {
  const c = audio.ctx; if (!c) return;
  const o = c.createOscillator(), g = c.createGain(), t0 = c.currentTime + start;
  o.type = type; o.frequency.setValueAtTime(freq, t0); if (slide) o.frequency.linearRampToValueAtTime(freq + slide, t0 + dur);
  g.gain.setValueAtTime(0.0001, t0); g.gain.exponentialRampToValueAtTime(gain, t0 + 0.02); g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
  o.connect(g).connect(audio.bus[bus] || audio.master); o.start(t0); o.stop(t0 + dur + 0.05);
}
function noiseBuffer(seconds) {
  const c = audio.ctx, buf = c.createBuffer(1, Math.max(1, c.sampleRate * seconds | 0), c.sampleRate), d = buf.getChannelData(0);
  for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1;
  return buf;
}
function thud(strength) {   // polycarbonate panel in an aluminium frame taking a hit: low thump + panel rattle
  const c = audio.ctx; if (!c) return;
  tone(62, 0, 0.22, "sine", 0.35 * strength, -20, "impacts");
  const n = c.createBufferSource(), env = c.createGain(), f = c.createBiquadFilter();
  n.buffer = noiseBuffer(0.15); f.type = "bandpass"; f.frequency.value = 900; f.Q.value = 1.2;
  env.gain.setValueAtTime(0.25 * strength, c.currentTime); env.gain.exponentialRampToValueAtTime(0.0001, c.currentTime + 0.14);
  n.connect(f).connect(env).connect(audio.bus.impacts); n.start();
}
function uiClick() { if (audio.ctx) tone(880, 0, 0.05, "sine", 0.05, 0, "ui"); }
// match cues (season.audioCues timing; synthesized substitutes, not the official recordings)
const CUE_SOUND = {
  "Cavalry Charge": () => [392, 523, 659, 784, 659, 784].forEach((f, i) => tone(f, i * 0.16, i === 5 ? 0.6 : 0.14, "triangle", 0.22)),
  "Buzzer x3": () => [0, 0.45, 0.9].forEach((t) => tone(110, t, 0.35, "square", 0.12)),
  "3 Bells": () => [0, 0.5, 1.0].forEach((t) => { tone(1320, t, 1.1, "sine", 0.2); tone(2640, t, 0.6, "sine", 0.06); }),
  "Chime": () => { tone(1046, 0, 1.2, "sine", 0.2); tone(1568, 0.25, 1.2, "sine", 0.15); },
  "Train Whistle": () => { tone(587, 0, 1.0, "sawtooth", 0.08, 20); tone(740, 0, 1.0, "sawtooth", 0.07, 20); },
  "3-second Buzzer": () => tone(98, 0, 3.0, "square", 0.12),
};
let lastMatchT = -99;
function cueAt(mt) {
  for (const c of season.audioCues || []) {
    if (c.atMatchTime_s < 0 || !CUE_SOUND[c.cue]) continue;
    if (lastMatchT < c.atMatchTime_s && mt >= c.atMatchTime_s) CUE_SOUND[c.cue]();
  }
  lastMatchT = mt;
}
// your robot: drivetrain gear whine, flywheel motor hum, intake whir; pitch and level follow the simulated speeds
const robotSound = {
  nodes: null,
  start() {
    const c = audio.ctx; if (!c || this.nodes) return;
    const mk = (type) => { const o = c.createOscillator(); o.type = type; o.start(); return o; };
    const drive = mk("sawtooth"), driveLP = c.createBiquadFilter(), driveG = c.createGain();
    driveLP.type = "lowpass"; driveLP.Q.value = 3; driveG.gain.value = 0; drive.connect(driveLP).connect(driveG).connect(audio.bus.robot);
    const whine = mk("triangle"), whineG = c.createGain(); whineG.gain.value = 0; whine.connect(whineG).connect(audio.bus.robot);
    const fly = mk("sine"), fly2 = mk("triangle"), flyG = c.createGain(), fly2G = c.createGain(); flyG.gain.value = 0; fly2G.gain.value = 0;
    fly.connect(flyG).connect(audio.bus.robot); fly2.connect(fly2G).connect(audio.bus.robot);
    const nz = c.createBufferSource(); nz.buffer = noiseBuffer(2); nz.loop = true; nz.start();
    const nzF = c.createBiquadFilter(), nzG = c.createGain(); nzF.type = "bandpass"; nzF.frequency.value = 1400; nzF.Q.value = 0.8; nzG.gain.value = 0;
    nz.connect(nzF).connect(nzG).connect(audio.bus.robot);
    this.nodes = { drive, driveLP, driveG, whine, whineG, fly, fly2, flyG, fly2G, nz, nzF, nzG };
  },
  update() {
    const n = this.nodes; if (!n) return;
    const t = audio.ctx.currentTime, w = userRobot.wheel, f = userRobot.fly, it = userRobot.intake;
    const dl = Math.min(1, w / 25);
    n.drive.frequency.setTargetAtTime(38 + w * 2.4, t, 0.05); n.driveLP.frequency.setTargetAtTime(180 + w * 30, t, 0.05);
    n.driveG.gain.setTargetAtTime(0.06 * dl, t, 0.06);
    n.whine.frequency.setTargetAtTime(120 + w * 21, t, 0.05); n.whineG.gain.setTargetAtTime(0.018 * dl, t, 0.06);
    const fl = Math.min(1, f / 420);
    n.fly.frequency.setTargetAtTime(30 + f * 0.95, t, 0.08); n.fly2.frequency.setTargetAtTime(60 + f * 1.9, t, 0.08);
    n.flyG.gain.setTargetAtTime(0.035 * fl, t, 0.1); n.fly2G.gain.setTargetAtTime(0.012 * fl, t, 0.1);
    n.nzF.frequency.setTargetAtTime(900 + it * 25, t, 0.05); n.nzG.gain.setTargetAtTime(0.03 * Math.min(1, it / 40), t, 0.06);
  },
  stop() {
    const n = this.nodes; if (!n) return;
    this.nodes = null;
    const t = audio.ctx.currentTime;
    for (const g of [n.driveG, n.whineG, n.flyG, n.fly2G, n.nzG]) g.gain.setTargetAtTime(0, t, 0.05);
    setTimeout(() => { for (const o of [n.drive, n.whine, n.fly, n.fly2, n.nz]) { try { o.stop(); } catch (_) { /* already stopped */ } } }, 400);
  },
};
