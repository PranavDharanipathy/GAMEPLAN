// World: authoritative simulation state for one match (field, robots, elements, rules).
// The same World runs headless (gp-sim, batch experiments) and inside Unreal (the Unreal
// plugin only renders World state and feeds live controller input). There is no second,
// simplified physics path.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gameplan/electrical.hpp"
#include "gameplan/robot.hpp"
#include "gameplan/rules.hpp"
#include "gameplan/season.hpp"
#include "gameplan/sensors.hpp"
#include "gameplan/uncertainty.hpp"

namespace gp {

enum class ElemType { Pollen = 0, Nectar = 1 };
enum class ElemState { Free = 0, Held = 1, InCell = 2, InFlower = 3, HumanArea = 4, OutOfField = 5 };

struct Element {
    int id = 0;
    ElemType type = ElemType::Pollen;
    Alliance color = Alliance::Red;   // NECTAR colour (unused for POLLEN)
    double radius = 0.0356, mass = 0.024;
    Vec3 p, v;
    ElemState state = ElemState::Free;
    int holder = -1;        // robot index when held
    int container = -1;     // hive*2+cell or flower index
    int lastRobot = -1;     // last robot that touched/launched it
    double lastRobotTime = -1;
    bool fromTip = false;   // released by a TIPPED HIVE and not yet touched anything else (G409)
    bool launched = false;
    Vec2 launchFrom;        // where and in which direction it was last launched (G405 intent)
    double launchYaw = 0;
    // rigid-body + contact state (balls.cpp)
    Vec3 w;                         // angular velocity (rad/s, world)
    double q[4] = {1, 0, 0, 0};     // orientation quaternion (w, x, y, z): the holes rotate with it
    double squish = 0;              // largest contact compression this step (m), for rendering
    Vec3 squishN{0, 0, 1};          // direction of that compression (from contact towards centre)
    double pathS = 0, pathV = 0;    // position / speed along the holder's internal ball path
    int passRobot = -1;             // robot whose frame the ball is leaving (shooter / mouth) ...
    double passUntil = -1;          // ... until this time
    double reentryAt = -1;  // field staff / human re-entry time for out-of-field elements
    bool enteredFlowerEarly = false;
};

struct HiveState {
    int upCell = 0;
    bool tipping = false;
    double tipStart = 0;
    bool spilled = false;
    double weight = 0;          // tip weight in the up cell (incl. impact bonus)
    double threshold = 7.5;     // sampled per match (field calibration spread)
    std::vector<int> contents[2];
    int tips = 0;
    double angle() const;       // current rotation (rad) relative to the initial up state
    double stateAngle = 1.0472;
    int initialUp = 0;
    double tipFraction(double t, double settle) const { return tipping ? clampd((t - tipStart) / settle, 0, 1) : 0; }
};

struct FlowerState {
    std::vector<int> stack;  // bottom -> top
};

struct HumanPlayer {
    Alliance alliance;
    int nectarInArea = 5;
    int unlocked = 0;           // NECTAR entries permitted by TIPS (G426.A)
    double nextActionTime = 0;
    std::vector<int> waitingIds;  // NECTAR element ids held by the human (in alliance area)
    double reactionS = 2.0;
    bool obeyRules = true;
};

struct FaultEvent {
    double t = 0;
    int robot = -1;
    std::string kind;     // esd_discharge, hub_comm_loss, hub_reset, rc_crash, ds_disconnect, brownout
    std::string target;   // hub id / "robot-controller" / "driver-station"
    double duration = 0;  // < 0 => no recovery this match
    double energyMJ = 0;  // discharge energy (mJ) for ESD
    std::string cause;
};

struct EnvConfig {
    double humidity = 0.40;          // relative humidity
    double ambientC = 25.0;
    bool antiStaticTreated = true;   // tiles sprayed per event guidance
    double tileMuScale = 1.0;
};

struct WorldConfig {
    uint64_t seed = 1;
    double dt = 0.0005;             // physics step
    std::string fidelity = "realtime";  // realtime | engineering
    UncertaintyConfig uncertainty;
    EnvConfig env;
    RefereeConfig referee;
    bool competitionMode = true;    // false => practice (AUTO driver control allowed, flagged)
    Json toJson() const;
};

class AIController;

struct RobotSlot {
    std::unique_ptr<Robot> robot;
    std::unique_ptr<SensorSuite> sensors;
    std::shared_ptr<AIController> ai;
    bool runtimeControlled = false;   // driven by the FTC Java runtime
    double esdCharge = 0;             // coulombs on frame (hidden)
    double esdSusceptibility = 1.0;   // hidden
    int pendingPortReset = -1;        // hub index whose ports reset after recovery
};

double predictBallHeight(const BallPhys& b, double m, double r, double v, double th, double spinSense, double dx, double& vzAt);

class World {
public:
    World(const SeasonDef& season, const Components& comps, const WorldConfig& cfg);
    ~World();

