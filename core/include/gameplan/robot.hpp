// Robot digital twin: chassis rigid body, mecanum wheel-ground contact with slip, geared
// joints with unilateral backlash, mechanisms, hub firmware motor controllers, sensors.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "gameplan/electrical.hpp"
#include "gameplan/json.hpp"
#include "gameplan/math.hpp"
#include "gameplan/season.hpp"
#include "gameplan/uncertainty.hpp"

namespace gp {

// Motor -> gearbox -> load with a backlash gap that is a real unilateral contact:
// engaged => lumped inertia; disengaged => motor and load move independently until the
// gap closes (momentum-conserving re-engagement). Encoder reads the MOTOR side, so the
// robot program sees backlash exactly as on a real robot.
struct GearedJoint {
    int motor = -1;
    double ratio = 1, eff = 0.9, backlash = 0;
    double jm = 2e-6;    // rotor inertia (motor shaft)
    double jl = 1e-4;    // load inertia (output shaft)
    double bearingViscous = 1e-4, bearingCoulomb = 0.01;
    // state (output-referred)
    double w = 0, theta = 0;     // load side
    double wm = 0, thetam = 0;   // motor side / ratio
    double gap = 0;
    int engaged = 1;             // -1, 0 (free), +1
    // per-step input
    double loadTorque = 0;
    double loadDamping = 0;      // d(load torque)/d(w) >= 0, used for semi-implicit integration of stiff tyre contact
    double lastMotorTorque = 0;
    double motorOmega() const { return wm * ratio; }
    double jmOut() const { return jm * ratio * ratio; }
    void integrate(double motorTorque, double dt);
};

enum class RunMode { RunWithoutEncoder = 0, RunUsingEncoder = 1, RunToPosition = 2, StopAndResetEncoder = 3 };

// REV hub firmware per-port controller (runs every physics step, independent of RC loop time).
struct PortController {
    RunMode mode = RunMode::RunWithoutEncoder;
    double power = 0;          // as sent by SDK (direction already applied)
    double targetVelocity = 0; // ticks/s
    bool velocityCommand = false;
    int targetPosition = 0;
    bool brake = true;
    bool motorEnabled = true;
    double kP = 10, kI = 3, kD = 0, kF = 0;   // SDK velocity PIDF (REV units, see ROBOT_CODE_COMPATIBILITY.md)
    double posP = 10;
    int tolerance = 5;
    double integ = 0, prevErr = 0;
    long long encoderOffset = 0;
    double ticksPerRev = 537.7;     // at output
    double maxTicksPerSec = 2796;   // achievable max used for setPower in RUN_USING_ENCODER
    bool busy = false;
    double compute(double posTicks, double velTicks, double dt);
};

struct Wheel {
    std::string name;
    Vec2 pos;          // body frame
    Vec2 n;            // traction direction (perpendicular to roller axis), body frame
    double radius = 0.052;
    int joint = -1;
    double normal = 0, slip = 0, muScale = 1.0;
    Vec2 force;        // world frame, last step
    double misalign = 0;
};

enum class MechType { Intake, Flywheel, Arm, Turret, Gate, Hood, Generic };

struct Mechanism {
    std::string id;
    MechType type = MechType::Generic;
    int joint = -1;                  // motor-driven
    std::vector<int> servos;         // servo-driven (indices into electrical servos)
    // intake
    double rollerRadius = 0.03, captureSpeed = 1.2, width = 0.30, reach = 0.06;
    int capacity = 3;
    // Internal ball path (body frame, metres), intake mouth -> shooter. Empty = generated from the
    // chassis size, intake and launcher geometry (Robot::ballPath()).
    std::vector<Vec3> path;
    double rollerZ = -1;             // intake roller axis height (default: just above a POLLEN centre)
    double mouthHeight = 0.13;       // clear height of the intake mouth
    double beltGripS = 0.03;         // time constant of belts/rollers bringing a held ball to speed
    // flywheel / launcher
    double flyRadius = 0.048, launchEff = 0.45, launchHeight = 0.30, launchFwd = 0.0, contactEnergyLoss = 0.55;
    double minLaunchFrac = 0.2;
    double compression = 0.006;      // ball squeeze between wheel and hood (m): more grip, more speed
    double spinSense = 1.0;          // +1: wheel under the ball's path -> backspin (lift); -1: topspin
    // gate (feeder)
    double openAbove = 0.5;          // servo target position fraction above which gate is open
    double feedInterval = 0.25;
    // hood
    double angleAt0 = 0.35, angleAt1 = 1.1;  // rad from horizontal, mapped from servo command
    // turret
    double turretGear = 1.0;
    int turretEncoderSensor = -1;
    // arm
    double gravityTorque = 0;
};

enum class SensorType { Imu, Pinpoint, Voltage, Distance, Color, Touch, Webcam, AnalogEncoder, Digital, Analog };

struct SensorDef {
    std::string name;
    SensorType type;
    std::string hub = "control";
    Vec3 pos;           // body frame
    double yaw = 0, pitch = 0;
    double hfov = 1.22, vfov = 0.9;
    int resX = 640, resY = 480;
    double fps = 30;
    double podX = 0, podY = 0;   // pinpoint pod offsets (m)
    int mechanism = -1;          // analog encoder bound to a mechanism (turret)
};

struct RobotConfig {
    std::string name, team, variant, sourcePath, contentHash;
    Json raw;
    double mass = 13.0, length = 0.43, width = 0.43, height = 0.40, cogZ = 0.10, cogX = 0, cogY = 0, yawInertia = 0;
    double wheelInertia = 1.5e-4, tileMu = 0.95, rollerRollingMu = 0.02;
    std::vector<Wheel> wheels;
    std::vector<Json> motorDefs, servoDefs, mechanismDefs, sensorDefs, hubDefs;
    Json batteryDef, wiringDef, imperfections, overrides;
    static RobotConfig load(const std::string& path);
    static RobotConfig fromJson(const Json& j, const std::string& path = "");
};

struct RobotSnapshotState;

class Robot {
public:
    RobotConfig cfg;
    Alliance alliance = Alliance::Red;
    std::string label;          // e.g. "red1"
    bool isAI = false;
    ElectricalSystem elec;
    std::vector<GearedJoint> joints;
    std::vector<PortController> ports;     // one per motor
    std::vector<std::string> motorNames;
    std::vector<int> motorJoint;           // joint index per motor
    std::vector<double> motorSign;         // -1 when the motor is mounted mirrored (e.g. right-side drive motors)
    std::vector<std::string> servoNames;
    std::vector<Mechanism> mechs;
    std::vector<SensorDef> sensors;
    std::vector<Wheel> wheels;
    // chassis state (world)
    Vec2 pos, vel;
    double heading = 0, omega = 0;
    Vec2 accel;                 // body-frame acceleration (for load transfer)
    Vec2 contactImpulse;        // world-frame impulse from walls/robots/structures since the last step
    double pitch = 0, roll = 0, pitchRate = 0, rollRate = 0;
    bool tipped = false;
    double yawInertia = 0.5;
    // imperfections (hidden state; never sent to the robot program)
    double frameFlexRad = 0, wheelSlipScale = 1.0, cogOffsetX = 0, cogOffsetY = 0;
    // game state
    std::vector<int> held;      // element ids held in robot storage
    double lastFeedTime = -1;
    bool disabled = false;      // referee DISABLE / RC dead
    bool rcAlive = true;
    double startX = 0, startY = 0;
    // accumulated metrics
    double distance = 0, maxSpeed = 0, slipEnergy = 0;
    int collisions = 0;

    void build(const Components& comps, UncertaintySampler& us, const std::string& prefix);
    OBB2 footprint() const;
    Vec2 bodyToWorld(Vec2 b) const { return pos + b.rotated(heading); }
    int motorIndex(const std::string& name) const;
    int servoIndex(const std::string& name) const;
    // physics
    // firmware -> electrical solve -> wheel/ground contact -> joints -> chassis -> slow electrical states
    void step(double dt, double t, double tileMuScale);
    void applyImpulse(Vec2 impulse, Vec2 worldPoint);
    double encoderTicks(int motor) const;              // raw (no offset)
    double encoderVelocity(int motor) const;
    double surfaceSpeed(int mech) const;
    double servoFraction(int servoIdx) const;          // commanded position fraction (0..1)
    bool anyPoweredOutput() const;
    Json snapshot() const;
    void restore(const Json& j);
};

}  // namespace gp
