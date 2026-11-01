# AGENTS.md — rules for every coding agent (and human) working on GamePlan

Read this first, then [AGENTOPS.md](AGENTOPS.md) (how to work), [BUILD_RULES.md](BUILD_RULES.md)
(how to build/test), [IMPLEMENTATION_LEDGER.md](IMPLEMENTATION_LEDGER.md) (what is really done),
[CONTEXT_STORE.md](CONTEXT_STORE.md) (facts), [DEEP_ANALYSIS.md](DEEP_ANALYSIS.md) (decisions).

## Repository map

| Path | What | Language |
|---|---|---|
| `core/include/gameplan/`, `core/src/` | Simulation core: `World`, `Robot`, electrical network, sensors/vision, BIOBUZZ rules, AI, recorder, runtime link, match orchestration, analysis, experiments, legality, CAD | C++17 |
| `core/tests/` | Dependency-free unit/system tests (`gp-tests`) | C++ |
| `apps/gpsim/` | Headless CLI (`gp-sim run/verify/counterfactual/report/experiment/stress/legality/...`) | C++ |
| `runtime/src/bridge/` | Runtime process: protocol, virtual Control Hub (`VirtualRuntime`), sandbox class loader + bytecode rewriter, importer, Maven resolver | Java 11+ |
| `runtime/src/shadow/` | Shadows of Android-bound FTC SDK classes + virtual devices (compiled against the real SDK jars) | Java |
| `runtime/tools/Build.java` | Runtime build (provisions SDK jars from Maven Central, SHA-1 verified) | Java |
| `runtime/fixtures/` | Representative FTC projects used by tests | Java |
| `seasons/<id>/season.json` | Versioned season packages (geometry, rules, timing, points, cues, tags, provenance) | JSON |
| `data/hardware/` | Component library (motors, gearboxes, servos, batteries, hubs) and SDK call timing profile | JSON |
| `examples/` | Robot descriptions, match setups, gamepad scripts, experiments | JSON |
| `unreal/GamePlan/` | Unreal Engine project + `GamePlanSim` plugin embedding the core | C++ (UE) |
| `tests/integration_test.py` | End-to-end tests (Java runtime in the loop) | Python (stdlib) |

## Architectural principles (non-negotiable)

1. **One simulation.** `gp::World` is the only physics/electrical/rules implementation. Unreal renders
   it; headless and batch runs use it. Never add a "fast" second model for batch runs.
2. **Virtual time.** Simulation advances on the core's fixed step (`WorldConfig::dt`). Rendering frame
   rate never changes physics. Robot-code time is virtual: every SDK call is charged its modelled cost.
3. **Real robot code.** Team code executes as compiled bytecode on the real FTC SDK classes where they
   work without Android, and on shadows (same public API) where they don't. Never replace team logic
   with an imitation. Anything not executed must be reported in the compatibility report.
4. **Source is read-only.** The importer never writes inside an imported project. Generated code,
   compiled packages and calibration data live in GamePlan's own directories. AI/experiments never edit
   team source.
5. **Ground truth stays hidden.** Robot code and AI only see modelled sensors / perception. Hidden
   parameters (battery resistance, tip threshold, sampled imperfections) are never exposed.
6. **Provenance or it doesn't ship.** Every physical constant carries `provenance` (`datasheet-verified`,
   `datasheet-recalled`, `derived`, `estimate`) and a confidence. Never present an estimate as official.
7. **Season packages are immutable once released.** Rule changes ship as a new package version; matches
   record package id/version/hash.
8. **Determinism.** Each subsystem draws from its own named RNG stream (`Rng::derive`). New randomness
   must use a new named stream, never a shared global RNG, `rand()`, wall clock, or static state.
9. **Honest status.** Never mark a feature complete because a class or button exists. Update the ledger.

## Hard constraints learned the hard way

- `World::time()` is `steps * dt` — never accumulate floating time (it caused a lockstep livelock).
- Match end detection runs on an absolute 50 ms grid so results don't depend on `advance()` chunking.
- Recorded gamepad inputs are stamped in **world time** and injected at the exact exchange time on replay.
- Motors physically mirrored on the robot use `"mirrored": true` in the robot description; the SDK
  `setDirection(REVERSE)` in team code is a separate, software-side inversion. Don't conflate them.
- Shadow classes must compile against the real SDK jars (Build.java enforces it). Nested types of a
  shadowed class must be re-declared in the shadow.
- The runtime's stdout is the protocol channel; robot `System.out` is redirected to LOG frames.
- `pkill -f` patterns can match your own shell command line. Use `pgrep -x`.

## Conventions

- C++17, no exceptions across the Unreal boundary without `bEnableExceptions`, no global mutable state.
- Java: bridge 11+, shadows compiled `--release 11`, team code `--release 8` (Android parity).
- SI units internally; field frame documented in `seasons/biobuzz-2027/season.json` (`frame`).
- New season → new directory `seasons/<id>/`, new rules engine version string.
