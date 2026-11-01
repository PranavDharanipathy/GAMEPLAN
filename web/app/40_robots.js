// ================= scoring elements: the official perforated balls, posed, spun and squished per ball =================
const ballMats = {
  0: phys({ color: 0xf2c418, roughness: 0.38, clearcoat: 0.25, clearcoatRoughness: 0.45, side: THREE.DoubleSide }),
  1: phys({ color: 0xd3271f, roughness: 0.38, clearcoat: 0.25, clearcoatRoughness: 0.45, side: THREE.DoubleSide }),
  2: phys({ color: 0x1d5fd6, roughness: 0.38, clearcoat: 0.25, clearcoatRoughness: 0.45, side: THREE.DoubleSide }),
};
// two levels of detail of the same official mesh: full hole detail near the camera, lighter far away
const ballIM = {}, ballIMfar = {};
function buildBalls() {
  const fallback = new THREE.SphereGeometry(1, 32, 24);
  [0, 1, 2].forEach((k) => {
    for (const far of [false, true]) {
      const g = k === 0 ? ((far && ballGeo.pollen_lod1) || ballGeo.pollen || fallback) : ((far && ballGeo.nectar_lod1) || ballGeo.nectar || fallback);
      const im = new THREE.InstancedMesh(g, ballMats[k], k === 0 ? 64 : 24);
      im.castShadow = im.receiveShadow = true; im.count = 0; im.frustumCulled = false;
      im.userData.unitR = g === fallback ? 1 : (k === 0 ? 0.03556 : 0.045975);
      gp.add(im); (far ? ballIMfar : ballIM)[k] = im;
    }
  });
}
const _m = new THREE.Matrix4(), _q = new THREE.Quaternion(), _s = new THREE.Matrix4(), _r = new THREE.Matrix4(), _sv = new THREE.Vector3();
function ballMatrix(fr, o, unitR) {
  // squash along the contact normal, bulge sideways (volume ~ conserved): S = I + a n n^T + b (I - n n^T)
  const r = fr[o + 4], sq = fr[o + 10], nx = fr[o + 11], ny = fr[o + 12], nz = fr[o + 13];
  const a = -sq / r, b = 0.5 * sq / r;
  _s.set(1 + b + (a - b) * nx * nx, (a - b) * nx * ny, (a - b) * nx * nz, 0,
         (a - b) * ny * nx, 1 + b + (a - b) * ny * ny, (a - b) * ny * nz, 0,
         (a - b) * nz * nx, (a - b) * nz * ny, 1 + b + (a - b) * nz * nz, 0, 0, 0, 0, 1);
  _q.set(fr[o + 7], fr[o + 8], fr[o + 9], fr[o + 6]);
  _r.makeRotationFromQuaternion(_q).scale(_sv.set(r / unitR, r / unitR, r / unitR));
  _m.multiplyMatrices(_s, _r); _m.setPosition(fr[o], fr[o + 1], fr[o + 2]);
  return _m;
}

