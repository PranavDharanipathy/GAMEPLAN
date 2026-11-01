// Browser test under the artifact viewer's security policy (serve build-web/site with web/serve_csp.py on :8766).
// Checks: data: fetches are refused (as in the viewer), all field CAD loads (AprilTag stickers applied), the driver's view
// follows the robot, arrow keys turn, a full-speed wall crash rocks the robot and flexes the wall.
// Usage: python3 web/serve_csp.py build-web/site 8766 & node web/csp_test.js <screenshot dir>
const { chromium } = require('playwright');
const out = process.argv[2], url = 'http://127.0.0.1:8766/index.html';
(async () => {
  const b = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const p = await b.newPage({ viewport: { width: 1280, height: 720 } });
  const errs = [];
  // the deliberate data: probe below is expected to be refused; anything else is a real problem
  p.on('console', (m) => { if ((m.type() === 'error' || /Refused|CSP|Content Security/i.test(m.text())) && !m.text().includes('base64,AAAA')) errs.push(m.text().slice(0, 160)); });
  p.on('pageerror', (e) => errs.push('pageerror: ' + e.message));
  await p.addInitScript(() => { window.__gpPause = true; });   // frames are stepped by the test (software GL is slow)
  await p.goto(url);
  await p.waitForFunction(() => window.__gp && window.__gp.ready(), null, { timeout: 180000, polling: 500 });
  console.log('data: fetch under this CSP:', await p.evaluate(() => fetch('data:application/octet-stream;base64,AAAA').then(() => 'allowed', () => 'BLOCKED')));
  console.log('asset status:', JSON.stringify(await p.$eval('#assetState', (e) => e.textContent)));
  console.log('scene check:', JSON.stringify(await p.evaluate(() => { const s = window.__gp.scene; let meshes = 0, tris = 0, tags = 0; s.traverse((o) => { if (o.isMesh) { meshes++; if (o.userData.keep) tags++; if (o.geometry.index) tris += o.geometry.index.count / 3; } }); return { meshes, tris: Math.round(tris), aprilTagStickers: tags }; })));
  await p.evaluate(() => { window.__gp.advance(6); window.__gp.renderFrame(); });
  await p.screenshot({ path: out + '/menu.png', timeout: 180000 });
  await p.evaluate(() => window.__gp.setSetting('preset', 'high'));
  await p.click('#mPlay'); await p.click('#mPrac'); await p.click('#startBtn');
  await p.waitForFunction(() => window.__gp.state.running, null, { timeout: 60000, polling: 500 });
  await p.evaluate(() => { window.__gpPause = true; window.__gp.advance(3.6); });
  const cam = () => p.evaluate(() => { window.__gp.renderFrame(); const c = window.__gp.camera.rotation; const f = window.__gp.state.sim.frame(); return { yaw: +c.y.toFixed(2), pitch: +c.x.toFixed(2), rx: +f[7].toFixed(2), ry: +f[8].toFixed(2) }; });
  console.log('camera at start  :', JSON.stringify(await cam()));
  await p.screenshot({ path: out + '/driver_near.png', timeout: 180000 });
  // drive out across the field (W), strafe (D), turn (Right arrow)
  await p.keyboard.down('KeyW'); await p.evaluate(() => window.__gp.advance(1.6)); await p.keyboard.up('KeyW');
  await p.keyboard.down('KeyD'); await p.evaluate(() => window.__gp.advance(0.8)); await p.keyboard.up('KeyD');
  for (let i = 0; i < 8; i++) await p.evaluate(() => window.__gp.renderFrame());
  console.log('camera after move:', JSON.stringify(await cam()));
  const h0 = await p.evaluate(() => window.__gp.state.sim.frame()[9]);
  await p.keyboard.down('ArrowRight'); await p.evaluate(() => window.__gp.advance(0.5)); await p.keyboard.up('ArrowRight');
  console.log('Right arrow turns the robot (rad):', ((await p.evaluate(() => window.__gp.state.sim.frame()[9])) - h0).toFixed(2));
  for (let i = 0; i < 8; i++) await p.evaluate(() => window.__gp.renderFrame());
  await p.screenshot({ path: out + '/driver_far.png', timeout: 180000 });
  // full-speed crash: reverse into the red (-x) wall
  await p.keyboard.down('ArrowLeft'); await p.evaluate(() => window.__gp.advance(0.5)); await p.keyboard.up('ArrowLeft');
  const crash = await p.evaluate(async () => {
    const g = window.__gp; let maxWall = 0, maxPitch = 0, minX = 9;
    const ev = new KeyboardEvent('keydown', { code: 'KeyS' }); dispatchEvent(ev);
    for (let i = 0; i < 80; i++) { g.advance(0.05); const f = g.state.sim.frame(); const nR = f[4], nE = f[5], nC = f[6]; const o = 7 + nR * 19 + nE * 14 + nC * 5 + 2; maxWall = Math.max(maxWall, Math.abs(f[o])); maxPitch = Math.max(maxPitch, Math.abs(f[7 + 16])); minX = Math.min(minX, f[7]); }
    dispatchEvent(new KeyboardEvent('keyup', { code: 'KeyS' }));
    return { minX: +minX.toFixed(3), maxWallFlex_mm: +(maxWall * 1000).toFixed(1), maxPitch_deg: +(maxPitch * 57.3).toFixed(1) };
  });
  console.log('crash into wall:', JSON.stringify(crash));
  await p.evaluate(() => { const g = window.__gp; g.camera.position.set(-2.2, 0.5, 0.9); g.camera.lookAt(-1.4, 0.1, 0); window.__gp.renderFrame(true); });
  await p.screenshot({ path: out + '/closeup.png', timeout: 180000 });
  await p.evaluate(() => { const g = window.__gp; g.camera.position.set(-0.4, 1.3, -1.7); g.camera.lookAt(-0.3, 1.0, 0); window.__gp.renderFrame(true); });
  await p.screenshot({ path: out + '/hive.png', timeout: 180000 });
  console.log('errors / CSP violations:', errs.length ? errs.slice(0, 8) : 'none');
  await b.close();
})();
