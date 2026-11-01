# AGENTOPS.md — how agents operate on GamePlan

## Inspect
1. `git status`, `git log --oneline | head`, read [IMPLEMENTATION_LEDGER.md](IMPLEMENTATION_LEDGER.md).
2. Build and run tests before touching anything (see BUILD_RULES.md); record failures you inherit.
3. For game behaviour, read the season package and the manual excerpt it cites; for hardware, the
   component's `provenance`.

## Implement
- Change the smallest layer that owns the behaviour (rules → `rules.cpp`, physics → `robot.cpp`/
  `world.cpp`, SDK API → shadow class, call cost → `timing_profiles.json`).
- New constants go into data files with provenance, not into code.
- New SDK surface: add the shadow, rebuild with `java runtime/tools/Build.java` (it compiles against
  the real SDK and fails on signature mismatches), add a fixture OpMode that uses it.

## Test
- `./build/gp-tests` (unit + system), `python3 tests/integration_test.py` (Java runtime in the loop).
- Any change to physics, rules, RNG use or the protocol: run `gp-sim verify` on a fresh recording and
  expect `EXACT`. If you changed behaviour on purpose, re-record.
- Cross-system changes need a causal-chain test (see `intake_load_reduces_drive_voltage_and_acceleration`).

## Debug
- Runtime hangs: the host watchdog prints the runtime's stderr. `jstack <pid>` the JVM; attach `gdb -p`
  to `gp-sim` (`pgrep -x gp-sim`) and sample `bt`. Typical causes: virtual clock not advancing, a
  missing `syncNow`, floating-time drift.
- Wrong robot motion: compare `robot_<i>.csv.gz` duties/currents with the OpMode's intent; check
  `"mirrored"` vs `setDirection`.
- Replay divergence: diff `hashes.csv` of original vs `verify/baseline`, then the first differing row
  of `frames.csv.gz` / `robot_*.csv.gz` / `loops_0.csv.gz`.

## Document
- Update the ledger row (status, tests, limitations) in the same commit.
- Put investigations and decisions in DEEP_ANALYSIS.md; durable facts in CONTEXT_STORE.md.
- Commit messages: what changed and why; never claim validation you did not run.