// ================= robots: a goBILDA-style mecanum launcher, open so the balls inside stay visible =================
// Geometry follows the simulated robot (frame size, wheel positions and radius, intake reach and roller,
// internal ball path, flywheel and hood). Body frame: x forward, y left, z up, metres.
const robots = [];
// goBILDA-style hole pattern (14 mm holes on a 24 mm pitch with 4 mm holes between), as an alpha cut-out
const holeTex = (() => {
  const px = 128, c = document.createElement("canvas"); c.width = c.height = px; const g = c.getContext("2d"), mm = px / 24;
  g.fillStyle = "#fff"; g.fillRect(0, 0, px, px); g.fillStyle = "#000";
  g.beginPath(); g.arc(px / 2, px / 2, 7 * mm, 0, 7); g.fill();
  for (const [x, y] of [[-8, -8], [8, -8], [-8, 8], [8, 8]]) { g.beginPath(); g.arc(px / 2 + x * mm, px / 2 + y * mm, 2 * mm, 0, 7); g.fill(); }
  const t = new THREE.CanvasTexture(c); t.wrapS = t.wrapT = THREE.RepeatWrapping; t.anisotropy = 4; return t;
})();
const RM = {
  frame: std({ color: 0xd9dcdf, metalness: 0.88, roughness: 0.42, envMapIntensity: 1.6, alphaMap: holeTex, alphaTest: 0.5, alphaToCoverage: true, side: THREE.DoubleSide }),
  alu: finish("metal", 0xc9cdd1), steel: M.steel, black: finish("plastic", 0x1d1e20),
  can: std({ color: 0x232426, metalness: 0.6, roughness: 0.35 }), gearbox: std({ color: 0xaeb3b8, metalness: 0.9, roughness: 0.38, envMapIntensity: 1.5 }),
  yellow: std({ color: 0xf2c418, roughness: 0.45 }), tread: finish("rubber", 0x171717), roller: std({ color: 0x3b3d40, roughness: 0.55 }),
  compliant: phys({ color: 0x2f8a3a, roughness: 0.72, specularIntensity: 0.4 }), hubPlate: std({ color: 0xb9bdc2, metalness: 0.85, roughness: 0.4, envMapIntensity: 1.5 }),
  hood: std({ color: 0x232323, roughness: 0.5, side: THREE.DoubleSide }), sheet: M.sheet,
  wireRed: std({ color: 0xb3221b, roughness: 0.4 }), wireBlack: std({ color: 0x121212, roughness: 0.4 }),
  revHub: std({ color: 0x151618, roughness: 0.5, metalness: 0.15 }), port: std({ color: 0x3a3c40, roughness: 0.6 }),
  battery: std({ color: 0x24272b, roughness: 0.55 }), lens: phys({ color: 0x050608, roughness: 0.05, clearcoat: 1 }),
};
// box whose UVs are in hole-pattern units on every face, so the pattern keeps its real size on any part
function patternBox(sx, sy, sz, pitch = 0.024) {
  const g = new THREE.BoxGeometry(sx, sy, sz), p = g.attributes.position, n = g.attributes.normal, uv = g.attributes.uv;
  for (let i = 0; i < p.count; i++) {
    const ax = Math.abs(n.getX(i)) > 0.5 ? 0 : Math.abs(n.getY(i)) > 0.5 ? 1 : 2;
    const a = ax === 0 ? p.getY(i) : p.getX(i), b = ax === 2 ? p.getY(i) : p.getZ(i);
    uv.setXY(i, a / pitch + 0.5, b / pitch + 0.5);
  }
  return g;
}
const place = (geo, x, y, z, rx = 0, ry = 0, rz = 0) => geo.applyMatrix4(new THREE.Matrix4().compose(
  new THREE.Vector3(x, y, z), new THREE.Quaternion().setFromEuler(new THREE.Euler(rx, ry, rz)), new THREE.Vector3(1, 1, 1)));
