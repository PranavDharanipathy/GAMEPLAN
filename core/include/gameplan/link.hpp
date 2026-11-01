// Host side of the FTC runtime link (see runtime/PROTOCOL.md).
// The FTC Java runtime runs in a separate, sandboxed process. Host and runtime advance in
// lockstep on one virtual clock: the runtime charges every SDK call its modelled Control Hub
// cost and asks the host to advance physics to its clock before any fresh hardware read.
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "gameplan/json.hpp"
#include "gameplan/recorder.hpp"

namespace gp {

class World;

struct GamepadState {
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
    uint32_t buttons = 0;   // bit layout in runtime/PROTOCOL.md
    std::vector<double> toVec() const { return {lx, ly, rx, ry, lt, rt, double(buttons)}; }
    static GamepadState fromVec(const std::vector<double>& v, size_t off);
};

enum GamepadButton : uint32_t {
    BTN_A = 1u << 0, BTN_B = 1u << 1, BTN_X = 1u << 2, BTN_Y = 1u << 3, BTN_LB = 1u << 4, BTN_RB = 1u << 5,
    BTN_DUP = 1u << 6, BTN_DDOWN = 1u << 7, BTN_DLEFT = 1u << 8, BTN_DRIGHT = 1u << 9, BTN_BACK = 1u << 10,
    BTN_START = 1u << 11, BTN_GUIDE = 1u << 12, BTN_LSTICK = 1u << 13, BTN_RSTICK = 1u << 14, BTN_TOUCHPAD = 1u << 15
};

// OS-level confinement applied to the child before exec (Linux: Landlock; SECURITY.md).
// Paths not listed are inaccessible to the child (open() fails with EACCES).
struct FsPolicy {
    std::vector<std::string> readDirs;    // read + execute, recursive
    std::vector<std::string> readFiles;   // read single files (jars)
    std::vector<std::string> writeDirs;   // full read/write, recursive (scratch only)
    std::vector<std::string> devFiles;    // read/write character devices (/dev/null, ...)
    bool denyTcp = true;                  // Landlock ABI >= 4: no TCP bind/connect
    bool scopeSignals = true;             // Landlock ABI >= 6: cannot signal processes outside its domain
};

// Landlock ABI the running kernel supports (0 = unavailable / not Linux).
int landlockAbi();

class ChildProcess {
public:
    ~ChildProcess();
    // If policy is given and the kernel supports Landlock, the child confines itself before exec and
    // exits with code 125 if confinement fails (fail closed).
    bool spawn(const std::vector<std::string>& argv, const std::string& workdir, std::string& err,
               const FsPolicy* policy = nullptr);
    bool writeAll(const void* data, size_t n);
    // Reads exactly n bytes; returns false on EOF/timeout.
    bool readAll(void* data, size_t n, double timeoutS);
    bool alive();
    void kill();
    int exitCode() const { return exitCode_; }
    std::string drainStderr();
    const std::string& confinement() const { return confinement_; }

private:
#ifdef _WIN32
    void* proc_ = nullptr;
    void* inW_ = nullptr;
    void* outR_ = nullptr;
    void* errR_ = nullptr;
#else
    int pid_ = -1, inFd_ = -1, outFd_ = -1, errFd_ = -1;
#endif
    int exitCode_ = -1;
    std::string confinement_;
};

struct RuntimeConfig {
    std::string java = "java";
    std::string runtimeJar;          // gameplan-runtime.jar (host bridge, app classpath)
    std::string packageDir;          // compiled simulator package of the team project
    std::string timingProfile;
    bool deterministicCompute = true;
    bool sandbox = true;
    double watchdogS = 30.0;         // wall-clock limit for one lockstep exchange
    std::string logDir;
    // Only directory the robot code may write: holds its virtual /sdcard (AppUtil.ROOT_FOLDER etc.)
    // and java.io.tmpdir. Defaults to a fresh temporary directory.
    std::string scratchDir;
};

struct TelemetryFrame { double t; std::vector<std::string> lines; };

class RuntimeLink {
public:
    RuntimeLink(World& w, int robot, const RuntimeConfig& cfg, Recorder* rec);
    ~RuntimeLink();
    bool start(std::string& err);        // spawn, INIT, READY
    const Json& ready() const { return ready_; }
    // queue control for delivery with the next STATE
    void initOpMode(const std::string& name) { pendingControl_ = 1; pendingName_ = name; }
    void startOpMode() { pendingControl_ = 2; }
    void stopOpMode() { pendingControl_ = 3; }
    void setGamepads(const GamepadState& g1, const GamepadState& g2, bool allowed) { gp1_ = g1; gp2_ = g2; inputAllowed_ = allowed; }
    // Deterministic re-execution: gamepad state is taken from the recorded timeline at the exact exchange time.
    std::function<void(double, GamepadState&, GamepadState&)> replayInputs;
    // Lockstep: target time the runtime is waiting for (after exchange()).
    double target() const { return target_; }
    bool exchange(double t, std::string& err);  // send STATE(t), read until SYNC; applies nothing yet
    void applyDueCommands(double t);            // apply runtime writes with timestamp <= t to the robot
    bool alive() const { return alive_; }
    const std::vector<TelemetryFrame>& telemetry() const { return telemetry_; }
    const Json& lastStatus() const { return status_; }
    const std::vector<Json>& statusLog() const { return statusLog_; }
    Json hwStats() const { return hwStats_; }
    void shutdown();
    bool deterministic() const { return deterministic_; }
    std::string opModeState() const { return opModeState_; }
    double lastLoopHz() const { return lastLoopHz_; }
    // "landlock-abiN" / "unavailable (...)" / "disabled": what OS confinement the runtime got.
    const std::string& fsConfinement() const { return fsConfinement_; }
    const std::string& scratchDir() const { return cfg_.scratchDir; }

private:
    bool readFrame(uint8_t& type, std::string& payload, std::string& err);
    bool sendFrame(uint8_t type, const std::string& payload);
    Json deviceTable() const;
    World& w_;
    int robot_;
    RuntimeConfig cfg_;
    Recorder* rec_;
    ChildProcess proc_;
    Json ready_, status_, hwStats_;
    std::vector<Json> statusLog_;
    double target_ = 0;
    int pendingControl_ = 0;
    std::string pendingName_;
    GamepadState gp1_, gp2_;
    bool inputAllowed_ = false;
    struct Cmd { uint16_t dev; uint8_t field; double value; double t; };
    std::deque<Cmd> cmds_;
    std::vector<TelemetryFrame> telemetry_;
    bool alive_ = false;
    bool deterministic_ = true;
    std::string opModeState_ = "none";
    uint16_t motorBase_ = 0, servoBase_ = 0, sensorBase_ = 0, hubBase_ = 0;
    double lastLoopHz_ = 0;
    std::string lastVisionKey_;
    std::string fsConfinement_ = "disabled";
    std::vector<double> lastInput_;
};

}  // namespace gp
