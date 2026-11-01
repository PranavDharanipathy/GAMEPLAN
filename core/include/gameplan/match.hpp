// Match orchestration shared by the headless CLI, batch experiments, counterfactual branches
// and the Unreal host. Owns the World, the FTC runtime links, AI controllers and the recorder.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gameplan/ai.hpp"
#include "gameplan/link.hpp"
#include "gameplan/recorder.hpp"
#include "gameplan/world.hpp"

namespace gp {

std::string resolvePath(const std::string& p, const std::string& baseDir);
std::string gameplanVersion();

struct ScriptedGamepad {
    struct Ev { double t; int pad; std::string key; double value; };
    std::vector<Ev> events;
    static ScriptedGamepad load(const std::string& path);
    // recorded inputs (inputs_<i>.csv) for deterministic re-execution
    static ScriptedGamepad fromRecording(const std::string& path);
    void stateAt(double matchTime, GamepadState& g1, GamepadState& g2) const;
    bool fromRecordingTimeline = false;
    std::vector<std::pair<double, std::pair<GamepadState, GamepadState>>> timeline;
};

struct RobotEntry {
    std::string label, robotPath, controller = "idle";  // runtime | ai | idle
    Alliance alliance = Alliance::Red;
    Pose2 start;
    bool startGiven = false;
    std::string packageDir, autoOpMode, teleOpMode, gamepadScript;
    Personality personality;
    double difficulty = 0.6;
    std::string instruction;
    Json weights;              // optional explicit task weights {pollen, hive, flower, garden}
    std::string imitation;     // team profile the AI parameters came from (tools/ftcscout_imitate.py)
};

struct MatchSetup {
    std::string seasonPath, componentsPath, recordDir, baseDir;
    WorldConfig world;
    std::vector<RobotEntry> robots;
    RuntimeConfig runtime;
    double preMatchS = 3.0;
    Json raw;
    static MatchSetup fromJson(const Json& j, const std::string& baseDir);
    static MatchSetup load(const std::string& path);
};

struct ParamChange { std::string key; double value; };
// Apply a simulation-level parameter change (never touches robot source code).
bool applyParam(World& w, const ParamChange& c, std::string& err);

class MatchRunner {
public:
    explicit MatchRunner(const MatchSetup& s);
    ~MatchRunner();
    bool begin(std::string& err);
    // advance the simulation by dt of simulated time (servicing runtimes); returns false when done
    bool advance(double dt, std::string& err);
    Json finish();
    Json run(std::string& err);   // begin + advance until final + finish

    World& world() { return *world_; }
    const MatchSetup& setup() const { return setup_; }
    Recorder* recorder() { return recorder_.get(); }
    RuntimeLink* link(int robot);
    void setLiveGamepads(int robot, const GamepadState& g1, const GamepadState& g2);
    void setReplayInputs(int robot, const ScriptedGamepad& s) { replay_[robot] = s; }   // call before begin()
    void schedule(double matchTime, const std::vector<ParamChange>& changes) { branchT_ = matchTime; branch_ = changes; }
    bool done() const { return done_; }
    std::vector<std::string> warnings;

private:
    void processSchedule();
    void perStep();
    MatchSetup setup_;
    SeasonDef season_;
    Components comps_;
    std::unique_ptr<World> world_;
    std::unique_ptr<Recorder> recorder_;
    std::vector<std::unique_ptr<RuntimeLink>> links_;   // index = robot (nullptr if not runtime)
    std::vector<ScriptedGamepad> scripts_;
    std::map<int, ScriptedGamepad> replay_;
    std::vector<bool> hasScript_, hasLive_;
    std::vector<std::pair<GamepadState, GamepadState>> live_;
    struct Sched { double t; int robot; int action; std::string name; bool done; };
    std::vector<Sched> sched_;
    bool started_ = false, done_ = false;
    double nextHash_ = 0;
    double branchT_ = -1;
    std::vector<ParamChange> branch_;
    bool branched_ = false;
    int sameTimeExchanges_ = 0;
    double lastEventT_ = -1;
};

// Counterfactual: deterministic re-execution of a recorded match with parameter changes from a
// chosen match time. Verifies that the baseline re-execution reproduces the recorded state
// hashes; if not, the comparison is labelled APPROXIMATE and the divergence time reported.
Json runCounterfactual(const std::string& recordDir, double branchMatchTime, const std::vector<ParamChange>& changes,
                       const std::string& outDir, std::string& err);

}  // namespace gp