function channelGeo(len) {   // 48 mm U-channel along x, open toward +z, centred
  const w = 0.048, t = 0.0025;
  return mergeGeometries([place(patternBox(len, w, t), 0, 0, -w / 2 + t / 2), place(patternBox(len, t, w), 0, -w / 2 + t / 2, 0), place(patternBox(len, t, w), 0, w / 2 - t / 2, 0)]);
}
// mecanum wheel: two hub plates and 10 barrel rollers at 45 degrees (contact roller axis = (1, handed) from above)
const wheelCache = new Map();
function mecanumGeo(R, handed) {
  const key = R.toFixed(4) + ":" + handed;
  if (wheelCache.has(key)) return wheelCache.get(key);
  const N = 10, rr = R * 0.24, len = R * 1.05, prof = [];
  for (let i = 0; i <= 10; i++) { const t = i / 10 * 2 - 1; prof.push(new THREE.Vector2(rr * (1 - 0.3 * t * t), t * len / 2)); }
  const rollers = [], plates = [], Y = new THREE.Vector3(0, 1, 0);
  for (let k = 0; k < N; k++) {
    const a = k / N * Math.PI * 2, radial = new THREE.Vector3(Math.cos(a), 0, Math.sin(a)), tangent = new THREE.Vector3(-Math.sin(a), 0, Math.cos(a));
    const axis = tangent.multiplyScalar(Math.SQRT1_2).add(new THREE.Vector3(0, handed * Math.SQRT1_2, 0)).normalize();
    const g = new THREE.LatheGeometry(prof, 10).applyQuaternion(new THREE.Quaternion().setFromUnitVectors(Y, axis));
    rollers.push(g.translate(...radial.multiplyScalar(R - rr * 0.95).toArray()));
  }
  for (const s of [-1, 1]) plates.push(new THREE.CylinderGeometry(R * 0.6, R * 0.6, 0.004, 28).translate(0, s * len * 0.36, 0));
  plates.push(new THREE.CylinderGeometry(R * 0.2, R * 0.2, len * 0.75, 16));                     // hub
  const out = { rollers: mergeGeometries(rollers), plates: mergeGeometries(plates) };
  out.rollers.userData.shared = out.plates.userData.shared = true;
  wheelCache.set(key, out);
  return out;
}
function signTexture(label, red) {
  const c = document.createElement("canvas"); c.width = 512; c.height = 160; const g = c.getContext("2d");
  g.fillStyle = red ? "#c4241c" : "#1b4fbe"; g.fillRect(0, 0, 512, 160);
  g.fillStyle = "#fff"; g.font = "700 104px Barlow Condensed, Arial Narrow, sans-serif"; g.textAlign = "center"; g.textBaseline = "middle";
  g.fillText(label.toUpperCase(), 256, 86);
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; t.anisotropy = 4; return t;
}
const tube = (pts, r = 0.0025) => new THREE.TubeGeometry(new THREE.CatmullRomCurve3(pts.map((p) => new THREE.Vector3(...p))), 24, r, 6, false);
function buildRobot(info) {
  const red = info.alliance === "red", L = info.length, W = info.width, Hh = info.height;
  const root = new THREE.Group(), g = new THREE.Group(); root.add(g);   // root: pose on the field; g: rocks on its wheels
  const parts = new Map(), put = (mat, geo) => { if (!parts.has(mat)) parts.set(mat, []); parts.get(mat).push(geo); return geo; };
  const hx = L / 2, hy = W / 2, wy = Math.abs(info.wheels[0].y), wr = info.wheels[0].r;
  const railY = wy - 0.05;                                    // inner rails, wheels outboard of them
  // --- drive base: two rails, a back cross member, a high front cross member (the intake mouth stays open)
  const fxm = 0.1;   // x of the front uprights and high cross member (between the wheels and the shooter)
  for (const s of [1, -1]) put(RM.frame, place(channelGeo(L - 0.07), 0, s * railY, 0.07, -s * Math.PI / 2));
  put(RM.frame, place(channelGeo(2 * railY - 0.048), -hx + 0.035, 0, 0.07, 0, -Math.PI / 2, Math.PI / 2));
  put(RM.frame, place(channelGeo(2 * railY + 0.048), fxm, 0, 0.21, 0, Math.PI / 2, Math.PI / 2));
  for (const s of [1, -1]) {
    put(RM.frame, place(patternBox(0.048, 0.048, 0.14), fxm, s * railY, 0.14));          // uprights to the high cross member
    // sign boards above the wheels on standoffs, robot signs on them
    put(RM.frame, place(patternBox(L - 0.03, 0.004, 0.07), 0, s * (hy - 0.008), 0.15));
    for (const x of [-0.03, 0.03]) put(RM.alu, place(new THREE.BoxGeometry(0.02, hy - 0.008 - railY, 0.024), x, s * (railY + (hy - 0.008 - railY) / 2), 0.106));
    const sign = new THREE.Mesh(new THREE.PlaneGeometry(L * 0.52, 0.062), std({ map: signTexture(info.label, red), roughness: 0.55 }));
    sign.position.set(-L * 0.08, s * (hy - 0.005), 0.15);
    sign.quaternion.setFromRotationMatrix(new THREE.Matrix4().makeBasis(new THREE.Vector3(-s, 0, 0), new THREE.Vector3(0, 0, 1), new THREE.Vector3(0, s, 0)));
    sign.castShadow = false; g.add(sign);
  }
  // --- drive motors: upright on right-angle gearboxes on the rails, output shaft through the rail to the wheel
  for (const w of info.wheels) {
    const s = Math.sign(w.y);
    put(RM.gearbox, place(new THREE.BoxGeometry(0.04, 0.04, 0.034), w.x, s * railY, 0.111));
    put(RM.can, place(new THREE.CylinderGeometry(0.0185, 0.0185, 0.062, 18), w.x, s * railY, 0.161, Math.PI / 2, 0, 0));
    put(RM.yellow, place(new THREE.CylinderGeometry(0.0189, 0.0189, 0.01, 18), w.x, s * railY, 0.134, Math.PI / 2, 0, 0));
    put(RM.black, place(new THREE.CylinderGeometry(0.012, 0.012, 0.006, 14), w.x, s * railY, 0.195, Math.PI / 2, 0, 0));   // motor end cap
    put(RM.steel, place(new THREE.CylinderGeometry(0.004, 0.004, wy - railY + 0.03, 8), w.x, s * (railY + (wy - railY) / 2 + 0.01), w.r));   // axle
  }
  // --- electronics: battery on the floor, Control Hub on the back plate, Expansion Hub on the back cross member
  put(RM.battery, place(new THREE.BoxGeometry(0.075, 0.142, 0.05), -hx + 0.105, 0, 0.055));
  put(RM.sheet, place(new THREE.BoxGeometry(0.003, 0.2, 0.26), -hx + 0.003, 0, 0.255));
  put(RM.revHub, place(new THREE.BoxGeometry(0.032, 0.143, 0.103), -hx + 0.022, 0, 0.265));
  put(RM.revHub, place(new THREE.BoxGeometry(0.103, 0.143, 0.032), -hx + 0.085, 0, 0.11));
  for (let k = 0; k < 6; k++) put(RM.port, place(new THREE.BoxGeometry(0.006, 0.012, 0.008), -hx + 0.04, -0.06 + k * 0.024, 0.31));
  // wiring: battery to hub (red/black), hub to each motor (black)
  put(RM.wireRed, tube([[-hx + 0.1, 0.05, 0.08], [-hx + 0.07, 0.07, 0.15], [-hx + 0.04, 0.05, 0.22]], 0.003));
  put(RM.wireBlack, tube([[-hx + 0.1, 0.04, 0.08], [-hx + 0.075, 0.06, 0.15], [-hx + 0.04, 0.04, 0.22]], 0.003));
  for (const w of info.wheels) {
    const s = Math.sign(w.y);
    put(RM.wireBlack, tube([[-hx + 0.04, s * 0.05, 0.24], [-hx + 0.08, s * (railY - 0.03), 0.21], [w.x * 0.5, s * (railY - 0.035), 0.2], [w.x, s * (railY - 0.012), 0.2]], 0.0022));
  }
  // --- intake: compliant-wheel roller across the mouth on two arms, its motor on the right arm
  let roller = null, fly = null, hood = null, feeder = null;
  if (info.intake) {
    const it = info.intake, rx = hx + it.reach - it.rollerRadius;
    roller = new THREE.Group(); roller.position.set(rx, 0, it.rollerZ); g.add(roller);
    const shaft = new THREE.Mesh(new THREE.CylinderGeometry(0.004, 0.004, it.width + 0.04, 8), RM.steel); roller.add(shaft);
    const wheels = [];
    for (let k = 0; k < 5; k++) wheels.push(new THREE.CylinderGeometry(it.rollerRadius, it.rollerRadius, it.width / 6.5, 20).translate(0, (k - 2) * it.width / 5, 0));
    const cw = new THREE.Mesh(mergeGeometries(wheels), RM.compliant); cw.castShadow = true; roller.add(cw);
    const armY = railY - 0.03;   // arms run inside the front wheels
    for (const s of [1, -1]) put(RM.frame, place(patternBox(rx - hx + 0.07, 0.004, 0.036), (rx + hx - 0.07) / 2, s * armY, it.rollerZ));
    const ye = it.width / 2 + 0.005;   // intake motor on the end of the roller shaft
    put(RM.gearbox, place(new THREE.CylinderGeometry(0.018, 0.018, 0.025, 18), rx, -(ye + 0.0125), it.rollerZ));
    put(RM.yellow, place(new THREE.CylinderGeometry(0.0189, 0.0189, 0.008, 18), rx, -(ye + 0.029), it.rollerZ));
    put(RM.can, place(new THREE.CylinderGeometry(0.0185, 0.0185, 0.035, 18), rx, -(ye + 0.05), it.rollerZ));
  }
  // --- ball path: clear ramp from the mouth up to the shooter, with side guides; feeder wheels in the riser
  const P = info.ballPath.map((p) => new THREE.Vector3(...p)), rB = season.scoringElements.NECTAR.diameter_m / 2;
  for (let i = 1; i < P.length; i++) {
    const a = P[i - 1], b = P[i], d = b.clone().sub(a), len = d.length(), mid = a.clone().add(b).multiplyScalar(0.5), tilt = Math.atan2(d.z, d.x);
    if (Math.abs(d.z) < Math.abs(d.x) * 1.5) {   // sloped section: floor + side guides
      put(RM.sheet, place(new THREE.BoxGeometry(len + 0.01, 0.13, 0.003), mid.x, mid.y, mid.z - rB - 0.003, 0, -tilt, 0));
      for (const s of [1, -1]) put(RM.sheet, place(new THREE.BoxGeometry(len + 0.01, 0.003, 0.08), mid.x, s * 0.068, mid.z, 0, -tilt, 0));
    }
  }
  if (info.flywheel) {
    const f = info.flywheel, top = P[P.length - 1];
    feeder = new THREE.Group(); g.add(feeder);
    const fw = [];
    for (let k = 0; k < 3; k++) {
      const z = 0.17 + k * (top.z - 0.17) / 2.2;
      const grp = new THREE.Group(); grp.position.set(top.x - rB - 0.012, 0, z); feeder.add(grp); fw.push(grp);
      const m = new THREE.Mesh(new THREE.CylinderGeometry(0.024, 0.024, 0.07, 16), RM.compliant); m.castShadow = true; grp.add(m);
    }
    feeder.userData.wheels = fw;
    // shooter: the flywheel sits in front of the riser; balls wrap over it under the hood and leave forward
    const fx = top.x + 0.075, fz = top.z - 0.04, plateY = 0.068;
    for (const s of [1, -1]) put(RM.frame, place(patternBox(0.25, 0.004, Hh - 0.13), fx - 0.06, s * plateY, 0.13 + (Hh - 0.13) / 2));
    fly = new THREE.Group(); fly.position.set(fx, 0, fz); g.add(fly);
    const tyres = [], hubs = [];
    for (const s of [1, -1]) {
      tyres.push(new THREE.CylinderGeometry(f.radius, f.radius, 0.026, 36).translate(0, s * 0.016, 0));
      hubs.push(new THREE.CylinderGeometry(f.radius * 0.62, f.radius * 0.62, 0.028, 24).translate(0, s * 0.016, 0));
    }
    for (let k = 0; k < 3; k++) hubs.push(new THREE.BoxGeometry(f.radius * 1.1, 0.064, 0.008).rotateY(k * Math.PI / 3));   // spokes show the spin
    const ty = new THREE.Mesh(mergeGeometries(tyres), RM.tread), hb = new THREE.Mesh(mergeGeometries(hubs), RM.alu);
    ty.castShadow = hb.castShadow = true; fly.add(ty, hb);
    put(RM.steel, place(new THREE.CylinderGeometry(0.004, 0.004, 2 * plateY + 0.02, 8), fx, 0, fz));
    put(RM.can, place(new THREE.CylinderGeometry(0.0185, 0.0185, 0.06, 18), fx, plateY + 0.05, fz));        // shooter motor, direct drive
    put(RM.gearbox, place(new THREE.CylinderGeometry(0.018, 0.018, 0.03, 18), fx, plateY + 0.012, fz));
    put(RM.yellow, place(new THREE.CylinderGeometry(0.0188, 0.0188, 0.01, 18), fx, plateY + 0.03, fz));
    // adjustable hood: an arc around the flywheel axle; its end sets the launch angle
    hood = new THREE.Group(); hood.position.set(fx, 0, fz); g.add(hood);
    const hr = f.radius + 2 * rB - 0.012;
    const arc = new THREE.Mesh(new THREE.CylinderGeometry(hr, hr, 0.12, 20, 1, true, Math.PI / 2 - 0.87, 0.87), RM.hood); arc.castShadow = true; hood.add(arc);
    put(RM.black, place(new THREE.BoxGeometry(0.04, 0.02, 0.022), top.x - 0.02, plateY + 0.012, top.z - 0.01));   // gate servo
  }
  // webcam on the front cross member
  if (info.webcam !== false) {
    put(RM.black, place(new THREE.BoxGeometry(0.03, 0.07, 0.03), fxm + 0.02, 0, 0.25));
    put(RM.lens, place(new THREE.CylinderGeometry(0.009, 0.009, 0.006, 16), fxm + 0.038, 0, 0.25, 0, 0, Math.PI / 2));
  }
  // wheels (spin about the axle)
  const wheels = [];
  for (const w of info.wheels) {
    const handed = Math.sign(w.roller || 1) || 1, geo = mecanumGeo(w.r, handed);
    const wg = new THREE.Group(); wg.position.set(w.x, w.y, w.r);
    const spin = new THREE.Group(); wg.add(spin);
    const ro = new THREE.Mesh(geo.rollers, RM.roller), pl = new THREE.Mesh(geo.plates, RM.hubPlate);
    ro.castShadow = pl.castShadow = true; ro.receiveShadow = pl.receiveShadow = true; spin.add(ro, pl);
    g.add(wg); wheels.push({ spin, x: w.x, y: w.y });
  }
  // merge every static part per material
  for (const [mat, geos] of parts) {
    const m = new THREE.Mesh(mergeGeometries(geos), mat);
    m.castShadow = !mat.userData.noShadow; m.receiveShadow = true; g.add(m);
    geos.forEach((x) => x.dispose());
  }
  // driver feedback LED strip (teams wire one to show shooter state): off / amber / green
  const led = new THREE.Mesh(new THREE.BoxGeometry(0.014, 0.16, 0.012), new THREE.MeshStandardMaterial({ color: 0x111111, emissive: 0x000000, emissiveIntensity: 5 }));
  led.position.set(-hx + 0.006, 0, Hh - 0.012); g.add(led);
  return { g: root, body: g, L, W, wheels, roller, fly, hood, feeder, led, status: 0, prev: null };
}
function ensureRobots(n) {
  while (robots.length < n) {
    const info = state.sim.robotInfo(robots.length);
    const r = buildRobot(info); gp.add(r.g); robots.push(r);
  }
}
function clearRobots() {
  robots.forEach((r) => { gp.remove(r.g); r.g.traverse((o) => { if (o.isMesh && !o.geometry.userData.shared) o.geometry.dispose(); }); });
  robots.length = 0;
}
