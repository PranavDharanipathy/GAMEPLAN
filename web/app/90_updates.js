// ================= updates: a bar when a newer version is out, plus About > Check for updates =================
// The live "latest release" record is kept in the artifact database (outside page versions: an open
// page's own files always belong to the version it loaded). The bar only appears in the menus and
// after the final buzzer - never over a running match.
const upd = { latest: null, db: null, ref: null, dismissed: null, tried: null };
const semver = (v) => String(v || "0").split(".").map((x) => parseInt(x, 10) || 0);
const newer = (a, b) => { const x = semver(a), y = semver(b); for (let i = 0; i < 3; i++) if ((x[i] || 0) !== (y[i] || 0)) return (x[i] || 0) > (y[i] || 0); return false; };
upd.dismissed = store("gp.update.dismissed");
upd.tried = sstore("gp.update.target");
$("aboutVer").textContent = BUILD.version;
$("aboutBuilt").textContent = [BUILD.built, BUILD.commit].filter(Boolean).join(" · ");
$("aboutSeason").textContent = `${season.name || "BIOBUZZ"} · package ${season.packageVersion || ""}`;
$("aboutRules").textContent = `${season.rulesEngineVersion} · ${season.manual.teamUpdate}`;
$("menuVer").textContent = "GamePlan " + BUILD.version;
$("setupVer").textContent = season.manual.teamUpdate + " rules";
$("aboutNew").replaceChildren(...(BUILD.notes && BUILD.notes.length ? BUILD.notes : ["See RELEASES.json in the GamePlan repository."]).map((n) => { const li = document.createElement("li"); li.textContent = n; return li; }));
function updateAvailable() { return upd.latest && newer(upd.latest.version, BUILD.version); }
function renderUpdates() {
  const avail = updateAvailable(), L = upd.latest;
  $("aboutState").classList.toggle("new", !!avail);
  $("aboutState").textContent = !upd.db ? "Update checks need you to be signed in to claude.ai"
    : !L ? "Checking for updates…" : avail ? `Version ${L.version} is available` : "You have the latest version";
  $("aboutUpdate").hidden = !avail;
  const ul = $("aboutNotes"); ul.hidden = !avail || !(L.notes || []).length;
  ul.replaceChildren(...(avail ? L.notes || [] : []).map((n) => { const li = document.createElement("li"); li.textContent = n; return li; }));
  $("menuVer").textContent = "GamePlan " + BUILD.version + (avail ? ` · ${L.version} available` : "");
  // the bar: right time (not while driving), right reason (newer and not dismissed, or required)
  const stillOld = avail && upd.tried === L.version;   // an update was started but this view is still the old version
  const show = !!avail && !state.running && (L.required || upd.dismissed !== L.version || stillOld);
  $("updBar").hidden = !show; document.body.classList.toggle("has-updbar", show);
  if (!show) return;
  $("updX").hidden = !!L.required;
  const msg = $("updMsg"); msg.replaceChildren();
  const b = document.createElement("b"); b.textContent = stillOld ? "Almost there" : (L.required ? "Update required" : "Update available");
  const sp = document.createElement("span");
  sp.textContent = stillOld ? ` · this window still shows ${BUILD.version}. Close and reopen GamePlan to finish updating to ${L.version}.`
    : ` · GamePlan ${L.version}${L.notes && L.notes[0] ? " — " + L.notes[0] : ""}`;
  msg.append(b, sp);
  $("updNow").textContent = stillOld ? "Try again" : "Update now";
}
function applyUpdate() {
  if (!updateAvailable() || state.running) return;      // never interrupts a match
  sstore("gp.update.target", upd.latest.version);
  store("gp.update.dismissed", null);
  // Opening the GamePlan link always serves the newest version: reload this view onto it.
  $("updNow").disabled = $("aboutUpdate").disabled = true; $("updNow").textContent = "Updating…";
  // a sandboxed frame may silently refuse top navigation, so a frame reload follows if we are still here
  if (upd.latest.webUrl) { try { window.top.location.href = upd.latest.webUrl; } catch (_) { /* refused */ } }
  setTimeout(() => location.reload(), 900);
}
$("updNow").onclick = applyUpdate;
$("aboutUpdate").onclick = applyUpdate;
$("updX").onclick = () => { if (!upd.latest) return; upd.dismissed = upd.latest.version; store("gp.update.dismissed", upd.dismissed); renderUpdates(); };
$("aboutCheck").onclick = async () => {
  if (!upd.ref) { renderUpdates(); return; }
  $("aboutState").textContent = "Checking for updates…";
  try { const snap = await upd.ref.get(); if (snap.exists) upd.latest = snap.data(); } catch (_) { /* keep the last known state */ }
  // an explicit check brings the bar back even if this version was dismissed earlier
  if (updateAvailable() && upd.dismissed === upd.latest.version) { upd.dismissed = null; store("gp.update.dismissed", null); }
  renderUpdates();
};
if (upd.tried && !newer(upd.tried, BUILD.version)) sstore("gp.update.target", null);   // the update landed
(async () => {
  let db = null;
  try { db = window.claude && window.claude.use ? await window.claude.use("db") : null; } catch (_) { db = null; }
  if (!db) { renderUpdates(); return; }
  upd.db = db; upd.ref = db.doc("releases/latest");
  upd.ref.onSnapshot((snap) => { upd.latest = snap.exists ? snap.data() : null; renderUpdates(); }, () => renderUpdates());
  renderUpdates();
})();
window.__gpUpdate = { simulate: (rec) => { upd.db = upd.db || {}; upd.latest = rec; renderUpdates(); }, render: renderUpdates, build: BUILD };
