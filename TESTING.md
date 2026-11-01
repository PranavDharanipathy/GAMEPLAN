# TESTING.md

| Suite | Command | Covers |
|---|---|---|
| Unit/system (21) | `./build/gp-tests` | motor derivation vs datasheet; drivetrain top speed/strafe; battery sag; **intake load lowers drivetrain voltage and acceleration**; servo current vs load; servo pair fold-back; OCV/R0 state dependence; resistor thermal time constant; backlash visible to encoder; season package = TU07 tables & hive calibration; match phases/timer/cues; same seed ⇒ same match, different seed ⇒ different; checkpoint restore; "do nothing" partner never powers an actuator; legality (illegal override, >8 motors); cube mass properties; STEP assembly + annotation matching; AprilTags move with HIVE tips; no ESD without charge; uncertainty level semantics |
| End-to-end (11) | `python3 tests/integration_test.py` | import fixture; Android-only exclusion; hardware discovery; full match with Java runtime; AUTO+TELEOP lifecycle; encoder drive distance; **EXACT re-execution**; **expensive SDK calls lengthen loops**; report; counterfactual with verified prefix |
| Manual validation done | see ledger | team project AthletechBiobuzzV1 import + `DriveIntakeShoot` match; AprilTag virtual detections in robot code; experiments; stress tests |

## Not yet covered (gaps)
Unreal host (cannot build here), Windows/macOS runtime path (CI configured), physical gamepads,
multi-runtime matches (two Java robots), FTCScout client, long-duration memory/perf soak.
