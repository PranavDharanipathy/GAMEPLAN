# FTC_SDK.md — what GamePlan knows about the FTC SDK and how it reproduces it

## Versions
- Baseline: **FTC SDK 12.0.0** (2026–2027 season), Maven Central `org.firstinspires.ftc:{RobotCore,Hardware,FtcCommon,Vision}:12.0.0` (AARs; `classes.jar` extracted).
- The importer reads the project's `org.firstinspires.ftc:RobotCore:<v>` and uses runtime adapters for
  that version if built (`java runtime/tools/Build.java --sdk <v>`), else falls back to 12.0.0 and says so.
- Only 12.0.0 shadows have been compiled and tested. Older SDKs: build with `--sdk`; any signature
  drift fails the build loudly (shadows are compiled against the real jars).

## What runs on a desktop JVM unchanged (verified by loading the real classes)
Interfaces and pure-Java classes: `DcMotor(Ex)`, `DcMotorSimple`, `Servo`, `CRServo`, `IMU`
(+`IMU.Parameters`), `VoltageSensor`, `DistanceSensor`, `ColorSensor`, `NormalizedColorSensor`,
`TouchSensor`, `DigitalChannel`, `PwmControl(.PwmRange)`, `PIDFCoefficients`, `Gamepad` (incl. edge
detection via `copy`), `ElapsedTime`, `Range`, `AngleUnit`/`DistanceUnit`/`CurrentUnit`/`VoltageUnit`,
`YawPitchRollAngles`, `Orientation`, `AngularVelocity`, `Quaternion`, `Pose2D`, `Pose3D`,
`RevHubOrientationOnRobot` (with GamePlan's `android.opengl.Matrix` stand-in), AprilTag result types,
annotations `@TeleOp/@Autonomous/@Disabled`.

## What is Android-bound and therefore shadowed (same public API)
`OpModeInternal`, `OpMode`, `LinearOpMode`, `HardwareMap(.DeviceMapping)`, `DcMotorImpl(Ex)`,
`ServoImpl(Ex)`, `CRServoImpl(Ex)`, `AnalogInput`, `LynxModule(.BulkCachingMode)`,
`GoBildaPinpointDriver` (+ nested enums), `RobotLog`, `VisionPortal(.Builder)`,
`AprilTagProcessor(.Builder)`; minimal `android.*` stand-ins (`Context`, `util.Log`, `util.Size`,
`os.SystemClock`, `os.Process.getElapsedCpuTime`, `opengl.Matrix`, `graphics.Canvas/Bitmap`) and
`org.opencv.core.{Point,Mat}` type stand-ins.

## Lifecycle semantics reproduced
- Iterative: `init` → `init_loop`* → (START) `start` → `loop`* → (STOP) `stop`; telemetry auto-updated
  after each call; `time`/`getRuntime()` reset at start.
- Linear: `runOpMode` on its own thread; `waitForStart`; `opModeIsActive()` = started && !stop (one
  user-loop iteration recorded per call); `isStopRequested()` includes thread interrupt; `sleep()`
  interruptible; after STOP the OpMode has `msStuckDetectStop` (900 ms virtual) before force-stop.
- When an OpMode stops the SDK zeroes motor power and disables servo PWM — reproduced.
- `requestOpModeStop`, `terminateOpModeNow`, exceptions → "crashed" status with stack trace (as the DS shows).
- Static state persists across OpModes in one session (same class loader), as on a Robot Controller.

## Hardware semantics reproduced
- Direction handled SDK-side: power, target position, encoder position/velocity sign-flipped for REVERSE.
- `setPower` with an unchanged value sends no Lynx command (costs only SDK overhead).
- `getPower()` is a Lynx transaction returning the last command (it is not cached!).
- Bulk caching: OFF (every read a transaction), AUTO (repeat read of the same channel refreshes),
  MANUAL (until `clearBulkCache`). Bulk-readable: encoder position/velocity, busy, digital, analog.
  Not bulk: motor current, voltage, I2C sensors.
- Run modes executed by a hub firmware model at the physics rate: RUN_WITHOUT_ENCODER (duty),
  RUN_USING_ENCODER (velocity PIDF, approximate REV units), RUN_TO_POSITION (P → velocity, tolerance,
  busy), STOP_AND_RESET_ENCODER (reset on receipt).
- Hub reset (brownout/ESD) → outputs off; after recovery modes reset and encoder counts restart at 0.
- Servo: position → scaleRange → PWM 600–2400 µs; first `setPosition` enables PWM; CR servo power →
  position → pulse. Servo motion and current come from the host servo model.

## Known gaps (explicit)
- Controller-level APIs (`DcMotorController`, `ServoController` methods) return defaults with a warning.
- `MotorConfigurationType` needs gson (provided) — returned when constructible, else `null`.
- AprilTag cluster detections (`AprilTagClusterDetection`, SDK 12) are synthesized with the SDK's grouping rules (see SIMULATION_PHYSICS.md); `pitch`/`roll` in `ftcPose` are 0.
- `robotPose` in AprilTag detections is `null` (camera-pose localization not computed yet).
- OnBot Java, Blocks, the RC web UI, `@WebHandlerRegistrar`, `@OnCreateEventLoop` hooks, Limelight3A,
  phone cameras: not executed (reported).
