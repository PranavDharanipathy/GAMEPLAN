// AI drivers for opponent / alliance-partner robots.
//
// AI robots are ordinary simulated robots: their commands go through the same hub firmware,
// electrical network and physics as the user's robot. The AI never reads hidden state
// (battery resistance, tip threshold, sampled imperfections) - it perceives a noisy estimate
// of poses and element positions, like a human driver. Difficulty changes skill (aim error,
// reaction time, decision quality, consistency), never physics.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "gameplan/json.hpp"
#include "gameplan/math.hpp"
#include "gameplan/rng.hpp"

namespace gp {

class World;

struct Personality {
    std::string name = "balanced";
    double aggression = 0.5;     // shoots with fewer held elements, contests contested elements
    double defense = 0.0;        // fraction of TELEOP spent defending (legal)
    double caution = 0.5;        // speed limiting and obstacle margins
    double consistency = 0.8;    // 1 = repeatable aim/feeds; low = mechanically inconsistent
    double reactionS = 0.35;
    double errorRate = 0.02;     // probability of a wrong decision per decision cycle
    double coordination = 0.6;   // respects partner zones / avoids partner
    double autoQuality = 0.7;
    double aimSigmaRad = 0.03;
    double maxSpeedFrac = 0.9;
    int shootAtHeld = 2;
    static Personality preset(const std::string& name);
    Json toJson() const;
    static Personality fromJson(const Json& j);
};

// Deterministic interpretation of the free-text alliance-partner instruction.
// Every phrase that changed behaviour is listed in `matched`; text that was not understood is
// listed in `unmatched` and surfaced to the user (never silently ignored).
struct StrategyDirectives {
    std::string text;
    bool idle = false;              // "do nothing", "stand still"
    bool parkAtEnd = true;
    double parkEarlyS = 12.0;
    double wPollen = 1.0, wHive = 1.0, wFlower = 1.0, wGarden = 0.3;
    double defend = -1.0;           // <0 => personality default
    double keepAwayFromPartner = 0.0;  // metres of extra clearance from the human-driven partner
    bool safeScoringOnly = false;
    bool stayOnOwnSide = false;
    std::string zone;               // "audience", "rear", "" (whole field)
    std::vector<std::string> matched, unmatched;
    static StrategyDirectives parse(const std::string& text);
    Json toJson() const;
};

class AIController {
public:
    AIController(Personality p, double difficulty, StrategyDirectives d, uint64_t seed, const std::string& name);
    virtual ~AIController() = default;
    virtual void update(World& w, int self, double t, double dt);
    bool intendsHiveRam() const { return false; }  // AI never rams the HIVE deliberately
    bool intendsCatch() const { return false; }
    const std::string& task() const { return task_; }
    const Personality& personality() const { return p_; }
    const StrategyDirectives& directives() const { return d_; }
    double difficulty() const { return difficulty_; }
    Json describe() const;
    int partner = -1;               // index of the alliance partner robot (for coordination)

private:
    struct Perceived { Vec2 pos; double heading; Vec2 vel; };
    Perceived perceiveSelf(World& w, int self);
    void drive(World& w, int self, Vec2 goal, double goalHeading, double speedFrac, bool faceGoal);
    void setMechanisms(World& w, int self, double t);
    bool solveShot(World& w, int self, const Vec3& target, double& hoodFrac, double& flySurface, double& yaw);
    int chooseElement(World& w, int self, double t, bool nectarOnly);
    Personality p_;
    double difficulty_;
    StrategyDirectives d_;
    Rng rng_;
    std::string name_, task_ = "idle";
    double nextDecision_ = 0;
    int targetElement_ = -1;
    Vec2 goal_;
    double goalHeading_ = 0;
    bool wantIntake_ = false, wantShoot_ = false, wantSpin_ = false;
    double flyTarget_ = 0, hoodTarget_ = 0.5;
    double aimError_ = 0;
    Vec2 localizationError_;
    double stuckTimer_ = 0;
    Vec2 lastPos_;
    int shotTargetFlower_ = -1;
    double flyInteg_ = 0;
};

}  // namespace gp
