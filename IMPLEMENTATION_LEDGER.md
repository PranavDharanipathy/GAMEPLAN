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
| Photoreal visuals (browser) | Implemented | `web/app.part.html`, `assets/field/*.glb`, `tools/cad_to_glb.py`, `tools/decimate_glb.py` | headless Chromium screenshots (driver, overhead, robot close-up, HIVE) | official CAD for field and elements (decimated to ~0.7 M triangles, 2 ball LODs keep all 26 holes); STEP part colours; real tag36h11 stickers; PBR; overhead arena rig (8 spot fixtures on a truss + area fill, rig-derived reflections) lighting the field and drive areas while the surroundings stay dark; clear polycarbonate with screen-space refraction, frosted thin HIVE skins passing some light; static night forest clearing (procedural trees, undergrowth, terrain, starry sky, fog); soft shadows + GTAO (Ultra); balls posed/spun/squished per simulated state and visible inside robots; robots are parametric open frames (no team CAD yet); WebGL is not path-traced |
| Photoreal visuals (Unreal) | Implemented, unbuilt | `unreal/GamePlan/README.md`, `GamePlanMatchActor.cpp` | not compiled here | full-res Nanite CAD import, Lumen/VSM/path tracer config, ball orientation + squish (WPO custom data), HIVE body rotation; needs an Unreal build to verify |
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
| Replay viewer UI | Not started | — | data supports scrubbing | Unreal replay actor to write |
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
Missing or partial (recorded above, not hidden): Unreal build/validation, driver UI screens, replay
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
