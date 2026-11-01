// ================= AprilTag stickers (real tag36h11 codes for IDs 30-45, SDK 12 cluster layout) =================
function tagStickerTexture(ids) {
  const IN = 0.0254, W = 17.0 * IN, Hh = 4.1 * IN, px = 1400;
  const c = document.createElement("canvas"); c.width = px; c.height = Math.round(px * Hh / W);
  const g = c.getContext("2d"); g.fillStyle = "#f7f7f5"; g.fillRect(0, 0, c.width, c.height);
  const s = px / W, size = 3.25 * IN * s, cell = size / 10;
  [-6.5, -2.75, 2.75, 6.5].forEach((xin, k) => {
    const code = TAGS[ids[k]]; if (!code) return;
    const x0 = (W / 2 + xin * IN) * s - size / 2, y0 = c.height / 2 - size / 2;
    for (let r = 0; r < 10; r++) for (let q = 0; q < 10; q++) { g.fillStyle = code[r * 10 + q] === "1" ? "#f7f7f5" : "#111"; g.fillRect(x0 + q * cell, y0 + r * cell, cell + 0.6, cell + 0.6); }
    g.fillStyle = "#555"; g.font = `${Math.round(cell * 0.9)}px IBM Plex Mono, monospace`; g.fillText(String(ids[k]), x0, y0 + size + cell * 1.1);
  });
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; t.anisotropy = 8; return { tex: t, W, Hh };
}
function applySticker(mesh, ids, pivot) {
  const pos = mesh.geometry.attributes.position, n = pos.count;
  const c = new THREE.Vector3(); for (let i = 0; i < n; i++) c.add(new THREE.Vector3().fromBufferAttribute(pos, i)); c.multiplyScalar(1 / n);
  // plane normal by power iteration on the covariance (smallest axis = largest of the inverse proxy)
  const cov = [[0, 0, 0], [0, 0, 0], [0, 0, 0]], v = new THREE.Vector3();
  for (let i = 0; i < n; i++) { v.fromBufferAttribute(pos, i).sub(c); const a = [v.x, v.y, v.z]; for (let r = 0; r < 3; r++) for (let q = 0; q < 3; q++) cov[r][q] += a[r] * a[q]; }
  const mul = (m, x) => new THREE.Vector3(m[0][0] * x.x + m[0][1] * x.y + m[0][2] * x.z, m[1][0] * x.x + m[1][1] * x.y + m[1][2] * x.z, m[2][0] * x.x + m[2][1] * x.y + m[2][2] * x.z);
  let e1 = new THREE.Vector3(1, 0.3, 0.2); for (let k = 0; k < 40; k++) e1 = mul(cov, e1).normalize();
  const tr = cov[0][0] + cov[1][1] + cov[2][2];
  const shifted = cov.map((row, r) => row.map((x, q) => (r === q ? tr : 0) - x));
  // power iteration on (tr I - cov), kept orthogonal to e1, converges to the smallest-variance axis: the normal
  let nrm = new THREE.Vector3(0.2, 0.3, 1); for (let k = 0; k < 60; k++) { nrm = mul(shifted, nrm); nrm.sub(e1.clone().multiplyScalar(nrm.dot(e1))).normalize(); }
  if (nrm.dot(c.clone().sub(pivot)) < 0) nrm.negate();   // outward, away from the HIVE body
  const up = new THREE.Vector3(0, 0, 1), yUp = up.clone().sub(nrm.clone().multiplyScalar(up.dot(nrm))).normalize();
  const xr = yUp.clone().cross(nrm).normalize();          // right, as seen facing the tags
  const { tex, W, Hh } = tagStickerTexture(ids);
  const uv = new Float32Array(n * 2);
  for (let i = 0; i < n; i++) { v.fromBufferAttribute(pos, i).sub(c); uv[2 * i] = 0.5 + v.dot(xr) / W; uv[2 * i + 1] = 0.5 + v.dot(yUp) / Hh; }
  mesh.geometry.setAttribute("uv", new THREE.BufferAttribute(uv, 2));
  mesh.material = std({ map: tex, roughness: 0.5 });
  mesh.userData.keep = true;
}

// ================= field from the official CAD =================
const loader = new GLTFLoader();
const hives = [null, null];
const hiveCadAngle = [0, F.hives[1].initialUpCell.includes("scoring") ? F.hives[1].stateAngle_rad : 0];   // CAD pose: red audience up, blue scoring up
const stickerIds = { "red goal april tag (audience)": [34, 35, 36, 37], "red goal april tag (scoring)": [30, 31, 32, 33],
  "blue goal april tag (audience)": [38, 39, 40, 41], "blue goal april tag (scoring)": [42, 43, 44, 45] };
