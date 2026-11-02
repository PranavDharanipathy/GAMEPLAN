// Browser playtest of the full app (serve build-web/site on :8765): menus, settings, a match driven with
// the keyboard, pause / leave, the end-of-match flow, touch controls on a phone, and menu/match control
// with a (simulated) gamepad.
// Usage: node web/playtest_browser.js <screenshot dir>
const { chromium, devices } = require('playwright');
const out = process.argv[2];
(async () => {
  const b = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--autoplay-policy=no-user-gesture-required'] });
  const errs = [];
  const visible = (p, id) => p.$eval('#' + id, (e) => !e.hidden);
  // ---------- desktop, keyboard ----------
  let p = await b.newPage({ viewport: { width: 960, height: 540 } });
  p.on('pageerror', (e) => errs.push('desktop: ' + e.message));
  await p.addInitScript(() => { window.__gpPause = true; });   // frames are stepped by the test (software GL is slow)
  await p.goto('http://localhost:8765/index.html');
  await p.waitForFunction(() => window.__gp && window.__gp.ready(), null, { timeout: 180000, polling: 500 });
  console.log('main menu shown, AI match behind it:', await visible(p, 'scr-menu'), await p.evaluate(() => window.__gp.state.match));
  // settings: preset, a toggle, a key rebind, persisted
  await p.click('#scr-menu [data-go="settings"]');
  await p.selectOption('#set-preset', 'balanced');
  console.log('preset balanced applies:', await p.evaluate(() => ({ preset: window.__gp.settings.preset, ao: window.__gp.settings.ao, shadows: window.__gp.settings.shadowLights })));
  await p.click('#setTab2');
  await p.click('#setPanel .kb');          // Drive forward, first key
  await p.keyboard.press('KeyT');
  console.log('rebind forward to T:', await p.evaluate(() => window.__gp.settings.binds.forward.join(',')), 'saved:', await p.evaluate(() => JSON.parse(localStorage.getItem('gp.settings.v1')).binds.forward[0]));
  await p.click('#scr-settings [data-back]');
  // competition match from the setup screen
  await p.click('#mPlay');
  await p.click('#startBtn');
  await p.waitForFunction(() => window.__gp.state.running, null, { timeout: 60000, polling: 500 });
  await p.evaluate(() => window.__gp.advance(5));
  console.log('competition AUTO hint:', await p.$eval('#hint', (e) => !e.hidden && e.textContent.slice(0, 60)));
  await p.keyboard.press('Escape');
  console.log('Esc pauses (menu shown, sim frozen):', await p.evaluate(() => window.__gp.state.paused && window.__gp.ui.top() === 'pause'));
  await p.keyboard.press('Escape');
  console.log('Esc again resumes:', await p.evaluate(() => !window.__gp.state.paused && !window.__gp.ui.top()));
  await p.click('#pauseBtn'); await p.click('#pLeave');
  console.log('Leave match -> main menu:', await visible(p, 'scr-menu'), !(await p.evaluate(() => window.__gp.state.running)));
  // practice match: drive with the rebound key, shoot
  await p.click('#mPlay'); await p.click('#mPrac'); await p.click('#startBtn');
  await p.waitForFunction(() => window.__gp.state.running && window.__gp.state.mode === 'practice', null, { timeout: 60000, polling: 500 });
  await p.evaluate(() => window.__gp.advance(3.5));
  const pos = () => p.evaluate(() => { const f = window.__gp.state.sim.frame(); return [f[7], f[8], f[9], f[7 + 17]]; });
  const p0 = await pos();
  await p.keyboard.down('KeyT'); await p.evaluate(() => window.__gp.advance(1.5)); await p.keyboard.up('KeyT');
  const p1 = await pos();
  console.log('T (rebound) drives the robot (m):', Math.hypot(p1[0] - p0[0], p1[1] - p0[1]).toFixed(2));
  await p.keyboard.down('Space'); await p.evaluate(() => window.__gp.advance(3));
  const ledSeen = await p.evaluate(() => window.__gp.robots[0].led.material.emissive.getHexString());
  console.log('shooter LED / robot HUD while Space held:', (await pos())[3], ledSeen, await p.$eval('#rhState', (e) => e.textContent));
  await p.keyboard.up('Space');
  await p.evaluate(() => window.__gp.renderFrame(true));
  await p.screenshot({ path: out + '/desktop_play.png', timeout: 180000 });
  // play the rest of the match out: final banner, then the report with Play again
  await p.evaluate(() => window.__gp.advance(170));
  console.log('final buzzer banner:', await visible(p, 'finalBanner'), !(await p.evaluate(() => window.__gp.state.running)));
  await p.waitForFunction(() => window.__gp.ui.top() === 'report', null, { timeout: 15000, polling: 250 });
  console.log('report opens:', await visible(p, 'scr-report'), await p.$eval('#reportBody h1', (e) => e.textContent));
  await p.screenshot({ path: out + '/desktop_report.png', timeout: 180000 });
  // replay of the match just played: seek, play at 2x, jump to an event, leave back to the report
  await p.click('#rWatch');
  const rp = await p.evaluate(() => {
    const g = window.__gp, R = g.replay, s = document.getElementById('rpSeek');
    const before = { m: g.state.match, frames: R.frames.length, marks: document.querySelectorAll('#rpMarks .mk').length };
    s.value = 500; s.dispatchEvent(new Event('input'));
    const cap = g.capture.frames, f = cap.reduce((b, fr) => (Math.abs(fr[0] - R.t) < Math.abs(b[0] - R.t) ? fr : b), cap[0]);
    const pos = g.robots[0].g.position, err = Math.hypot(pos.x - f[7], pos.y - f[8]);
    const t0 = R.t; R.speed = 2; R.playing = true; g.replayTick(1); const adv = R.t - t0;
    return Object.assign(before, { seekPoseError_m: +err.toFixed(3), advance2x: +adv.toFixed(2) });
  });
  console.log('replay of this match (frames, marks, pose error after seek, 1 s at 2x):', JSON.stringify(rp));
  await p.keyboard.press('Escape');
  console.log('Esc leaves the replay to the report:', await p.evaluate(() => window.__gp.ui.top() === 'report' && window.__gp.state.match === 'over'));
  await p.click('#rAgain');
  await p.waitForFunction(() => window.__gp.state.running, null, { timeout: 60000, polling: 500 });
  console.log('Play again starts the next seed:', await p.evaluate(() => window.__gp.state.setup.seed));
  await p.close();
  // ---------- phone, touch ----------
  const ctx = await b.newContext({ ...devices['Pixel 7'], deviceScaleFactor: 1 });
  p = await ctx.newPage();
  p.on('pageerror', (e) => errs.push('phone: ' + e.message));
  await p.addInitScript(() => { window.__gpPause = true; });
  await p.goto('http://localhost:8765/index.html');
  await p.waitForFunction(() => window.__gp && window.__gp.ready(), null, { timeout: 180000, polling: 500 });
  await p.evaluate(() => window.__gp.setSetting('preset', 'low'));
  await p.screenshot({ path: out + '/phone_menu.png', timeout: 180000 });
  await p.tap('#mQuick');
  await p.waitForFunction(() => window.__gp.state.running, null, { timeout: 60000, polling: 500 });
  await p.evaluate(() => window.__gp.advance(3.5));
  console.log('touch controls visible:', await p.$eval('#touch', (e) => !e.hidden));
  const q0 = await p.evaluate(() => { const f = window.__gp.state.sim.frame(); return [f[7], f[8]]; });
  const box = await p.$eval('#stickL', (e) => { const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; });
  const cdp = await ctx.newCDPSession(p);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: box.x, y: box.y }] });
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: box.x, y: box.y - 60 }] });
  await p.evaluate(() => window.__gp.advance(1.5));
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
  const q1 = await p.evaluate(() => { const f = window.__gp.state.sim.frame(); return [f[7], f[8]]; });
  console.log('touch stick drives the robot (m):', Math.hypot(q1[0] - q0[0], q1[1] - q0[1]).toFixed(2));
  await p.tap('#pauseBtn');
  console.log('pause button on touch:', await p.evaluate(() => window.__gp.ui.top() === 'pause'));
  await p.tap('#pResume');
  await p.evaluate(() => window.__gp.renderFrame(true));
  await p.screenshot({ path: out + '/phone_play.png', timeout: 180000 });
  await ctx.close();
  // ---------- a replay file exported by the desktop CLI (needs build/gp-sim from gate G1) ----------
  const path = require('path'), fs = require('fs'), { execFileSync } = require('child_process');
  const root = path.join(__dirname, '..'), sim = path.join(root, 'build', 'gp-sim'), rec = path.join(root, 'out', 'browser-replay');
  if (fs.existsSync(sim)) {
    execFileSync(sim, ['run', path.join(root, 'examples/matches/ai-vs-ai.match.json'), '--record', rec], { cwd: root });
    execFileSync(sim, ['replay-export', rec], { cwd: root });
    p = await b.newPage({ viewport: { width: 960, height: 540 } });
    p.on('pageerror', (e) => errs.push('replay file: ' + e.message));
    await p.addInitScript(() => { window.__gpPause = true; });
    await p.goto('http://localhost:8765/index.html');
    await p.waitForFunction(() => window.__gp && window.__gp.ready(), null, { timeout: 180000, polling: 500 });
    await p.click('#scr-menu [data-go="replays"]');
    await p.setInputFiles('#rpFile', path.join(rec, 'replay.gpreplay'));
    await p.waitForFunction(() => window.__gp.state.match === 'replay' || /Could not/.test(document.getElementById('rpFileState').textContent), null, { timeout: 60000, polling: 250 });
    console.log('desktop replay file opens:', JSON.stringify(await p.evaluate(() => ({ m: window.__gp.state.match, frames: window.__gp.replay.frames.length, robots: window.__gp.robots.length, title: document.getElementById('rpTitle').textContent, err: document.getElementById('rpFileState').textContent }))));
    await p.evaluate(() => { const g = window.__gp; g.seekTo(g.replay.start + 75); });
    console.log('clock at 1:15 of match time:', await p.$eval('#timer', (e) => e.textContent));
    await p.close();
  } else console.log('desktop replay file: not run (build/gp-sim missing; build gate G1 first)');
  // ---------- gamepad (a simulated controller: navigator.getGamepads stubbed) ----------
  p = await b.newPage({ viewport: { width: 960, height: 540 } });
  p.on('pageerror', (e) => errs.push('gamepad: ' + e.message));
  await p.addInitScript(() => {
    window.__gpPause = true;
    const pad = { id: 'Test pad', index: 0, connected: true, mapping: 'standard', axes: [0, 0, 0, 0], buttons: Array.from({ length: 17 }, () => ({ pressed: false, value: 0 })) };
    window.__pad = pad; navigator.getGamepads = () => [pad];
  });
  await p.goto('http://localhost:8765/index.html');
  await p.waitForFunction(() => window.__gp && window.__gp.ready(), null, { timeout: 180000, polling: 500 });
  const press = (i) => p.evaluate((i) => { window.__pad.buttons[i].pressed = true; window.__gpPoll(0.016); window.__pad.buttons[i].pressed = false; window.__gpPoll(0.016); }, i);
  const focused = () => p.evaluate(() => document.activeElement && document.activeElement.id);
  await press(13); await press(13);
  console.log('d-pad moves menu focus, A opens:', await focused(), await (async () => { await press(0); return p.evaluate(() => window.__gp.ui.top()); })());
  await press(15); const tab = await p.evaluate(() => document.activeElement.textContent);
  await press(1);
  console.log('d-pad right switches settings tab, B goes back:', tab, await p.evaluate(() => window.__gp.ui.top()));
  await p.evaluate(() => document.getElementById('mQuick').focus()); await press(0);
  await p.waitForFunction(() => window.__gp.state.running, null, { timeout: 60000, polling: 500 });
  await press(9); const paused = await p.evaluate(() => window.__gp.ui.top());
  await press(9);
  console.log('Start pauses and resumes:', paused, await p.evaluate(() => !window.__gp.state.paused));
  await p.evaluate(() => { window.__gp.advance(3.5); window.__pad.axes[1] = -1; window.__gp.advance(1.0); window.__pad.axes[1] = 0; });
  console.log('left stick drives, hint names the pad button:', await p.evaluate(() => window.__gp.state.sim.frame()[7].toFixed(2)), await p.$eval('#hint', (e) => e.textContent.slice(0, 20)));
  console.log('page errors:', errs.length ? errs : 'none');
  await b.close();
})();
