// BIOBUZZ executable rules engine: match state machine, scoring assessment, and violations.
// Objective rules are evaluated deterministically. Rules whose penalty depends on referee
// perception (STRATEGIC, deliberate) go through RefereeModel, which is configurable, seeded
// and writes a rationale for every decision. Every violation cites its rule id.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "gameplan/json.hpp"
#include "gameplan/rng.hpp"
#include "gameplan/season.hpp"

namespace gp {

class World;

enum class Phase { PreMatch = 0, Auto = 1, Transition = 2, Teleop = 3, Post = 4, Final = 5 };
const char* toString(Phase p);

struct RefereeConfig {
    int strategicRepeatThreshold = 3;    // REPEATED occurrences before a call is perceived STRATEGIC
    double hiveRamSpeed = 0.9;           // m/s contact speed with HIVE frame considered "ramming"
    bool stochastic = false;             // model referee variance
    double missProbability = 0.0;        // probability a referee misses an objective-assisted call
    std::string profile = "standard";
    Json toJson() const;
    static RefereeConfig fromJson(const Json& j);
};

struct ScoreEvent {
    double t = 0;
    double matchTime = 0;
    Alliance alliance = Alliance::Red;
    std::string kind;     // LEAVE, PARK_AUTO, HIVE_TIP, ...
    int points = 0;
    int robot = -1;
    std::string period;   // AUTO / TELEOP / END
    std::string detail;
};

struct Violation {
    double t = 0, matchTime = 0;
    std::string rule;           // G410, ...
    int robot = -1;
    Alliance offender = Alliance::Red;
    std::string penalty;        // VERBAL_WARNING, MINOR_FOUL, MAJOR_FOUL, YELLOW_CARD, ...
    int foulPoints = 0;         // credited to the opponent
    bool card = false;
    bool judgment = false;      // decided by RefereeModel
    std::string rationale;      // traceable explanation (inputs + decision)
};

struct AllianceScore {
    int autoPts = 0, teleopPts = 0, endPts = 0, foulCredit = 0;
    int leavePark = 0, tips = 0;
    std::map<std::string, int> breakdown;
    int rp = 0;
    std::vector<std::string> rpDetail;
    int total() const { return autoPts + teleopPts + endPts + foulCredit; }
};

class RefereeModel {
public:
    explicit RefereeModel(const RefereeConfig& c, uint64_t seed) : cfg_(c), rng_(Rng::derive(seed, "referee")) {}
    // Returns whether a judgment-based violation is perceived STRATEGIC, with rationale.
    bool strategic(const std::string& rule, int robot, int occurrences, bool intentSignal, std::string& why);
    bool notices(std::string& why);
    const RefereeConfig& config() const { return cfg_; }
    Rng::State rngState() const { return rng_.state(); }
    void setRngState(const Rng::State& s) { rng_.setState(s); }

private:
    RefereeConfig cfg_;
    Rng rng_;
};

class MatchRules {
public:
    MatchRules(const SeasonDef& s, const RefereeConfig& rc, uint64_t seed);

    void startMatch(double t);                 // AUTO begins at t
    void update(World& w, double t, double dt);
    void requestStop(double t, const std::string& why);  // "MATCH stopped" (foghorn)

    Phase phase() const { return phase_; }
    double matchTime(double t) const { return t - tStart_; }
    double remaining(double t) const;          // FIELD timer semantics (counts down per period)
    std::string timerText(double t) const;     // e.g. "2:00"
    bool driverControlAllowed(double t, bool competition) const;
    bool flowerUnlocked(double t) const;       // FLOWER/NECTAR unlock (last 60 s)
    bool isFinal() const { return phase_ == Phase::Final; }
    double startTime() const { return tStart_; }
    double autoEnd() const { return tStart_ + season_.autoS; }
    double teleopStart() const { return tStart_ + season_.autoS + season_.transitionS; }
    double matchEnd() const { return tStart_ + season_.matchLength(); }

    const AllianceScore& score(Alliance a) const { return score_[int(a)]; }
    const std::vector<ScoreEvent>& scoreEvents() const { return events_; }
    const std::vector<Violation>& violations() const { return violations_; }
    std::vector<std::string>& cues() { return cueLog_; }
    Json resultJson() const;

    // Called by World on game events
    void onTip(World& w, Alliance a, double t);
    void onNectarEnteredFlower(World& w, int element, int flower, double t);
    void onHumanNectarEntry(World& w, Alliance a, double t, bool permitted);
    void onElementLeftField(World& w, int element, double t);
    void onHiveContact(World& w, int robot, double speed, double t);
    void addViolation(World& w, const std::string& rule, int robot, const std::string& base, const std::string& strategicPenalty,
                      bool judgment, bool intent, const std::string& detail, double t, int perInstanceFoul = 0);

    Json snapshot() const;
    void restore(const Json& j);

private:
    void assessAutoEnd(World& w, double t);
    void assessFinal(World& w, double t);
    void credit(Alliance a, const std::string& kind, int pts, const std::string& period, double t, int robot, const std::string& detail);
    void checkContinuousRules(World& w, double t, double dt);
    void computeRP();
    int count(const std::string& rule, int robot) const;

    const SeasonDef& season_;
    RefereeModel ref_;
    Phase phase_ = Phase::PreMatch;
    double tStart_ = 1e18;
    double settleStart_ = 0;
    AllianceScore score_[2];
    std::vector<ScoreEvent> events_;
    std::vector<Violation> violations_;
    std::vector<std::string> cueLog_;
    std::map<std::string, int> occurrences_;   // rule:robot -> count
    std::map<std::string, double> timers_;     // rule:robot -> accumulated seconds
    std::map<std::string, bool> flags_;
    int cueIndex_ = 0;
    bool stopped_ = false;
};

}  // namespace gp