// The viewer's security policy forbids fetching data: URIs, so each CAD file ships as a GLB in base64
// inside a .json file (a relative fetch, allowed) and is parsed in memory.
async function fetchGLB(name) {
  const r = await fetch(`assets/field/${name}.glb.json`);
  if (!r.ok) throw new Error(name + ": HTTP " + r.status);
  const b64 = (await r.json()).glb, bin = atob(b64), u8 = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) u8[i] = bin.charCodeAt(i);
  return new Promise((res, rej) => loader.parse(u8.buffer, "", res, rej));
}
// One draw call per material per container instead of one per CAD part (the field has ~870 parts).
// Containers that move (each perimeter wall, each HIVE body) are merged separately.
function mergeStatic(container) {
  container.updateMatrixWorld(true);
  const inv = new THREE.Matrix4().copy(container.matrixWorld).invert(), buckets = new Map();
  container.traverse((o) => { if (o.isMesh && !o.userData.keep) { const k = o.material.uuid; if (!buckets.has(k)) buckets.set(k, []); buckets.get(k).push(o); } });
  for (const list of buckets.values()) {
    if (list.length < 2) continue;
    const geos = list.map((o) => o.geometry.clone().applyMatrix4(new THREE.Matrix4().multiplyMatrices(inv, o.matrixWorld)));
    let merged = null;
    try { merged = mergeGeometries(geos, false); } catch (_) { merged = null; }
    geos.forEach((g) => g.dispose());
    if (!merged) continue;
    const m = new THREE.Mesh(merged, list[0].material);
    m.castShadow = list[0].castShadow; m.receiveShadow = true; m.userData.merged = list.length;
    container.add(m);
    for (const o of list) { o.parent.remove(o); o.geometry.dispose(); }
  }
}
const assetFail = [];
const load = { done: 0, total: 11, onProgress: null };
const tick = () => { load.done++; if (load.onProgress) load.onProgress(); };
function loadGroup(group, parent) {
  return fetchGLB(group).then((g) => {
    g.scene.traverse((o) => {
      if (!o.isMesh) return;
      const path = (o.userData && o.userData.name) || o.name || "";
      o.material = materialFor(path, group);
      o.castShadow = group !== "tape" && !o.material.userData.noShadow && !o.material.userData.tiny;   // fasteners: negligible shadows
      o.receiveShadow = true;
      const lk = partName(path).toLowerCase().replace(/^am-5888-(red|blue)[12]: /, "");
      if (stickerIds[lk]) { const h = group === "hive_red" ? 0 : 1; const pv = F.hives[h].pivot; applySticker(o, stickerIds[lk], new THREE.Vector3(pv[0], pv[1], pv[2])); }
    });
    parent.add(g.scene);
    if (group === "perimeter") splitWalls(g.scene).forEach(mergeStatic);
    else mergeStatic(g.scene);
    tick();
    return true;
  }).catch((e) => { assetFail.push(group); console.warn("CAD asset failed:", e); tick(); return false; });
}
const ballGeo = { pollen: null, nectar: null, pollen_lod1: null, nectar_lod1: null };
async function loadField() {
  const groups = ["perimeter", "tape", "frame", "flowers", "trays"];
  buildTiles();
  const ok = await Promise.all(groups.map((g) => loadGroup(g, gp)));
  for (let h = 0; h < 2; h++) {
    const pv = F.hives[h].pivot, piv = new THREE.Group(), inner = new THREE.Group();
    piv.position.set(pv[0], pv[1], pv[2]); inner.position.set(-pv[0], -pv[1], -pv[2]);
    piv.add(inner); gp.add(piv); hives[h] = piv;
    ok.push(await loadGroup(h ? "hive_blue" : "hive_red", inner));
  }
  const geo = (name) => fetchGLB(name).then((g) => { let m = null; g.scene.traverse((o) => { if (o.isMesh && !m) m = o.geometry; }); tick(); return m; }).catch(() => { assetFail.push(name); tick(); return null; });
  ballGeo.pollen = await geo("pollen"); ballGeo.nectar = await geo("nectar");
  ballGeo.pollen_lod1 = await geo("pollen_lod1"); ballGeo.nectar_lod1 = await geo("nectar_lod1");
  buildBalls();
  if (assetFail.length) fallbackField(assetFail);
  applyGraphics();
  state.fieldReady = true;
  return assetFail.slice();
}
// FTC soft tiles: 6 x 6 interlocking 24 in EVA foam tiles (the CAD solids are too fine to decimate well)
function buildTiles() {
  const px = 2048, n = 6, s = px / n;
  const c = document.createElement("canvas"); c.width = c.height = px; const g = c.getContext("2d");
  let seed = 11; const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
  for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) { const v = 96 + Math.round((rnd() - 0.5) * 7); g.fillStyle = `rgb(${v},${v + 2},${v + 4})`; g.fillRect(i * s, j * s, s, s); }
  const img = g.getImageData(0, 0, px, px);   // foam grain
  for (let k = 0; k < img.data.length; k += 4) { const d = (rnd() - 0.5) * 9; img.data[k] += d; img.data[k + 1] += d; img.data[k + 2] += d; }
  g.putImageData(img, 0, 0);
  for (let i = 0; i < 170; i++) {   // rubber scuffs from earlier matches: faint arcs where wheels turned
    g.strokeStyle = `rgba(18,18,20,${0.02 + rnd() * 0.045})`; g.lineWidth = 3 + rnd() * 9;
    const x = rnd() * px, y = rnd() * px, r = 50 + rnd() * 420, a0 = rnd() * 6.28;
    g.beginPath(); g.arc(x, y, r, a0, a0 + 0.25 + rnd() * 0.9); g.stroke();
  }
  for (let i = 0; i < 45; i++) {      // dust and lighter worn patches
    g.fillStyle = `rgba(205,205,198,${0.012 + rnd() * 0.02})`;
    g.beginPath(); g.ellipse(rnd() * px, rnd() * px, 20 + rnd() * 110, 10 + rnd() * 60, rnd() * 3, 0, 6.28); g.fill();
  }
  // interlocking edges: rounded tabs every 1.5 in, a dark gap with a faint lit lip
  const tab = (u) => { const w = Math.sin(u); return Math.sign(w) * Math.pow(Math.abs(w), 0.35); };
  for (let i = 1; i < n; i++) for (const vert of [true, false]) for (const [col, off, lw] of [["rgba(255,255,255,0.05)", 1.4, 1], ["rgba(24,25,27,0.5)", 0, 1.4]]) {
    g.strokeStyle = col; g.lineWidth = lw; g.beginPath();
    for (let k = 0; k <= 960; k++) {
      const a = k * px / 960, b = i * s + 3 * tab(a / s * 16 * Math.PI) + off;
      if (vert) { if (k) g.lineTo(b, a); else g.moveTo(b, a); } else { if (k) g.lineTo(a, b); else g.moveTo(a, b); }
    }
    g.stroke();
  }
  const tex = new THREE.CanvasTexture(c); tex.colorSpace = THREE.SRGBColorSpace; tex.anisotropy = renderer.capabilities.getMaxAnisotropy();
  const mat = std({ map: tex, roughness: 0.9, bumpMap: noiseTex(512, 128, 70, 5), bumpScale: 0.35 });
  mat.bumpMap.repeat.set(12, 12);
  const tiles = new THREE.Mesh(new THREE.BoxGeometry(2 * H, 2 * H, 0.0127), mat);
  tiles.position.z = -0.0064; tiles.receiveShadow = true; gp.add(tiles);
}
// Simplified stand-ins, used only for CAD groups that failed to load, so nothing is ever invisible.
function fallbackField(missing) {
  const has = (g) => missing.includes(g);
  const mk = (geo, mat, x, y, z, parent = gp) => { const m = new THREE.Mesh(geo, mat); m.position.set(x, y, z); m.castShadow = !mat.userData.noShadow; m.receiveShadow = true; parent.add(m); return m; };
  if (has("perimeter")) {
    const wh = F.perimeter.wallHeight_m, L = 2 * H + 0.05;
    const g = new THREE.Group(); gp.add(g);
    [[-1, 0], [1, 0], [0, -1], [0, 1]].forEach(([sx, sy], w) => {
      const wg = new THREE.Group(); g.add(wg); walls[w] = wg;
      const along = sx === 0, x = sx * (H + 0.012), y = sy * (H + 0.012);
      mk(new THREE.BoxGeometry(along ? L : 0.006, along ? 0.006 : L, wh - 0.05), M.sheet, x, y, wh / 2, wg);
      for (const z of [0.02, wh]) mk(new THREE.BoxGeometry(along ? L : 0.03, along ? 0.03 : L, 0.04), M.aluDark, x, y, z, wg);
    });
  }
  if (has("frame")) F.hiveFrame.legs.forEach((l) => {
    const a = new THREE.Vector3(...l.base), b = new THREE.Vector3(...l.apex), m = mk(new THREE.CylinderGeometry(0.019, 0.019, a.distanceTo(b), 12), M.alu, 0, 0, 0);
    m.position.copy(a.clone().add(b).multiplyScalar(0.5)); m.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), b.clone().sub(a).normalize());
  });
  if (has("flowers")) F.flowers.forEach((f) => {
    const fg = F.flowerGeometry, m = mk(new THREE.CylinderGeometry(fg.outerRadius_m, fg.outerRadius_m, fg.topHeight_m, 24, 1, true), finish("plastic", 0x5fa73d), f.axis[0], f.axis[1], fg.topHeight_m / 2);
    m.rotation.x = Math.PI / 2;
  });
  for (let h = 0; h < 2; h++) if (has(h ? "hive_blue" : "hive_red")) {
    const inner = hives[h].children[0];
    F.hives[h].cells.forEach((c) => {   // each CELL: an open tilted box at its CAD-up pose
      const cg = new THREE.Group(); const cc = c.upClusterCenter; cg.position.set(cc[0], cc[1], cc[2] + 0.12);
      cg.rotation.x = (c.side === "audience" ? -1 : 1) * F.cell.tiltAngle_rad;
      const W = F.cell.openingWidth_m, D = F.cell.openingHeight_m, Hc = F.cell.depth_m;
      mk(new THREE.BoxGeometry(W, D, 0.004), M.hiveSheet, 0, 0, -Hc / 2, cg);
      for (const sx of [-1, 1]) mk(new THREE.BoxGeometry(0.004, D, Hc), M.hiveSheet, sx * W / 2, 0, 0, cg);
      for (const sy of [-1, 1]) mk(new THREE.BoxGeometry(W, 0.004, Hc), M.hiveSheet, 0, sy * D / 2, 0, cg);
      inner.add(cg);
    });
  }
}
// The perimeter CAD is one scene; split it into its four walls (-x, +x, -y, +y) so each can flex on impact.
const walls = [null, null, null, null];
function splitWalls(root) {
  const groups = [0, 1, 2, 3].map(() => new THREE.Group());
  const meshes = []; root.traverse((o) => { if (o.isMesh) meshes.push(o); });
  groups.forEach((g, w) => { root.add(g); walls[w] = g; });
  root.updateMatrixWorld(true);
  const box = new THREE.Box3(), c = new THREE.Vector3(), toField = new THREE.Matrix4().copy(gp.matrixWorld).invert();
  for (const m of meshes) {
    box.setFromObject(m).getCenter(c); c.applyMatrix4(toField);       // world (y up) -> field frame (z up)
    const w = Math.abs(c.x) > Math.abs(c.y) ? (c.x < 0 ? 0 : 1) : (c.y < 0 ? 2 : 3);
    groups[w].attach(m);
  }
  return groups;
}
// event floor: rubber mats under the field and the drive team areas, laid on the forest floor
{
  const mats = new THREE.Mesh(new THREE.BoxGeometry(9.6, 7.4, 0.012), M.floor);
  mats.position.z = -0.018; mats.receiveShadow = true; gp.add(mats);
}
// painted zones the CAD doesn't include as solids (tape is in the CAD; ALLIANCE AREAS are floor markings)
const zone = (b, color, z) => { const m = new THREE.Mesh(new THREE.PlaneGeometry(b.xMax - b.xMin, b.yMax - b.yMin), std({ color, roughness: 0.92 })); m.position.set((b.xMin + b.xMax) / 2, (b.yMin + b.yMax) / 2, z); m.receiveShadow = true; gp.add(m); };
zone(F.allianceAreas.red, 0x2a1513, -0.008); zone(F.allianceAreas.blue, 0x121b2a, -0.008);

