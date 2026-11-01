# Host ↔ FTC runtime protocol (v1)

The C++ host (`core/src/link.cpp`) spawns the Java runtime
(`org.gameplan.runtime.RuntimeMain`) as a child process and talks to it over the child's
stdin/stdout. stderr is diagnostics only. All integers and floats are **little-endian**.

```
frame   := u32 length  (= 1 + payload bytes)
           u8  type
           payload
```

## Lockstep model

There is one virtual clock (seconds of simulated time, `f64`). The host owns physics. The
runtime owns robot-code execution.

1. Host → `INIT`, runtime → `READY`.
2. Host → `STATE(t)`. The runtime's clock is set to `t`.
3. The runtime executes robot code. Every SDK call charges its modelled Control Hub cost to the
   virtual clock (`data/hardware/timing_profiles.json`). Hardware writes are buffered with the
   virtual timestamp at which the hub would receive them.
4. When the code needs fresh hardware data (bulk read, I2C read, voltage, …), sleeps, finishes
   a loop, or has run more than 5 ms of virtual time since the last exchange, the runtime sends
   `SYNC(t')` with its buffered writes and blocks.
5. The host integrates physics from `t` to `t'`, applying each write at its timestamp, and
   answers with `STATE(t')` containing sensor values sampled at `t'`.

No host wall-clock time is spent "waiting for hardware": the cost of every call is virtual. A
robot program that does too many expensive calls therefore runs fewer, slower loops in
simulated time — exactly the effect on a real Control Hub.

## Host → runtime

| type | name | payload |
|---|---|---|
| 1 | INIT | UTF-8 JSON: `protocol`, `devices[]` (index, name, kind, hub, port, ...), `timingProfile`, `deterministicCompute`, `seed`, `robot`, `sandbox` |
| 2 | STATE | see below |
| 9 | SHUTDOWN | empty; runtime replies `HWSTATS` then `BYE` and exits |

`STATE`:
```
f64  t
u8   control        0 none, 1 INIT opmode, 2 START, 3 STOP
u16  nameLen, name  (OpMode name for INIT)
u8   flags          bit0 robotControllerAlive, bit1 driverStationDisconnected, bit2 driverInputAllowed
gamepad1, gamepad2: f32 lx, ly, rx, ry, lt, rt; u32 buttons
u16  n; n × (u16 device, u8 field, f64 value)      sensor values sampled at t
u32  visionLen, UTF-8 JSON (empty if no new processed camera frame)
```
Gamepad button bits: A=0, B=1, X=2, Y=3, LB=4, RB=5, DPAD_UP=6, DPAD_DOWN=7, DPAD_LEFT=8,
DPAD_RIGHT=9, BACK=10, START=11, GUIDE=12, LSTICK=13, RSTICK=14, TOUCHPAD=15.

## Runtime → host

| type | name | payload |
|---|---|---|
| 10 | READY | JSON: `opModes[]` (name, class, group, kind=TeleOp/Autonomous, linear), `sdkVersion`, `diagnostics[]`, optional `error` |
| 11 | SYNC | `f64 t; u16 n; n × (u16 device, u8 field, f64 value, f64 tCommand)` |
| 12 | TELEMETRY | JSON `{t, lines[]}` (Driver Station telemetry, rate-limited as the SDK does) |
| 13 | LOOP | `f64 tStart, f64 tEnd, u32 transactions, f64 hwUs, i2cUs, computeUs, overheadUs, visionUs, slowestUs` |
| 14 | LOG | JSON `{level, msg}` (RobotLog, System.out of robot code) |
| 15 | STATUS | JSON `{opModeState, opMode, deterministic, error?, stack?}` |
| 16 | HWSTATS | JSON `{ops:[{op, calls, transactions, totalUs, meanUs}]}` |
| 17 | BYE | empty |

## Device fields

Motors (`kind=motor`) sensor fields: 1 position (ticks, offset applied by hub), 2 velocity
(ticks/s), 3 current (A), 4 busy, 5 over-current. Command fields: 1 power, 2 run mode
(0 RUN_WITHOUT_ENCODER, 1 RUN_USING_ENCODER, 2 RUN_TO_POSITION, 3 STOP_AND_RESET_ENCODER),
3 target position, 4 target velocity, 5 zero-power brake, 6 motor enable, 7–10 velocity PIDF,
11 target tolerance, 12 position P.

Servos / CR servos: command 1 pulse width (µs), 2 PWM enable.

IMU: 1 yaw, 2 pitch, 3 roll (rad), 4 yaw rate, 5 pitch rate, 6 roll rate (rad/s).
Voltage sensor: 1 volts. Pinpoint: 1 x (mm), 2 y (mm), 3 heading (rad), 4 vx, 5 vy (mm/s),
6 heading rate, 7 encoder X, 8 encoder Y. Distance: 1 metres. Color: 1 r, 2 g, 3 b, 4 alpha,
5 distance (m). Touch: 1 pressed. Analog: 1 volts. Hub: 1 bus volts, 2 in reset.
