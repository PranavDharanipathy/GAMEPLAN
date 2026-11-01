# DATA_SCHEMA.md

| File | Schema id | Notes |
|---|---|---|
| `seasons/<id>/season.json` | `gameplan.season/1` | timing, cues, field geometry (frame defined inside), hives/cells/tags, flowers, staging, points, RP, robot rules, violations, officiating, provenance |
| `data/hardware/components.json` | `gameplan.components/1` | motors, gearboxes, servos, batteries, interconnects, controllers, servo power devices, sensors; every entry has `provenance{kind,confidence,source}` |
| `data/hardware/timing_profiles.json` | `gameplan.timing/1` | per-SDK-call cost (transaction / i2c / local / vision), lognormal spread, spikes, compute model |
| `examples/robots/*.robot.json` | `gameplan.robot/1` | chassis, drivetrain (wheels, rollers), battery, wiring, hubs (parent, wire, XT30), motors (component, gearbox, hub, port, `mirrored`, lead), servos (load), mechanisms, sensors, imperfections, `overrides` (experimental → legality) |
| `*.match.json` | — | season/components paths, seed, mode, fidelity, uncertainty `{all, levels, overrides}`, env, referee, runtime, robots (controller runtime/ai/idle, OpModes, gamepad script, AI personality/difficulty/instruction) |
| `*.experiment.json` | — | match, mode (deterministic/stochastic/adversarial), trials, variants (`patch` dotted paths; `a.b:literal.key`), adversarial params, objective, limits |
| package `package.json` | `gameplan.package/1` | project, source sha256, SDK version, classpath (shadow, sdk, libs, team) |
| package `compatibility.json` | `gameplan.compatibility/1` | dependency resolution, scan, compilation (excluded files + reasons), boundaries, Kotlin, substitutes |

## Match record directory
`header.json` (versions, season sha256, component sha256, robot config sha256, runtime READY incl.
OpModes/diagnostics, AI descriptions, world config, every sampled hidden parameter, platform),
`setup.json`, `frames.csv.gz` (truth: robots, elements, hives, scores; 50 Hz, 500 Hz near events),
`robot_<i>.csv.gz` (200 Hz: battery V/I/SoC/temp/OCV/R0/V_RC, hub bus V/servo rail/reset, per motor
duty/I/V/torque/rpm/winding T/limited, per servo cmd/angle/I/V, wheel N/slip, odometry/IMU error),
`loops_<i>.csv.gz` (each robot-program loop: start/end, transactions, Lynx/I2C/compute/overhead/vision µs),
`inputs_<i>.csv.gz` (gamepad changes, world time), `events.jsonl`, `hashes.csv` (world hash each
second), `result.json`, `report.html`. Truth (frames/robot) and robot-observable data (loops, inputs,
runtime telemetry) are kept separate. Retention: user-managed; the latest 30 are listed by the UI (planned).