// ================= static forest around the clearing =================
// Everything is generated from a fixed seed (identical every load) and never moves.
function groundHeight(x, y) {   // flat clearing for the event, gently rolling forest floor beyond
  const r = Math.hypot(x, y), k = Math.min(1, Math.max(0, (r - 9) / 14));
  return k * (0.45 * Math.sin(x * 0.17) * Math.cos(y * 0.13) + 0.3 * Math.sin(x * 0.41 + 1.3) * Math.sin(y * 0.37 + 0.7) + 0.12 * Math.sin(x * 1.1 + y * 0.9));
}
function canvasTex(size, paint, repeat) {
  const c = document.createElement("canvas"); c.width = c.height = size; paint(c.getContext("2d"), size);
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; t.wrapS = t.wrapT = THREE.RepeatWrapping;
  if (repeat) t.repeat.set(repeat, repeat); t.anisotropy = 8; return t;
}
function buildForest() {
  let sd = 4242; const rnd = () => (sd = (sd * 16807) % 2147483647) / 2147483647;
  // forest floor: mixed grass, moss, soil and fallen needles
  const floorTex = canvasTex(1024, (g, n) => {
    g.fillStyle = "#2c3320"; g.fillRect(0, 0, n, n);
    for (let i = 0; i < 26000; i++) {
      const x = rnd() * n, y = rnd() * n, t = rnd();
      g.fillStyle = t < 0.45 ? `rgba(${48 + rnd() * 30 | 0},${62 + rnd() * 34 | 0},${28 + rnd() * 14 | 0},0.8)`
                  : t < 0.75 ? `rgba(${70 + rnd() * 30 | 0},${52 + rnd() * 20 | 0},${34 + rnd() * 14 | 0},0.7)`
                  : `rgba(${96 + rnd() * 40 | 0},${70 + rnd() * 26 | 0},${40 + rnd() * 16 | 0},0.6)`;
      g.save(); g.translate(x, y); g.rotate(rnd() * Math.PI); g.fillRect(-0.5, -2 - rnd() * 4, 1 + rnd(), 4 + rnd() * 8); g.restore();
    }
  }, 60);
  const ground = new THREE.PlaneGeometry(320, 320, 220, 220);
  const gpos = ground.attributes.position;
  for (let i = 0; i < gpos.count; i++) gpos.setZ(i, groundHeight(gpos.getX(i), gpos.getY(i)) - 0.03);
  ground.computeVertexNormals();
  const gmesh = new THREE.Mesh(ground, std({ map: floorTex, roughness: 0.97, bumpMap: noiseTex(512, 128, 120, 21), bumpScale: 1.2, envMapIntensity: 0.25 }));
  gmesh.material.bumpMap.repeat.set(80, 80); gmesh.receiveShadow = true; gp.add(gmesh);

  const bark = canvasTex(256, (g, n) => {
    g.fillStyle = "#3a2e24"; g.fillRect(0, 0, n, n);
    for (let i = 0; i < 900; i++) { const v = 30 + rnd() * 40 | 0; g.fillStyle = `rgba(${v + 12},${v},${v - 6},0.7)`; g.fillRect(rnd() * n, rnd() * n, 1 + rnd() * 3, 10 + rnd() * 50); }
  });
  bark.repeat.set(2, 6);
  const barkM = std({ map: bark, roughness: 1, bumpMap: bark, bumpScale: 3, envMapIntensity: 0.25 });
  // needle / leaf textures with alpha cut-outs: ragged branch silhouettes instead of smooth solids
  const needleTex = canvasTex(512, (g, n) => {
    g.clearRect(0, 0, n, n);
    for (let i = 0; i < 9000; i++) {
      const v = rnd(), x = rnd() * n, y = (1 - Math.pow(v, 0.8)) * n * 0.97;   // denser toward the top of each tier
      const L = 6 + rnd() * 16, a = Math.PI / 2 + (rnd() - 0.5) * 1.3, gl = 120 + rnd() * 90 | 0;
      g.strokeStyle = `rgb(${gl * 0.45 | 0},${gl},${gl * 0.5 | 0})`; g.lineWidth = 1 + rnd() * 1.5;
      g.beginPath(); g.moveTo(x, y); g.lineTo(x + Math.cos(a) * L, y + Math.sin(a) * L); g.stroke();
    }
  });
  const leafTex = canvasTex(512, (g, n) => {
    g.clearRect(0, 0, n, n);
    for (let i = 0; i < 2600; i++) {
      const x = rnd() * n, y = rnd() * n, gl = 120 + rnd() * 100 | 0;
      g.fillStyle = `rgb(${gl * 0.6 | 0},${gl},${gl * 0.4 | 0})`;
      g.save(); g.translate(x, y); g.rotate(rnd() * 6.28); g.beginPath(); g.ellipse(0, 0, 3 + rnd() * 5, 1.5 + rnd() * 2.5, 0, 0, 6.28); g.fill(); g.restore();
    }
  });
  leafTex.repeat.set(3, 2);
  const needleM = std({ map: needleTex, alphaTest: 0.35, side: THREE.DoubleSide, roughness: 0.88, envMapIntensity: 0.25 });
  const leafM = std({ map: leafTex, alphaTest: 0.35, side: THREE.DoubleSide, roughness: 0.82, envMapIntensity: 0.25 });
  const rockM = std({ color: 0x4f514b, roughness: 0.95, bumpMap: noiseTex(256, 128, 120, 33), bumpScale: 2, flatShading: true, envMapIntensity: 0.2 });
  // organic geometry: jitter vertices so no two tiers/blobs look machine-made
  const jitter = (geo, amt) => { const p = geo.attributes.position; for (let i = 0; i < p.count; i++) { const k = 1 + (rnd() - 0.5) * amt; p.setXYZ(i, p.getX(i) * k, p.getY(i), p.getZ(i) * k); } geo.computeVertexNormals(); return geo; };
  const coneGeo = jitter(new THREE.ConeGeometry(1, 1, 11, 3, true), 0.35); coneGeo.translate(0, 0.5, 0);
  const trunkGeo = new THREE.CylinderGeometry(0.55, 1, 1, 9, 1); trunkGeo.translate(0, 0.5, 0);
  const blobGeo = new THREE.IcosahedronGeometry(1, 2);
  { const p = blobGeo.attributes.position; for (let i = 0; i < p.count; i++) { const k = 0.78 + rnd() * 0.42; p.setXYZ(i, p.getX(i) * k, p.getY(i) * k * 0.85, p.getZ(i) * k); } blobGeo.computeVertexNormals(); }
  const rockGeo = new THREE.DodecahedronGeometry(1, 1);
  { const p = rockGeo.attributes.position; for (let i = 0; i < p.count; i++) { const k = 0.7 + rnd() * 0.5; p.setXYZ(i, p.getX(i) * k, p.getY(i) * k * 0.6, p.getZ(i) * k); } rockGeo.computeVertexNormals(); }

  const place = [];
  for (let i = 0; i < 520; i++) {   // trees: none inside the clearing, denser with distance
    const r = 12.5 + Math.pow(rnd(), 0.7) * 85, a = rnd() * Math.PI * 2;
    place.push({ x: Math.cos(a) * r, y: Math.sin(a) * r, conifer: rnd() < 0.68, h: 9 + rnd() * 15, s: 0.75 + rnd() * 0.6 });
  }
  const nCone = place.filter((t) => t.conifer).length;
  const trunks = new THREE.InstancedMesh(trunkGeo, barkM, place.length);
  const tiers = new THREE.InstancedMesh(coneGeo, needleM, nCone * 6);
  const blobs = new THREE.InstancedMesh(blobGeo, leafM, (place.length - nCone) * 6);
  const m4 = new THREE.Matrix4(), q = new THREE.Quaternion(), e = new THREE.Euler(), col = new THREE.Color();
  // tree coordinates are in the three.js world (y up): field (x, y) -> world (x, -y)
  const put = (im, i, x, y, z, sx, sy, sz, ry, tilt = 0) => { e.set(tilt, ry, tilt * 0.5); q.setFromEuler(e); m4.compose(new THREE.Vector3(x, y, z), q, new THREE.Vector3(sx, sy, sz)); im.setMatrixAt(i, m4); };
  let ti = 0, bi = 0;
  place.forEach((t, i) => {
    const gy = groundHeight(t.x, t.y), wx = t.x, wz = -t.y;
    if (t.conifer) {
      put(trunks, i, wx, gy - 0.2, wz, 0.22 * t.s, t.h * 0.75, 0.22 * t.s, rnd() * 6);
      for (let k = 0; k < 6; k++) {
        const f = k / 6, rad = (2.6 - 2.1 * f) * t.s, hh = t.h * 0.28;
        put(tiers, ti, wx, gy + t.h * (0.22 + 0.68 * f), wz, rad, hh, rad, rnd() * 6, (rnd() - 0.5) * 0.08);
        col.setHSL(0.3 + (rnd() - 0.5) * 0.05, 0.25 + rnd() * 0.15, 0.32 + rnd() * 0.12); tiers.setColorAt(ti++, col);
      }
    } else {
      put(trunks, i, wx, gy - 0.2, wz, 0.3 * t.s, t.h * 0.62, 0.3 * t.s, rnd() * 6, (rnd() - 0.5) * 0.06);
      for (let k = 0; k < 6; k++) {
        const rad = (2.0 + rnd() * 1.8) * t.s, a = rnd() * 6.28, d = rnd() * 2.2 * t.s;
        put(blobs, bi, wx + Math.cos(a) * d, gy + t.h * (0.62 + rnd() * 0.3), wz + Math.sin(a) * d, rad, rad * 0.85, rad, rnd() * 6);
        col.setHSL(0.22 + (rnd() - 0.5) * 0.07, 0.3 + rnd() * 0.2, 0.34 + rnd() * 0.14); blobs.setColorAt(bi++, col);
      }
    }
  });
  const forest = new THREE.Group();   // world-space group (not inside the field frame)
  // the rig's shadow cameras end ~14 m out and the trees stand beyond the light, so they only receive
  [trunks, tiers, blobs].forEach((im) => { im.castShadow = false; im.receiveShadow = true; im.userData.noAO = true; forest.add(im); });
  // undergrowth, rocks and fallen logs
  const bushes = new THREE.InstancedMesh(blobGeo, leafM, 420), rocks = new THREE.InstancedMesh(rockGeo, rockM, 90), logs = new THREE.InstancedMesh(trunkGeo, barkM, 26);
  for (let i = 0; i < 420; i++) {
    const r = 8.5 + Math.pow(rnd(), 0.8) * 70, a = rnd() * 6.28, x = Math.cos(a) * r, y = Math.sin(a) * r, s2 = 0.35 + rnd() * 0.9;
    put(bushes, i, x, groundHeight(x, y) + s2 * 0.35, -y, s2 * 1.3, s2 * 0.8, s2 * 1.2, rnd() * 6);
    col.setHSL(0.24 + (rnd() - 0.5) * 0.08, 0.3 + rnd() * 0.2, 0.3 + rnd() * 0.12); bushes.setColorAt(i, col);
  }
  for (let i = 0; i < 90; i++) {
    const r = 9.5 + rnd() * 58, a = rnd() * 6.28, x = Math.cos(a) * r, y = Math.sin(a) * r, s2 = 0.2 + Math.pow(rnd(), 2) * (r < 16 ? 0.6 : 1.3);
    put(rocks, i, x, groundHeight(x, y) + s2 * 0.1, -y, s2, s2, s2 * (0.8 + rnd() * 0.5), rnd() * 6);
    col.setHSL(0.18 + rnd() * 0.08, 0.08 + rnd() * 0.12, 0.3 + rnd() * 0.15); rocks.setColorAt(i, col);   // lichen and moss
  }
  for (let i = 0; i < 26; i++) {
    const r = 10 + rnd() * 50, a = rnd() * 6.28, x = Math.cos(a) * r, y = Math.sin(a) * r, L = 3 + rnd() * 7;
    e.set(Math.PI / 2, rnd() * 6, 0); q.setFromEuler(e);
    m4.compose(new THREE.Vector3(x, groundHeight(x, y) + 0.2, -y), q, new THREE.Vector3(0.22, L, 0.22)); logs.setMatrixAt(i, m4);
  }
  [bushes, rocks, logs].forEach((im) => { im.castShadow = false; im.receiveShadow = true; im.userData.noAO = true; forest.add(im); });
  bushes.userData.optional = true;
  // grass tufts around the clearing (alpha-tested blade cards)
  const grassTex = canvasTex(256, (g, n) => {
    g.clearRect(0, 0, n, n);
    for (let i = 0; i < 70; i++) {
      const x = n * (0.1 + rnd() * 0.8), h = n * (0.35 + rnd() * 0.6), lean = (rnd() - 0.5) * n * 0.25;
      g.strokeStyle = `rgb(${60 + rnd() * 50 | 0},${85 + rnd() * 60 | 0},${35 + rnd() * 25 | 0})`; g.lineWidth = 2 + rnd() * 3;
      g.beginPath(); g.moveTo(x, n); g.quadraticCurveTo(x + lean * 0.3, n - h * 0.6, x + lean, n - h); g.stroke();
    }
  });
  const grassM = std({ map: grassTex, alphaTest: 0.45, side: THREE.DoubleSide, roughness: 0.9, envMapIntensity: 0.25 });
  const card = new THREE.PlaneGeometry(0.6, 0.35); card.translate(0, 0.175, 0);
  const grass = new THREE.InstancedMesh(card, grassM, 5200);
  for (let i = 0; i < 5200; i++) {
    const r = 5.4 + Math.pow(rnd(), 0.6) * 22, a = rnd() * 6.28, x = Math.cos(a) * r, y = Math.sin(a) * r, s2 = 0.7 + rnd() * 0.8;
    put(grass, i, x, groundHeight(x, y) - 0.02, -y, s2, s2, s2, rnd() * 6);
  }
  grass.receiveShadow = true; grass.userData.noAO = true; grass.userData.optional = true; forest.add(grass);
  forest.traverse((o) => { if (o.isInstancedMesh) { o.instanceMatrix.needsUpdate = true; if (o.instanceColor) o.instanceColor.needsUpdate = true; o.computeBoundingSphere(); } });
  vegetation = [bushes, grass];
  scene.add(forest);

  // the lighting rig itself: a box truss on four towers outside the drive team areas (one merged mesh)
  const truss = std({ color: 0xbfc4c9, metalness: 1, roughness: 0.38 });
  const chords = [];
  const beam = (a, b) => {   // 4-chord box truss with zig-zag lacing between two points (world space)
    const d = b.clone().sub(a), len = d.length(), mid = a.clone().add(b).multiplyScalar(0.5);
    const qq = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 1, 0), d.clone().normalize());
    const corner = [[0.15, 0.15], [-0.15, 0.15], [-0.15, -0.15], [0.15, -0.15]];
    for (const [ox, oz] of corner) {
      const g = new THREE.CylinderGeometry(0.024, 0.024, len, 6);
      g.applyQuaternion(qq); g.translate(...mid.clone().add(new THREE.Vector3(ox, 0, oz).applyQuaternion(qq)).toArray()); chords.push(g);
    }
    const nLace = Math.max(2, Math.round(len / 0.5));
    for (let k = 0; k < nLace; k++) for (let side = 0; side < 4; side++) {   // diagonal lacing on each face
      const c0 = corner[side], c1 = corner[(side + 1) % 4];
      const p0 = a.clone().add(d.clone().multiplyScalar(k / nLace)).add(new THREE.Vector3(c0[0], 0, c0[1]).applyQuaternion(qq));
      const p1 = a.clone().add(d.clone().multiplyScalar((k + 1) / nLace)).add(new THREE.Vector3(c1[0], 0, c1[1]).applyQuaternion(qq));
      const dd = p1.clone().sub(p0), g = new THREE.CylinderGeometry(0.008, 0.008, dd.length(), 4);
      g.applyQuaternion(new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 1, 0), dd.clone().normalize()));
      g.translate(...p0.clone().add(p1).multiplyScalar(0.5).toArray()); chords.push(g);
    }
  };
  const R = RIG + 0.6, P = (x, y, z) => new THREE.Vector3(x, y, z);
  [[-R, -R], [R, -R], [R, R], [-R, R]].forEach(([x, z], i, arr) => {
    beam(P(x, groundHeight(x, -z), z), P(x, RIG_H + 0.3, z));                           // tower
    const [x2, z2] = arr[(i + 1) % 4]; beam(P(x, RIG_H + 0.3, z), P(x2, RIG_H + 0.3, z2));   // span
  });
  const trussMesh = new THREE.Mesh(mergeGeometries(chords), truss); trussMesh.userData.noAO = true; scene.add(trussMesh);
  chords.forEach((g) => g.dispose());
  const body = std({ color: 0x1a1b1d, roughness: 0.6, metalness: 0.4 });
  const lens = new THREE.MeshStandardMaterial({ color: 0x000000, emissive: new THREE.Color(1, 0.96, 0.88), emissiveIntensity: 6 });
  const cans = [], glasses = [];
  const o3 = new THREE.Object3D();
  fixtures.forEach((f) => {
    o3.position.copy(f.position); o3.lookAt(f.target.position); o3.rotateX(Math.PI / 2); o3.updateMatrix();
    cans.push(new THREE.CylinderGeometry(0.16, 0.2, 0.42, 16).applyMatrix4(o3.matrix));
    o3.position.copy(f.position).add(f.target.position.clone().sub(f.position).normalize().multiplyScalar(0.22));
    o3.rotation.set(0, 0, 0); o3.lookAt(f.target.position); o3.updateMatrix();
    glasses.push(new THREE.CircleGeometry(FIX_LENS / 2, 20).applyMatrix4(o3.matrix));
  });
  scene.add(new THREE.Mesh(mergeGeometries(cans), body), new THREE.Mesh(mergeGeometries(glasses), lens));
}
buildForest();
