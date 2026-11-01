# CONTEXT_STORE.md — maintained engineering facts

## BIOBUZZ (TU07, 2026-10-29)
- Match: AUTO 30 s, transition 8 s, TELEOP 120 s. Timer authoritative; cues: 2:30 start (Cavalry
  Charge), 2:00 AUTO end (Buzzer×3), 0:08–0:01 "Drivers, pick up your controllers", 2:00 TELEOP (3 Bells),
  1:00 FLOWER unlock (Chime), 0:20 (Train Whistle), 0:00 (3 s buzzer), stop (Foghorn).
- Elements: 40 POLLEN (2.8 in), 8 red + 8 blue NECTAR (3.6 in). Staging: 4 in each FLOWER, 4 per
  GARDEN, 4 preload per robot, 3 NECTAR in each initially up CELL, 5 NECTAR per ALLIANCE AREA.
- Points: LEAVE 3 (AUTO), PARK 5/5, TIP 20/20, CELL remaining 2, bottom NECTAR 5, owned FLOWER element 2,
  GARDEN element 1; MINOR 5, MAJOR 20. RP thresholds (non-championship): SWARM 16, POLLINATOR 4 and 7 tips.
- Robot: start in 18 in cube, expansion 18×24×29 in, ≤ 8 motors + ≤ 8 servos, 1 Control Hub (or phone +
  Expansion Hub) + ≤ 1 additional Expansion Hub, servo ≤ 8 W mech / ≤ 4 A stall @6 V.
- AprilTags 36h11, 3.25 in, clusters of 4 on CELL bottoms: red rear 30–33, red audience 34–37, blue
  audience 38–41, blue rear 42–45. Tags move when HIVES tip.
- Field frame (GamePlan): origin centre, +x to blue wall, +y away from audience, +z up; CAD mapping
  `(x, −z, y)`.

## FTC SDK 12.0
- Maven Central artifacts RobotCore/Hardware/FtcCommon/Vision 12.0.0 (AAR). `MotorConfigurationType`
  needs gson. SDK 12 adds `AprilTagSingleDetection`/`AprilTagClusterDetection`, `PwmControl.setPulseWidth`.
- `OpMode` fields live in package-private `OpModeInternal` (gamepad1/2, telemetry, hardwareMap,
  msStuckDetectStop).

## Hardware (verified 2026-11-01)
- goBILDA 5203 19.2:1: 312 RPM, 0.25 A free, 9.2 A stall, 24.3 kg·cm @12 V ⇒ bare 0.1241 N·m, 5991 rpm.
- REV HD Hex: 12 V, 0.4 A free, 8.5 A stall, 6000 rpm, 0.105 N·m, 28 CPR.

## Team project (AthletechBiobuzzV1)
- SDK 12.0.0, Pedro Pathing 3.0.2 (+tuning 1.0.1), slothboard 0.3.2+0.6.0, Kotlin 2.4.10, commons-math3.
- Device names: lf rf lb rb (rf/rb reversed in code), shooter, intake, blocker, lhood, rhood, lturret,
  rturret, turretEncoder, pinpoint, ll (Limelight, unsupported).
- Import: 76/84 files compile unmodified; source tree untouched (git clean before/after).

## Performance (this container, 4 cores)
- AI-only match: ~90× real time unrecorded, ~15–20× recorded. Java-runtime match: ~8.5× real time.
