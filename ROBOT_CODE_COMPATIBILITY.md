# ROBOT_CODE_COMPATIBILITY.md

## Architecture decision
Evaluated: (a) full Android emulation of the Robot Controller app; (b) clean-room reimplementation of
the SDK; (c) **hybrid: real SDK jars + shadows of the Android-bound classes in a sandboxed desktop
JVM**. Chosen: (c). Proof of concept: the real `RobotCore` 12.0 interfaces, `Gamepad`, `ElapsedTime`,
navigation types load on a desktop JVM; only Android-bound classes fail (`LynxModule` → `android.content.Context`).
(a) is not cross-platform-practical and cannot run faster than real time; (b) would drift from the
SDK and break binary compatibility with libraries like Pedro Pathing compiled against the real SDK.

GamePlan does **not** claim to reproduce the Android Robot Controller OS or app. It executes TeamCode
and its libraries through a compatibility runtime.

## Execution path
1. **Import** (`Importer`): Gradle files → SDK version, repositories, dependencies (exclusions honoured);
   Maven resolution (POM parents, BOMs, highest-version wins, AAR → classes.jar, SHA-1 verified,
   429 back-off); source scan (OpModes, `hardwareMap` names incl. resolved string constants, vision,
   threads, Android APIs, web servers); Kotlin compiler provisioned to match the project's plugin.
2. **Compile with failure isolation**: `javac --release 8` against the *real* SDK jars + libraries
   (GamePlan stand-ins last on the classpath). Files that fail, and files depending on them, are
   excluded and listed with the first compiler error. Nothing in the team's source is rewritten.
3. **Load**: `SandboxLoader` (child-first: shadows → SDK → libs → team) passes every class through
   `ClassRewriter` (time/sleep → virtual clock, `System.exit`, process creation denied) and scans APIs.
4. **Run**: OpModes are discovered by annotation; the host drives INIT/START/STOP like a Driver Station.

## Compatibility matrix (tested)
| Project / feature | Result |
|---|---|
| Fixture iterative TeleOp (mecanum, bulk AUTO, DcMotorEx velocity, servos, voltage) | runs; behaviour checked |
| Fixture LinearOpMode AUTO (RUN_TO_POSITION, IMU turn, `sleep`) | runs; drove 0.77 m of 0.8 m target |
| Fixture VisionPortal + AprilTagProcessor | runs; virtual detections delivered |
| Fixture expensive loop (no caching, getCurrent/getPower/voltage) | runs; loop 25.7 ms vs 6.9 ms |
| Fixture Android-only OpMode | excluded with reason (correct) |
| AthletechBiobuzzV1 (real team, SDK 12, Pedro 3, Sloth dashboard, Kotlin 2.4.10, 84 files) | 76 compile & load; `DriveIntakeShoot` played a full match |
| — excluded: Pedro `tuning.autotune` package (not in resolved artifacts), `MonitorWebServer` (Android assets/web), `RobotLogger`, `HardwareMonitorViewer` (depend on it) | reported |
| Road Runner, FTCLib projects | **not yet tested** |
| Robot-code threads | run on the shared virtual clock; marked non-deterministic |
| Raw-frame vision pipelines (OpenCV/EasyOpenCV), Limelight | not executed (boundary reported) |

Success on these projects does not imply universal compatibility; every import produces
`compatibility.json` for that project.
