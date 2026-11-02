# IMPLEMENTATION_LEDGER.md

Statuses: Not started · Investigating · Designed · Implementing · Implemented (code exists, not tested)
· Tested (automated tests pass) · Validated (exercised end-to-end on representative inputs) · Blocked.

| Feature | Status | Location | Tests / evidence | Known limitations / remaining work |
|---|---|---|---|---|
| BIOBUZZ season package (TU07 + CAD) | Tested | `seasons/biobuzz-2027/` | `season_package_matches_manual_tu07` | some coefficients estimated (marked) |
| Versioned season system | Implemented | `season.cpp`, match header | header records id/version/sha256 | single season installed; no package manager UI |
| Rules engine: phases, timer, cues | Tested | `rules.cpp` | `match_timer_phases_and_cues` | — |
| Rules engine: scoring & RP | Validated | `rules.cpp` | full matches (AI + runtime) | assessment uses modelled physics of FLOWER/CELL |
| Violations + referee judgment | Tested | `rules.cpp` | integration matches, rationale strings | gaps listed in GAME_RULES_ENGINE.md |
| Coupled electrical network | Tested | `electrical.cpp` | 9 electrical tests incl. causal chain | REV limits recalled (verify) |
| Battery / thermal / fuse | Tested | `electrical.cpp` | OCV/R0, thermal tests | NiMH curve estimated |
| Servo electrical/mechanical | Tested | `electrical.cpp` | load/fold-back tests | servo specs mostly recalled/estimated |
| Mecanum chassis, slip, tipping | Tested | `robot.cpp` | top speed, strafe | reduced-order vertical dynamics |
| Backlash / imperfections | Tested | `robot.cpp` | backlash test | frame flex = load imbalance model only |
| Field contacts, elements, HIVE, FLOWER | Validated | `world.cpp` | matches, tag tests | element–robot contact is kinematic push |
| Uncertainty engine (4 levels) | Tested | `uncertainty.cpp` | `uncertainty_levels_semantics` | correlations only where coded |
| Sensors + corruption (no failures) | Implemented | `sensors.cpp` | used in runtime matches | no dedicated unit tests per sensor |
| Ball physics (spin, compliant contacts, physical intake path, ball-dependent shots) | Validated | `balls.cpp`, `test_balls.cpp` | 5 unit tests (bounce/squish, slide→roll, glancing collision momentum+spin, intake holds inside footprint + reverse spits out, shot vs ball size and spin); determinism re-verified | contact constants are estimates; cells hold balls in packed slots (not simulated inside) |
| Photoreal visuals (browser) | Validated (0.8.0) | `web/app/10_render.js`, `web/app/20_materials.js`, `web/app/30_field.js`, `web/app/40_robots.js`, `assets/field/*.glb`, `tools/cad_to_glb.py`, `tools/decimate_glb.py` | headless Chromium screenshots (driver, HIVE, robot close-ups, low, wide, perimeter; Low/High/Ultra presets; Natural vs Filmic colour) and a scene check (all CAD loads, 4 AprilTag stickers applied) | official CAD for field and elements (decimated to ~0.7 M triangles, 2 ball LODs keep all 26 holes); STEP colour and a finish per part type on every CAD part (0.7.0 and earlier silently used sanitized node names, so FLOWERS, ribs, cable ties, rails and the AprilTag stickers fell back to plain aluminium); static parts merged per material (~870 parts -> ~80 draw calls, ~10x faster frames); clear polycarbonate (HIVE skins, perimeter, guards) as a thin sheet: Fresnel reflections of both surfaces + slight absorption and haze, blended over everything behind it including other clear parts (no refraction pass); overhead rig of 8 spot fixtures, up to 8 casting contact-hardening (PCSS) soft shadows; GTAO; bloom; AgX tone mapping; camera grain/vignette; static night forest clearing; robots are representative goBILDA-style builds from the simulated geometry (perforated channel and plates at the real 24 mm hole pitch, upright drive motors on right-angle gearboxes, 10-roller mecanum wheels with the simulated roller handedness, compliant intake, riser feeder wheels, flywheel under an adjustable hood that follows the simulated launch angle, hubs, battery, wiring, webcam, signs) - not a team CAD; WebGL is not path-traced |
| Photoreal visuals (Unreal) | Implemented, unbuilt | `unreal/GamePlan/README.md`, `GamePlanMatchActor.cpp` | not compiled here; the actor advances the match by frame time, which can make its recordings re-execute only APPROXIMATE (see AGENTS.md) | full-res Nanite CAD import, Lumen/VSM/path tracer config, ball orientation + squish (WPO custom data), HIVE body rotation; needs an Unreal build to verify |
| Update notices (browser app) | Implemented | `web/app/90_updates.js`, `VERSION`, `RELEASES.json`, `tools/release.py` | headless test of every state: no runtime, same version, newer (bar), hidden during a match, newer arriving mid-match shown after the final buzzer, × dismissal remembered per version, About shows/offers update, `required` hides × | live record in the artifact db (`releases/latest`), needs signed-in viewers; Update now reloads onto the live version (falls back to a 'close and reopen' note if the viewer keeps the old one); desktop app has no distribution channel yet |
| Playable browser match | Validated (0.8.0) | `web/app/80_game.js`, `web/app/60_input.js`, `apps/web/gp_web.cpp` (built-in TeleOp), `web/playtest.js`, `web/playtest_browser.js` | scripted human playtest: collects POLLEN, ready shots score most attempts and tip the HIVE; browser test: keyboard (incl. a rebound key) and touch drive the robot, shooter LED + robot status strip, AUTO hint, full match to the final banner and report | shooter assist uses the true pose (a team's would use odometry); auto-aim can be switched off (the gate then opens when the wheel and hood are ready); browser runs the built-in TeleOp, not team Java code |
| Browser game shell: main menu, match setup, settings, pause, post-match | Validated (0.8.0) | `web/index.template.html`, `web/app/85_ui.js`, `web/app/80_game.js`, `web/app/00_core.js`, `apps/web/gp_web.cpp` (`demo`, `aimAssist`) | browser test: menu over a live 4-AI match, settings preset/toggles apply and persist, key rebinding persists and drives, Esc pause/resume, pause -> leave -> menu, final banner -> report -> Play again (next seed); phone: Quick practice, touch drive, pause button; simulated gamepad: d-pad/A/B menus, Start pause/resume, stick drive; no page errors | settings live in this browser only (localStorage); gamepad menu navigation (d-pad/A/B/Start) verified with a simulated controller, not a physical one |
| Browser asset loading under the viewer's security policy | Validated | `tools/build_web_page.py` (GLB as base64 JSON), `web/app/30_field.js` (`fetchGLB`, fallbacks), `web/serve_csp.py`, `web/csp_test.js` | under a viewer-equivalent CSP: data: fetch refused, all 871 CAD parts load, no violations | 0.6.x shipped glTF with data: buffers, which the viewer blocked: users saw no HIVE, FLOWERS, barrier or ball holes |
| Driver controls, head-tracking camera, standing spot | Validated | `web/app/50_view.js`, `web/app/60_input.js`, `apps/web/gp_web.cpp` | browser tests: WASD drives, arrows turn, camera follows the robot and tilts down when it is close; Fire (hold) auto-aims and releases ready shots (scripted driver scores most shots) | |
| Barrier impacts | Validated | `world.cpp` (wall flex states), `robot.cpp` (contact forces into rocking) | unit test: full-speed crash rocks the robot, bounces back, flexes the wall ~1 cm and rings down; browser: thud + visible flex | flex stiffness is an estimate |
| Virtual AprilTag perception | Validated | `sensors.cpp`, `world.cpp` (`clusterFrame`), `SimAprilTagProcessor` | default library -> `AprilTagClusterDetection`s (up to 100 %); custom library -> singles with pose; cluster geometry unit test vs SDK metadata | no robotPose/rawPose; ftcPose pitch/roll 0 |
| ESD / faults / brownout | Tested | `world.cpp` | `esd_never_triggers_without_charge…` | magnitudes estimated |
| FTC runtime (hybrid real SDK) | Validated | `runtime/` | integration tests; team OpMode match | see FTC_SDK.md gaps |
| Loop-time cost model | Validated | `VirtualRuntime`, `timing_profiles.json` | 25.7 vs 6.9 ms loops | µs values not yet calibrated |
| Project importer + compat report | Validated | `Importer.java` | fixture + AthletechBiobuzzV1 | Gradle parsing is regex-based |
| Kotlin support | Validated | importer | team's 4 `.kt` files compiled | compiler fetched online (bundle for offline) |
| Source read-only + watch/rebuild | Implemented | importer `--watch` | git clean check | watch mode not covered by tests |
| Sandbox | Implemented (Linux) | `link.cpp`, `ClassRewriter`, `SandboxProbe` fixture | Landlock ABI 7 + private net namespace verified: hostile probe denied home/repo reads, writes outside scratch, TCP; virtual /sdcard writable | mac/win OS sandbox missing |
| Recording | Validated | `recorder.cpp` | all matches | retention UI missing |
| Deterministic re-execution | Validated | `match.cpp` | `verify` EXACT (AI and Java runtime) | cross-OS not bit-identical (detected) |
| Checkpoint/restore (host) | Tested | `World::snapshot/restore` | `checkpoint_restore…` | AI private state not in snapshot |
| Counterfactual (numerical, no AI) | Validated | `runCounterfactual` | integration test | full re-execution cost |
| Post-match report & attribution | Validated | `analysis.cpp` | report generated, checked by integration | HTML, not Unreal UMG |
| Driver vs robot analysis | Implemented | `analysis.cpp` | report section | metrics basic |
| AI opponents/partners, personalities, difficulty | Validated | `ai.cpp` | matches score/tip/park | simple navigation; no learning yet |
| Natural-language partner instruction | Tested | `StrategyDirectives` | "do nothing" test | rule-based phrase set; unknown text reported |
| Experiments (det/stoch/adversarial) + estimates | Validated | `experiment.cpp` | battery study, adversarial run | pause/cancel via control file only |
| Stress tests | Validated | `runStressTests` | run on reference robot | scenario list fixed |
| Legality | Tested | `legality.cpp` | legality test | inspection-only rules not checkable |
| CAD: STEP assembly, mesh mass props, materials, annotations | Tested | `cad.cpp` | cube + STEP tests; field STEP parsed (Python prototype) | no B-rep tessellation |
| Browser build (core → WebAssembly/WASI, 3D driver view, gamepad/keyboard, HUD, post-match report) | Validated | `apps/web/`, `web/`, `tools/build_web*.py` | Node full-match test (`web/node_test.js`); headless Chromium render + drive | built-in TeleOp instead of team Java code (no JVM in browser); single robot model |
| Unreal host (pawn, input, match actor, HUD) | Implemented (unbuilt) | `unreal/GamePlan` | **none** — UE unavailable here | must be compiled & validated |
| Physical gamepads | Implemented (unbuilt) | `GamePlanInput.cpp` | none | calibration UI missing |
| Replay viewer (browser) | Validated (0.9.0) | `web/app/88_replay.js`, `core/src/view.cpp` (shared view frames), `core/src/replay.cpp` + `gp-sim replay-export` | browser test: your match is captured (20 frames/s); after seeking, the drawn robot pose equals the captured frame; 2x playback, event jump, Esc back to the report; a desktop export (AI match) opens from a file with its clock and score; integration test: export of the Java-runtime match re-executes EXACT and the file matches its header | browser replays live in memory until the next match (the viewer blocks downloads, so saving one needs the desktop CLI); live-match events are derived from frames (scores, HIVE tips, phases, fouls), desktop exports also carry launches, intakes, collisions; the Unreal replay actor is not written |
| CI (Linux/macOS/Windows) | Implemented | `.github/workflows/ci.yml` | not yet run | Windows integration skipped |
| FTCScout / team imitation | Implemented | `tools/ftcscout_imitate.py`, `experiment.cpp` (FTCScout-named components), `match.cpp` (`ai.imitate`, `ai.weights`) | real BIOBUZZ OPR for teams 8807 (21 pts: matched within 2σ except auto park) and 21229 (151 pts: unreachable, reported); integration check offline | imitates scoring profile only; AI ceiling with the generic launcher ≈ 70 pts solo, so top teams are reported unreachable; defense not calibrated |
| Learning bots | Not started | — | — | ROADMAP |
| Video ingestion / calibration | Not started | — | — | ROADMAP 6–7 |
| Multiplayer / spectators | Not started | — | — | ROADMAP 9 |
| Workspaces, versions, variants, robot library | Not started | — | records carry hashes | ROADMAP 10 |
| Installer / updater | Not started | — | — | ROADMAP 11 |

## Audit 1 — requirements completeness (2026-11-01)
Re-read every part of the brief against code, docs, tests. Covered with real implementations: season
package/rules (I–II), core architecture/fixed step/headless (III), real robot-code execution, import,
lifecycle, SDK adapters, compatibility honesty, source preservation (V), legality incl. illegal
experimental robots (VII), physics + imperfections + field provenance + fidelity modes + headless/
accelerated + reproducibility (VIII), uncertainty engine + 3 experiment modes (IX), coupled electrical
with servo current, battery sag, topology, protection, ESD (X, post-match only), sensors with
corruption-not-failure and virtual vision (XI), loop-time costs affecting behaviour (XII), AI with
personalities, difficulty, partner instructions (XIII), recording/replay data/counterfactual without
AI (XV), experiments with estimates (XVII), sandbox (XX, partial).
Missing or partial (recorded above, not hidden): Unreal build/validation, driver UI screens, Unreal replay
viewer, CAD tessellation and digital-twin reconstruction from geometry, electrical config editor UI,
video ingestion/calibration, learning bots, multiplayer, workspaces/versioning UI, robot
library/consent, installer, OS sandboxes on macOS/Windows, AprilTag robotPose.

## Audit 2 — contradictions and architecture
- Competition AUTO vs practice free-drive: separate `competitionMode`; gamepads zeroed in AUTO in
  competition; practice results labelled in header/HUD. ✔
- Driver POV vs follow cameras: driver pawn has no reference to robots; spectator views are separate (planned). ✔
- No electrical HUD vs background simulation: HUD shows timer/phase/score/connection only; electrical
  data only in records/report. ✔
- Real source vs virtual hardware: team bytecode runs; only Android-bound SDK classes are shadowed;
  excluded files reported, never rewritten. ✔
- Camera simulation vs no detection pipeline: detections generated from geometry; raw-frame
  processors receive no frames and are reported. ✔
- Local compute vs online multiplayer: planned host-authoritative design sends inputs only. ✔ (design)
- AI learning vs source modification: AI changes only its own parameters; no code path writes team source. ✔
- Illegal robots vs legality: overrides simulate but flag `illegal`/`experimental`. ✔
- Full physics vs accelerated headless: one World; acceleration only skips rendering. ✔
- Old season reproducibility vs updates: package version + sha256 in every record. ✔
- Found and fixed during this audit: G403/G404 falsely called on DS-stop latency and on AI servos
  holding PWM after stop (SDK stop semantics now mirrored).

## Audit 3 — functionality and test integrity
Ran `gp-tests` (21/21) and `tests/integration_test.py` (11/11). Looked for inert/fake features:
- Fixed: servo dynamics were never integrated in matches (hood stuck); encoder reset lost when
  STOP_AND_RESET and RUN_TO_POSITION arrived in one step; Maven 429 silently dropped dependencies;
  replay divergence causes listed in DEEP_ANALYSIS D5.
- Remaining honest gaps: PortController velocity PIDF uses approximate REV units; `DcMotorController`
  proxies return defaults; MonitorOpModeHook-style SDK hooks not executed; Unreal code uncompiled;
  Windows runtime path untested; CI not yet executed; timing µs uncalibrated.
