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

## Team imitation (FTCScout)
`python3 tools/ftcscout_imitate.py <team> [--event CODE] [--estimate]` fetches the team's BIOBUZZ
statistics, calibrates an AI against its OPR components (≈2 min, local) and writes
`profiles/teams/<team>.team.json` plus the raw API response. Use it with
`"controller": "ai", "ai": {"imitate": "profiles/teams/<team>.team.json"}`. Read the profile's
`calibration.unreachable` before trusting it.

## Releasing (browser app)
1. Bump `VERSION` and add the newest entry to `RELEASES.json` (version, date, notes, `required`).
2. `python3 tools/release.py` builds the site and prints the `releases/latest` record.
3. Publish `build-web/site/index.html` to the same artifact URL (keep the `db` capability declaration).
4. Write the printed record to the artifact database document `releases/latest`.
Running copies show an update bar between matches (never during one) with Update now and ×;
About on the setup screen always offers Check for updates / Update now. `required: true` hides ×.

## Pull requests and merging
Follow [MERGE.md](MERGE.md): feature brief on every feature branch, integration on `integrate/<date>`,
verification gates G1–G10, merge report, then fast-forward `main` and delete merged branches.

