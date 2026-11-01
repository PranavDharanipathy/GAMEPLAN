# ARCHITECTURE.md

```
                 ┌──────────────────────── Unreal host (unreal/GamePlan) ───────────────────────┐
 physical pads → │ FGamePlanInput → MatchRunner::setLiveGamepads   ADriverStationPawn (eyes in     │
                 │ AGamePlanMatchActor renders World state          ALLIANCE AREA, never on robot)  │
                 │ AGamePlanHUD: timer/phase/score/connection only                                  │
                 └───────────────────────────────┬──────────────────────────────────────────────────┘
                                                 │ links gameplan_core (same library as gp-sim)
 ┌────────────────────────────── gameplan_core (C++17) ────────────────────────────────────────────┐
 │ MatchRunner ── schedule (DS INIT/START/STOP), lockstep with RuntimeLinks, recorder, hashing       │
 │ World (dt fixed) ── Robot ×N ── PortController (hub firmware) → ElectricalSystem.solve           │
 │   │                 │            battery(OCV,R0,RC,thermal,fuse) → main path → hub feeds → buses │
 │   │                 │            motors (EMF, R(T), current limit), servo rails (pair/total fold)│
 │   │                 └─ GearedJoint (backlash contact) → wheels (mecanum slip) → chassis + tipping│
 │   ├─ elements (POLLEN/NECTAR ballistics, intake, launch, FLOWER stacks, CELL capture)            │
 │   ├─ HIVE bistable tip model, humans (NECTAR entry rules), ESD/fault model                       │
 │   ├─ SensorSuite (noise/bias/drift/latency/corruption) + virtual AprilTag perception             │
 │   ├─ MatchRules (BIOBUZZ scoring, violations, RefereeModel)  · AIController ×N                   │
 │ Recorder · analysis (report, attribution) · experiments · legality · CAD                         │
 └───────────────────────────────┬──────────────────────────────────────────────────────────────────┘
                                 │ stdin/stdout frames, lockstep virtual time (runtime/PROTOCOL.md)
 ┌─────────── FTC runtime JVM (sandboxed child process) ─────────────────────────────────────────────┐
 │ RuntimeMain (gp-main: protocol) · VirtualRuntime (clock, per-call costs, bulk cache, handoff)     │
 │ SandboxLoader + ClassRewriter → [GamePlan shadows][real FTC SDK jars][libs][team classes]         │
 │ OpModeThread runs the team's OpMode                                                               │
 └───────────────────────────────────────────────────────────────────────────────────────────────────┘
```

## Ownership of state
- Physical truth: `World` (+ `Robot`, `ElectricalSystem`, elements, hives). Snapshot/restore complete
  for host state (`World::snapshot`).
- Robot-observable state: produced by `SensorSuite::fill` / perception only at exchange time.
- Robot-program state: inside the runtime JVM (not serialisable → counterfactuals re-execute).

## Step order (one `World::step`, dt = 1 ms real-time / 0.25 ms engineering)
AI decisions → mechanism loads → per robot: firmware → electrical solve → servo dynamics → wheel
contact → joints → chassis → slow electrical states → robot/field/robot contacts → element
interactions (intake/launch/push) → element physics (FLOWER/CELL capture) → HIVE tips → humans →
faults → sensors/vision capture → rules → time.

## Lockstep
The runtime asks the host to advance to its virtual clock before fresh reads (see PROTOCOL.md).
The host advances physics to the minimum target of all runtimes, applies their timestamped writes
inside the step that contains each timestamp, and answers with sensors sampled at that time.

## Navigation of the product (planned UI destinations)
Drive (match screen) · Robot Lab (robot description, CAD, annotations, legality) · Electrical ·
Opponents/Scout · Match History/Replay · Engineering Analysis · Experiments · Settings. Today the
engineering workspace is the generated HTML report; Unreal UMG screens are future work.
