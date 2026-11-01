# GamePlan — FTC BIOBUZZ simulator

GamePlan simulates *FIRST* Tech Challenge matches for the 2026–2027 **BIOBUZZ** season with a
digital twin of a team's robot, running the team's **real FTC Java code** against simulated
hardware, with coupled mechanical / electrical / sensor / loop-timing models, an executable rules
engine, AI opponents and partners, match recording, exact replay, counterfactual branching and
local batch experiments.

| Part | Where | State |
|---|---|---|
| Simulation core (C++17) | `core/` | built and tested here |
| Headless CLI `gp-sim` | `apps/gpsim/` | built and tested here |
| FTC runtime (Java, real SDK 12.0 jars + shadows) | `runtime/` | built and tested here |
| Project importer | `runtime/src/bridge/.../tools/Importer.java` | tested on a fixture and on a real team project |
| BIOBUZZ season package | `seasons/biobuzz-2027/` | from TU07 manual + official field CAD |
| Unreal Engine host | `unreal/GamePlan/` | **written, not built** (no Unreal Engine in the dev container) |

Honest status per feature: [IMPLEMENTATION_LEDGER.md](IMPLEMENTATION_LEDGER.md).

## Quick start (Linux/macOS; Windows: same commands in a Developer prompt)

```bash
cmake -S . -B build && cmake --build build -j
```
```bash
java runtime/tools/Build.java
```
```bash
./build/gp-tests && python3 tests/integration_test.py
```
```bash
./build/gp-sim run examples/matches/ai-vs-ai.match.json --record out/matches/demo
```
```bash
java -cp runtime/build/gameplan-runtime.jar org.gameplan.runtime.tools.Importer /path/to/YourFtcProject --out out/packages/yours
```
```bash
./build/gp-sim report out/matches/demo
```

Read [AGENTS.md](AGENTS.md) before changing anything.