    // Setup
    int addRobot(const RobotConfig& rc, Alliance a, const std::string& label, Pose2 start, bool runtime,
                 std::shared_ptr<AIController> ai = nullptr);
    void stageField();   // place all scoring elements per season staging (after robots added)

    // Simulation
    void step();                 // one physics step of cfg.dt
    void advanceTo(double t, const std::function<void()>& perStep = {});
    double time() const { return t_; }
    long long stepIndex() const { return steps_; }

    // Accessors
    const SeasonDef& season() const { return season_; }
    const WorldConfig& config() const { return cfg_; }
    std::vector<RobotSlot>& robots() { return robots_; }
    const std::vector<RobotSlot>& robots() const { return robots_; }
    std::vector<Element>& elements() { return elements_; }
    const std::vector<Element>& elements() const { return elements_; }
    HiveState& hive(Alliance a) { return hives_[int(a)]; }
    const HiveState& hive(Alliance a) const { return hives_[int(a)]; }
    const std::vector<FlowerState>& flowers() const { return flowers_; }
    HumanPlayer& human(Alliance a) { return humans_[int(a)]; }
    MatchRules& rules() { return *rules_; }
    const MatchRules& rules() const { return *rules_; }
    std::vector<FaultEvent>& faults() { return faults_; }
    const UncertaintySampler& sampler() const { return sampler_; }
    Vec3 tagPosition(int tagId, Vec3* normal = nullptr) const;   // live pose (moves with HIVE tips)
    // Live frame of a HIVE cell's AprilTag cluster (SDK 12 cluster object frame): origin and axes
    // x right / y down as seen facing the tags, z into the cell.
    struct ClusterFrame { Vec3 origin, xr, yd, za; };
    ClusterFrame clusterFrame(int hive, int cell) const;
    // Which cell a tag belongs to (false if unknown); k = index in the cell's tag list.
    bool tagCell(int tagId, int& hive, int& cell, int& k) const;
    std::vector<int> allTagIds() const;
    bool robotTouchesWall(int r, double margin = 0.01) const;
    bool robotInLoadingZone(int r) const;
    double robotContactTime(int a, int b) const;
    bool robotsInContact(int a, int b) const;
    // Ball physics helpers (balls.cpp)
    const BallPhys& ballPhys(const Element& e) const;
    double ballInertia(const Element& e) const;
    std::vector<Vec3> ballPath(int robot) const;              // body frame, mouth -> shooter
    double ballPathLength(int robot) const;
    Vec3 ballPathPoint(int robot, double s, Vec3* tangent = nullptr) const;
    Vec3 cellSlot(int hive, int cell, int k) const;           // resting place of the k-th element in a cell

    // State hashing / checkpoints (host-side state only; see DEEP_ANALYSIS.md on runtime state)
    Json snapshot() const;
    void restore(const Json& j);
    std::string stateHash() const;

    // Hooks
    std::function<void(const FaultEvent&)> onFault;
    std::function<void(const char* kind, int robot, const Json& detail)> onEvent;

    // Launch bookkeeping for analysis
    struct LaunchRecord { double t; int robot; int element; double speed, elevation, heading, flyBefore, flyAfter; };
    std::vector<LaunchRecord> launches;

private:
    void collideRobots(double dt);
    void stepBalls(double dt);                       // balls.cpp
    void launchElement(int r, int mech, int element);
    void stepHives(double dt);
    void stepHumans(double dt);
    void stepFaults(double dt);
    void releaseFromCell(int hive, int cell);
    void ejectElement(Element& e);
    Vec3 cellOpeningCenter(int hive, int cell) const;
    bool robotHasRuntime(int r) const { return robots_[r].runtimeControlled; }

    SeasonDef season_;
    const Components& comps_;
    WorldConfig cfg_;
    UncertaintySampler sampler_;
    Rng rngElements_, rngFaults_, rngHuman_;
    double t_ = 0;
    long long steps_ = 0;
    std::vector<RobotSlot> robots_;
    std::vector<Element> elements_;
    HiveState hives_[2];
    std::vector<FlowerState> flowers_;
    HumanPlayer humans_[2];
    std::unique_ptr<MatchRules> rules_;
    std::vector<FaultEvent> faults_;
    std::vector<std::vector<double>> contactTime_;
    std::vector<double> intakeLoadUntil_, intakeLoad_;
    std::vector<Vec2> prevRobotVel_;
    std::vector<int> prevLegContact_;
};

}  // namespace gp
