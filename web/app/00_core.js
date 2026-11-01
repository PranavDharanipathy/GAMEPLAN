// ================= core: page data, shared state, persisted settings =================
const $ = (id) => document.getElementById(id);
const season = JSON.parse($("seasonJson").textContent);
const TAGS = JSON.parse($("tagJson").textContent);
const CADCOL = JSON.parse($("cadColorJson").textContent || "{}");
const BUILD = JSON.parse($("buildJson").textContent);
const F = season.field, H = F.halfSize_m;
// match: "none" (nothing simulated yet), "demo" (AI match behind the menus), "live" (you drive), "over"
const state = { alliance: "red", mode: "competition", sim: null, match: "none", running: false, paused: false, loading: false,
  overhead: false, elec: [], stand: { u: 0.5, v: 0.3 }, setup: null, fieldReady: false, simReady: false };
let wasmBytes = null;
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
// browser storage holds only this viewer's conveniences (settings, standing spot); it may be unavailable
const store = (k, v) => { try { if (v === undefined) return localStorage.getItem(k); if (v === null) localStorage.removeItem(k); else localStorage.setItem(k, v); } catch (_) { return null; } return null; };
const sstore = (k, v) => { try { if (v === undefined) return sessionStorage.getItem(k); if (v === null) sessionStorage.removeItem(k); else sessionStorage.setItem(k, v); } catch (_) { return null; } return null; };

// ---- settings: one object, saved on every change, read by every system ----
// Graphics presets set the individual graphics options; changing one option makes the preset "custom".
const PRESETS = {
  low:      { res: 0.67, shadowLights: 1, shadowMap: 512,  softShadows: false, ao: false, bloom: false, film: false, vegetation: "light" },
  balanced: { res: 0.85, shadowLights: 2, shadowMap: 1024, softShadows: false, ao: false, bloom: true,  film: true,  vegetation: "full" },
  high:     { res: 1,    shadowLights: 4, shadowMap: 1024, softShadows: true,  ao: true,  bloom: true,  film: true,  vegetation: "full" },
  ultra:    { res: 2,    shadowLights: 8, shadowMap: 2048, softShadows: true,  ao: true,  bloom: true,  film: true,  vegetation: "full" },
};
const PRESET_ORDER = ["ultra", "high", "balanced", "low"];
const GFX_KEYS = Object.keys(PRESETS.high);
const KEY_ACTIONS = [   // [id, label]; keyboard stands in for an FTC gamepad
  ["forward", "Drive forward"], ["back", "Drive backward"], ["left", "Strafe left"], ["right", "Strafe right"],
  ["turnLeft", "Turn left"], ["turnRight", "Turn right"], ["intake", "Intake"], ["reverse", "Reverse intake"],
  ["fire", "Fire (hold)"], ["hoodUp", "Hood trim up"], ["hoodDown", "Hood trim down"], ["view", "Overhead view"], ["pause", "Pause"],
];
const DEFAULTS = Object.assign({ preset: "high", toneMap: "agx", exposure: 1, autoQuality: true, fps: false }, PRESETS.high, {
  fov: 75, eye: 1.65, headFollow: 0.85, headSpeed: 0.22, lookSens: 1, invertLook: false,
  binds: { forward: ["KeyW", ""], back: ["KeyS", ""], left: ["KeyA", ""], right: ["KeyD", ""],
    turnLeft: ["ArrowLeft", "KeyQ"], turnRight: ["ArrowRight", "KeyE"], intake: ["ShiftLeft", "ShiftRight"], reverse: ["KeyC", ""],
    fire: ["Space", ""], hoodUp: ["ArrowUp", "KeyI"], hoodDown: ["ArrowDown", "KeyK"], view: ["KeyV", ""], pause: ["Escape", "KeyP"] },
  deadzone: 0.06, keyTurn: 0.7, rumble: true,
  vol: 0.8, volCues: 0.9, volRobot: 0.55, volImpacts: 0.8, mute: false,
  aimAssist: true, hints: true, keysHint: true, robotHud: true, hudScale: 1,
  reducedMotion: matchMedia("(prefers-reduced-motion: reduce)").matches, highContrast: false, uiScale: 1,
});
const SETTINGS_KEY = "gp.settings.v1";
const settings = (() => {
  const out = JSON.parse(JSON.stringify(DEFAULTS));
  let saved = null; try { saved = JSON.parse(store(SETTINGS_KEY) || "null"); } catch (_) { saved = null; }
  if (saved && typeof saved === "object") for (const k of Object.keys(DEFAULTS)) {
    if (k === "binds" && saved.binds && typeof saved.binds === "object") {
      for (const a of Object.keys(DEFAULTS.binds)) if (Array.isArray(saved.binds[a])) out.binds[a] = [String(saved.binds[a][0] || ""), String(saved.binds[a][1] || "")];
    } else if (saved[k] !== undefined && typeof saved[k] === typeof DEFAULTS[k]) out[k] = saved[k];
  }
  out.binds.pause[0] = "Escape";          // Esc always pauses / goes back
  return out;
})();
const settingsListeners = [];
const onSettings = (f) => settingsListeners.push(f);
function saveSettings() { store(SETTINGS_KEY, JSON.stringify(settings)); }
function matchingPreset() {
  for (const p of PRESET_ORDER) if (GFX_KEYS.every((k) => PRESETS[p][k] === settings[k])) return p;
  return "custom";
}
function setSetting(k, v, quiet) {
  if (k === "preset") { if (PRESETS[v]) Object.assign(settings, PRESETS[v]); settings.preset = v; }
  else { settings[k] = v; if (GFX_KEYS.includes(k)) settings.preset = matchingPreset(); }
  saveSettings();
  if (!quiet) settingsListeners.forEach((f) => f(k, v));
}
function resetSettings(keys) {
  for (const k of keys) settings[k] = JSON.parse(JSON.stringify(DEFAULTS[k]));
  if (keys.some((k) => GFX_KEYS.includes(k) || k === "preset")) settings.preset = matchingPreset();
  saveSettings(); settingsListeners.forEach((f) => f("*", null));
}
try { const sv = JSON.parse(store("gp.stand") || "null"); if (sv && sv.u >= 0 && sv.u <= 1 && sv.v >= 0 && sv.v <= 1) state.stand = sv; } catch (_) { /* default spot */ }
