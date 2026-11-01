#include "gameplan/rules.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>

#include "gameplan/world.hpp"
#include "gameplan/ai.hpp"

namespace gp {

const char* toString(Phase p) {
    switch (p) {
        case Phase::PreMatch: return "PRE_MATCH";
        case Phase::Auto: return "AUTO";
        case Phase::Transition: return "TRANSITION";
        case Phase::Teleop: return "TELEOP";
        case Phase::Post: return "POST_MATCH";
        case Phase::Final: return "FINAL";
    }
    return "?";
}

Json RefereeConfig::toJson() const {
    Json j = Json::object();
    j.set("profile", profile);
    j.set("strategicRepeatThreshold", strategicRepeatThreshold);
    j.set("hiveRamSpeed", hiveRamSpeed);
    j.set("stochastic", stochastic);
    j.set("missProbability", missProbability);
    return j;
}
RefereeConfig RefereeConfig::fromJson(const Json& j) {
    RefereeConfig c;
    c.profile = j["profile"].str("standard");
    c.strategicRepeatThreshold = int(j["strategicRepeatThreshold"].num(3));
    c.hiveRamSpeed = j["hiveRamSpeed"].num(0.9);
    c.stochastic = j["stochastic"].boolean(false);
    c.missProbability = j["missProbability"].num(0.0);
    return c;
}

bool RefereeModel::strategic(const std::string& rule, int robot, int n, bool intent, std::string& why) {
    std::ostringstream s;
    bool st = false;
    if (intent) {
        st = cfg_.stochastic ? rng_.bernoulli(0.85) : true;
        s << rule << ": intent signal present (controller declared a deliberate action) -> "
          << (st ? "perceived STRATEGIC" : "not perceived STRATEGIC (referee variance draw)");
    } else if (n >= cfg_.strategicRepeatThreshold) {
        st = cfg_.stochastic ? rng_.bernoulli(0.75) : true;
        s << rule << ": occurrence " << n << " >= REPEATED threshold " << cfg_.strategicRepeatThreshold
          << " (Manual 10.6: repeated warnings invite STRATEGIC scrutiny) -> " << (st ? "perceived STRATEGIC" : "not STRATEGIC (variance)");
    } else {
        s << rule << ": occurrence " << n << " < REPEATED threshold " << cfg_.strategicRepeatThreshold
          << " and no intent signal -> treated as accidental/unforeseeable (not STRATEGIC)";
    }
    s << " [referee profile=" << cfg_.profile << ", stochastic=" << (cfg_.stochastic ? "yes" : "no") << "]";
    why = s.str();
    return st;
}
bool RefereeModel::notices(std::string& why) {
    if (!cfg_.stochastic || cfg_.missProbability <= 0) { why = "objective call (always observed in deterministic referee profile)"; return true; }
    bool seen = !rng_.bernoulli(cfg_.missProbability);
    why = seen ? "observed by referee" : "missed by referee (variance draw)";
    return seen;
}

MatchRules::MatchRules(const SeasonDef& s, const RefereeConfig& rc, uint64_t seed) : season_(s), ref_(rc, seed) {}

void MatchRules::startMatch(double t) {
    tStart_ = t;
    phase_ = Phase::Auto;
    cueIndex_ = 0;
}

void MatchRules::requestStop(double t, const std::string& why) {
    stopped_ = true;
    cueLog_.push_back("match_stopped@" + std::to_string(matchTime(t)) + " Foghorn: " + why);
    phase_ = Phase::Post;
    settleStart_ = t;
}

double MatchRules::remaining(double t) const {
    double mt = matchTime(t);
    switch (phase_) {
        case Phase::PreMatch: return season_.autoS;
        case Phase::Auto: return std::max(0.0, season_.autoS - mt);
        case Phase::Transition: return std::max(0.0, season_.autoS + season_.transitionS - mt);
        case Phase::Teleop: return std::max(0.0, season_.matchLength() - mt);
        default: return 0;
    }
}
std::string MatchRules::timerText(double t) const {
    double shown;
    double mt = matchTime(t);
    switch (phase_) {
        case Phase::PreMatch: shown = season_.teleopS + season_.autoS; break;
        case Phase::Auto: shown = season_.teleopS + std::max(0.0, season_.autoS - mt); break;
        case Phase::Transition: shown = std::ceil(std::max(0.0, season_.autoS + season_.transitionS - mt)); break;
        case Phase::Teleop: shown = std::max(0.0, season_.matchLength() - mt); break;
        default: shown = 0;
    }
    int s = int(std::ceil(shown - 1e-9));
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}
bool MatchRules::driverControlAllowed(double t, bool competition) const {
    if (!competition) return phase_ == Phase::Auto || phase_ == Phase::Teleop;
    return phase_ == Phase::Teleop;
}
bool MatchRules::flowerUnlocked(double t) const {
    return phase_ == Phase::Teleop && remaining(t) <= season_.flowerUnlockRemaining + 1e-9;
}

int MatchRules::count(const std::string& rule, int robot) const {
    auto it = occurrences_.find(rule + ":" + std::to_string(robot));
    return it == occurrences_.end() ? 0 : it->second;
}

void MatchRules::credit(Alliance a, const std::string& kind, int pts, const std::string& period, double t, int robot, const std::string& detail) {
    if (pts == 0) return;
    AllianceScore& s = score_[int(a)];
    if (period == "AUTO") s.autoPts += pts;
    else if (period == "TELEOP") s.teleopPts += pts;
    else s.endPts += pts;
    s.breakdown[kind] += pts;
    events_.push_back({t, matchTime(t), a, kind, pts, robot, period, detail});
}

void MatchRules::addViolation(World& w, const std::string& rule, int robot, const std::string& base, const std::string& strategicPenalty,
                              bool judgment, bool intent, const std::string& detail, double t, int perInstanceFoul) {
    if (phase_ == Phase::PreMatch || phase_ == Phase::Final) return;
    std::string key = rule + ":" + std::to_string(robot);
    int n = ++occurrences_[key];
    Violation v;
    v.t = t;
    v.matchTime = matchTime(t);
    v.rule = rule;
    v.robot = robot;
    v.offender = robot >= 0 ? w.robots()[robot].robot->alliance : Alliance::Red;
    v.judgment = judgment;
    std::string why;
    std::string penalty = base;
    if (judgment) {
        bool st = ref_.strategic(rule, robot, n, intent, why);
        if (st) penalty = strategicPenalty;
    } else {
        if (!ref_.notices(why)) return;
    }
    v.penalty = penalty;
    v.rationale = detail + " | " + why;
    if (penalty.find("MAJOR_FOUL") != std::string::npos) v.foulPoints += season_.points("MAJOR_FOUL", 20);
    if (penalty.find("MINOR_FOUL") != std::string::npos) v.foulPoints += season_.points("MINOR_FOUL", 5);
    v.foulPoints += perInstanceFoul;
    v.card = penalty.find("CARD") != std::string::npos;
    if (penalty == "NONE") return;
    score_[int(other(v.offender))].foulCredit += v.foulPoints;
    violations_.push_back(v);
}

void MatchRules::onTip(World& w, Alliance a, double t) {
    bool autoPeriod = t < teleopStart();
    credit(a, "HIVE_TIP", season_.points(autoPeriod ? "HIVE_TIP.auto" : "HIVE_TIP.teleop", 20), autoPeriod ? "AUTO" : "TELEOP", t, -1,
           std::string("HIVE TIP ") + toString(a));
    score_[int(a)].tips++;
}

void MatchRules::onNectarEnteredFlower(World& w, int element, int flower, double t) {
    if (flowerUnlocked(t)) return;
    if (phase_ == Phase::Post || phase_ == Phase::Final) return;
    auto& e = w.elements()[element];
    e.enteredFlowerEarly = true;
    int robot = e.lastRobot;
    if (robot < 0) return;
    addViolation(w, "G410", robot, "MAJOR_FOUL", "MAJOR_FOUL", false, false,
                 "NECTAR entered FLOWER scoring volume with more than 60 s remaining", t);
}

void MatchRules::onHumanNectarEntry(World& w, Alliance a, double t, bool permitted) {
    if (permitted) return;
    // attribute to the first robot of that alliance for alliance bookkeeping
    int robot = -1;
    for (size_t i = 0; i < w.robots().size(); ++i) if (w.robots()[i].robot->alliance == a) { robot = int(i); break; }
    addViolation(w, "G426", robot, "MINOR_FOUL", "MINOR_FOUL", false, false, "human entered NECTAR before it was unlocked", t);
}

void MatchRules::onElementLeftField(World& w, int element, double t) {
    auto& e = w.elements()[element];
    if (e.lastRobot < 0 || t - e.lastRobotTime > 3.0) return;
    addViolation(w, "G405", e.lastRobot, "NONE", "MAJOR_FOUL", true, false,
                 "scoring element left the FIELD after robot contact/launch (penalised only if perceived deliberate)", t);
}

void MatchRules::onHiveContact(World& w, int robot, double speed, double t) {
    std::string key = "hivecontact:" + std::to_string(robot);
    if (timers_.count(key) && t - timers_[key] < 1.0) return;  // one contact episode
    timers_[key] = t;
    if (speed < ref_.config().hiveRamSpeed) return;
    bool intent = w.robots()[robot].ai && w.robots()[robot].ai->intendsHiveRam();
    if (flags_["G417:" + std::to_string(robot) + ":major"]) {
        addViolation(w, "G417", robot, "VERBAL_WARNING", "VERBAL_WARNING", true, false, "repeat HIVE frame contact (per-MATCH major already assessed)", t);
        return;
    }
    size_t before = violations_.size();
    char d[96];
    std::snprintf(d, sizeof d, "robot contacted HIVE frame at %.2f m/s (ramming threshold %.2f m/s)", speed, ref_.config().hiveRamSpeed);
    addViolation(w, "G417", robot, "VERBAL_WARNING", "MAJOR_FOUL+YELLOW_CARD", true, intent, d, t);
    if (violations_.size() > before && violations_.back().penalty.find("MAJOR") != std::string::npos) flags_["G417:" + std::to_string(robot) + ":major"] = true;
}

void MatchRules::checkContinuousRules(World& w, double t, double dt) {
    auto& rs = w.robots();
    for (size_t i = 0; i < rs.size(); ++i) {
        Robot& r = *rs[i].robot;
        std::string id = std::to_string(i);
        // 0.25 s grace: the DS stops AUTO at 0:00 and commands need one control cycle to reach the motors
        if (phase_ == Phase::Transition && t - autoEnd() > 0.25 && r.anyPoweredOutput() && !flags_["G403:" + id]) {
            flags_["G403:" + id] = true;
            addViolation(w, "G403", int(i), "VERBAL_WARNING", "MAJOR_FOUL+YELLOW_CARD", true, false,
                         "powered actuator output during the AUTO-to-TELEOP transition", t);
        }
        if (phase_ == Phase::Post && t - matchEnd() > 0.25 && r.anyPoweredOutput() && !flags_["G404:" + id]) {
            flags_["G404:" + id] = true;
            addViolation(w, "G404", int(i), "VERBAL_WARNING", "MAJOR_FOUL+YELLOW_CARD", true, false, "powered movement after the end of TELEOP", t);
        }
        if (phase_ == Phase::Auto || phase_ == Phase::Teleop) {
            if (r.held.size() > 4 && !flags_["G407:" + id]) {
                flags_["G407:" + id] = true;
                addViolation(w, "G407", int(i), "VERBAL_WARNING", "MAJOR_FOUL+YELLOW_CARD", true, false,
                             "robot CONTROLS " + std::to_string(r.held.size()) + " SCORING ELEMENTS (limit 4)", t);
            }
            for (int e : r.held) {
                const Element& el = w.elements()[e];
                if (el.type == ElemType::Nectar && el.color != r.alliance) {
                    std::string k = "G408:" + id + ":" + std::to_string(e);
                    if (!flags_[k]) {
                        flags_[k] = true;
                        addViolation(w, "G408", int(i), "VERBAL_WARNING", "YELLOW_CARD", true, false, "robot CONTROLS opponent NECTAR", t);
                    }
                }
            }
        }
    }
    // G402: AUTO interference on the opponent's side
    if (phase_ == Phase::Auto) {
        for (size_t i = 0; i < rs.size(); ++i)
            for (size_t j = 0; j < rs.size(); ++j) {
                if (i == j) continue;
                Robot& a = *rs[i].robot;
                Robot& b = *rs[j].robot;
                if (a.alliance == b.alliance || !w.robotsInContact(int(i), int(j))) continue;
                if (season_.sideOf(a.pos.x) != a.alliance && season_.sideOf(b.pos.x) == b.alliance && !flags_["G402:" + std::to_string(i)]) {
                    flags_["G402:" + std::to_string(i)] = true;
                    addViolation(w, "G402", int(i), "MAJOR_FOUL", "MAJOR_FOUL+YELLOW_CARD", true, false,
                                 "AUTO contact with an opponent robot on the opponent's side of the FIELD", t);
                }
            }
    }
    // G421 PIN 3-count
    if (phase_ == Phase::Auto || phase_ == Phase::Teleop) {
        for (size_t i = 0; i < rs.size(); ++i)
            for (size_t j = 0; j < rs.size(); ++j) {
                if (i == j) continue;
                Robot& a = *rs[i].robot;
                Robot& b = *rs[j].robot;
                if (a.alliance == b.alliance) continue;
                std::string k = "pin:" + std::to_string(i) + ":" + std::to_string(j);
                double effortB = 0;
                for (auto& m : b.elec.motors) effortB = std::max(effortB, std::fabs(m.duty));
                double slipB = 0;
                for (auto& wh : b.wheels) slipB += std::fabs(wh.slip);
                bool contact = w.robotsInContact(int(i), int(j));
                Vec2 toB = (b.pos - a.pos).normalized();
                bool aPushing = a.vel.dot(toB) >= -0.02 && a.vel.norm() >= b.vel.norm() - 0.05;
                bool bPinned = contact && b.vel.norm() < 0.12 && (effortB > 0.3 || slipB > 0.3) && aPushing;
                double& tm = timers_[k];
                double& sep = timers_[k + ":sep"];
                if (bPinned) {
                    if (tm == 0) { timers_[k + ":x"] = b.pos.x; timers_[k + ":y"] = b.pos.y; }
                    tm += dt;
                    sep = 0;
                    int fouls = int(std::floor(tm / 3.0));
                    int done = int(timers_[k + ":fouls"]);
                    if (fouls > done) {
                        timers_[k + ":fouls"] = fouls;
                        addViolation(w, "G421", int(i), "MAJOR_FOUL", "MAJOR_FOUL", false, false,
                                     done == 0 ? "PIN exceeded 3 s" : "PIN not corrected: additional 3 s", t);
                    }
                } else if (tm > 0) {
                    double dist = (a.pos - b.pos).norm();
                    double moved = (b.pos - Vec2{timers_[k + ":x"], timers_[k + ":y"]}).norm();
                    if (dist > 0.61 || moved > 0.61) sep += dt;   // G421.A / G421.B: count pauses
                    if (sep > 3.0) { tm = 0; sep = 0; timers_[k + ":fouls"] = 0; }
                }
            }
    }
}

void MatchRules::assessAutoEnd(World& w, double t) {
    auto& rs = w.robots();
    for (size_t i = 0; i < rs.size(); ++i) {
        Robot& r = *rs[i].robot;
        if (!w.robotTouchesWall(int(i))) {
            credit(r.alliance, "LEAVE", season_.points("LEAVE.auto", 3), "AUTO", t, int(i), r.label + " not contacting the perimeter");
            score_[int(r.alliance)].leavePark += season_.points("LEAVE.auto", 3);
        }
        if (w.robotInLoadingZone(int(i))) {
            credit(r.alliance, "PARK_AUTO", season_.points("PARK.auto", 5), "AUTO", t, int(i), r.label + " in LOADING ZONE");
            score_[int(r.alliance)].leavePark += season_.points("PARK.auto", 5);
        }
    }
}

void MatchRules::assessFinal(World& w, double t) {
    auto& rs = w.robots();
    for (size_t i = 0; i < rs.size(); ++i) {
        Robot& r = *rs[i].robot;
        if (w.robotInLoadingZone(int(i))) {
            credit(r.alliance, "PARK_TELEOP", season_.points("PARK.teleop", 5), "TELEOP", t, int(i), r.label + " in LOADING ZONE");
            score_[int(r.alliance)].leavePark += season_.points("PARK.teleop", 5);
        }
    }
    // POLLEN/NECTAR remaining in up-facing CELLS
    for (int h = 0; h < 2; ++h) {
        const HiveState& hs = w.hive(Alliance(h));
        int n = int(hs.contents[hs.upCell].size());
        credit(Alliance(h), "CELL_REMAINING", n * season_.points("CELL_REMAINING.end", 2), "END", t, -1,
               std::to_string(n) + " elements in up-facing CELL");
    }
    // FLOWERS
    const auto& els = w.elements();
    for (size_t f = 0; f < w.flowers().size(); ++f) {
        const auto& st = w.flowers()[f].stack;
        std::vector<int> scoring;
        for (int id : st) {
            const Element& e = els[id];
            if (e.p.z + e.radius > season_.flowerZMin && e.p.z - e.radius < season_.flowerZMax) scoring.push_back(id);
        }
        int topNectar = -1, bottomNectar = -1;
        for (int id : scoring) if (els[id].type == ElemType::Nectar) { if (bottomNectar < 0) bottomNectar = id; topNectar = id; }
        if (bottomNectar >= 0)
            credit(els[bottomNectar].color, "BOTTOM_NECTAR_BONUS", season_.points("BOTTOM_NECTAR_BONUS.end", 5), "END", t, -1,
                   season_.flowers[f].id);
        if (topNectar >= 0)
            credit(els[topNectar].color, "OWNED_FLOWER", int(scoring.size()) * season_.points("OWNED_FLOWER_ELEMENT.end", 2), "END", t, -1,
                   season_.flowers[f].id + " owned with " + std::to_string(scoring.size()) + " scoring elements");
    }
    // GARDENS
    for (int a = 0; a < 2; ++a) {
        int n = 0;
        for (auto& e : els) {
            if (e.state != ElemState::Free) continue;
            if (e.p.z > e.radius * 2.5) continue;
            if (season_.garden[a].intersectsCircle(e.p.xy(), e.radius)) ++n;
        }
        credit(Alliance(a), "GARDEN", n * season_.points("GARDEN_ELEMENT.end", 1), "END", t, -1, std::to_string(n) + " elements in GARDEN");
    }
    computeRP();
}

void MatchRules::computeRP() {
    for (int a = 0; a < 2; ++a) {
        AllianceScore& s = score_[a];
        s.rp = 0;
        s.rpDetail.clear();
        if (s.leavePark >= season_.swarmThreshold) { s.rp++; s.rpDetail.push_back("SWARM"); }
        if (s.tips >= season_.poll1Threshold) { s.rp++; s.rpDetail.push_back("POLLINATOR_1"); }
        if (s.tips >= season_.poll2Threshold) { s.rp++; s.rpDetail.push_back("POLLINATOR_2"); }
    }
    int r = score_[0].total(), b = score_[1].total();
    if (r > b) { score_[0].rp += 3; score_[0].rpDetail.push_back("WIN"); }
    else if (b > r) { score_[1].rp += 3; score_[1].rpDetail.push_back("WIN"); }
    else { score_[0].rp += 1; score_[1].rp += 1; score_[0].rpDetail.push_back("TIE"); score_[1].rpDetail.push_back("TIE"); }
}

void MatchRules::update(World& w, double t, double dt) {
    if (phase_ == Phase::PreMatch || phase_ == Phase::Final) return;
    double mt = matchTime(t);
    // audio cues (informational only; the timer is authoritative)
    const Json& cues = season_.raw["audioCues"];
    while (cueIndex_ < int(cues.size())) {
        double at = cues[cueIndex_]["atMatchTime_s"].num();
        if (at < 0) { ++cueIndex_; continue; }
        if (mt + 1e-9 < at) break;
        cueLog_.push_back(cues[cueIndex_]["id"].str() + "@" + std::to_string(at) + " " + cues[cueIndex_]["cue"].str());
        ++cueIndex_;
    }
    if (phase_ == Phase::Auto && mt >= season_.autoS) {
        assessAutoEnd(w, t);
        phase_ = Phase::Transition;
    }
    if (phase_ == Phase::Transition && mt >= season_.autoS + season_.transitionS) phase_ = Phase::Teleop;
    if (phase_ == Phase::Teleop && mt >= season_.matchLength()) {
        phase_ = Phase::Post;
        settleStart_ = t;
    }
    checkContinuousRules(w, t, dt);
    if (phase_ == Phase::Post) {
        // assess once everything has come to rest (10.5.C/D/E), bounded wait
        bool rest = true;
        for (auto& e : w.elements()) if (e.state == ElemState::Free && e.v.norm() > 0.05) rest = false;
        for (auto& r : w.robots()) if (r.robot->vel.norm() > 0.05) rest = false;
        for (int h = 0; h < 2; ++h) if (w.hive(Alliance(h)).tipping) rest = false;
        if ((rest && t - settleStart_ > 0.5) || t - settleStart_ > 6.0) {
            assessFinal(w, t);
            phase_ = Phase::Final;
        }
    }
}

Json MatchRules::resultJson() const {
    Json j = Json::object();
    j.set("phase", toString(phase_));
    j.set("stopped", stopped_);
    for (int a = 0; a < 2; ++a) {
        const AllianceScore& s = score_[a];
        Json o = Json::object();
        o.set("total", s.total()); o.set("auto", s.autoPts); o.set("teleop", s.teleopPts); o.set("end", s.endPts);
        o.set("foulCredit", s.foulCredit); o.set("tips", s.tips); o.set("leavePark", s.leavePark); o.set("rp", s.rp);
        Json bd = Json::object();
        for (auto& kv : s.breakdown) bd.set(kv.first, kv.second);
        o.set("breakdown", bd);
        Json rp = Json::array();
        for (auto& d : s.rpDetail) rp.push(d);
        o.set("rpDetail", rp);
        j.set(toString(Alliance(a)), o);
    }
    Json ev = Json::array();
    for (auto& e : events_) {
        Json o = Json::object();
        o.set("t", e.t); o.set("matchTime", e.matchTime); o.set("alliance", toString(e.alliance)); o.set("kind", e.kind);
        o.set("points", e.points); o.set("robot", e.robot); o.set("period", e.period); o.set("detail", e.detail);
        ev.push(o);
    }
    j.set("scoreEvents", ev);
    Json vs = Json::array();
    for (auto& v : violations_) {
        Json o = Json::object();
        o.set("t", v.t); o.set("matchTime", v.matchTime); o.set("rule", v.rule); o.set("robot", v.robot);
        o.set("offender", toString(v.offender)); o.set("penalty", v.penalty); o.set("foulPoints", v.foulPoints);
        o.set("card", v.card); o.set("judgment", v.judgment); o.set("rationale", v.rationale);
        vs.push(o);
    }
    j.set("violations", vs);
    Json cl = Json::array();
    for (auto& c : cueLog_) cl.push(c);
    j.set("cues", cl);
    j.set("referee", ref_.config().toJson());
    return j;
}

Json MatchRules::snapshot() const {
    Json j = resultJson();
    j.set("tStart", tStart_);
    j.set("settleStart", settleStart_);
    j.set("phaseId", int(phase_));
    j.set("cueIndex", cueIndex_);
    Json oc = Json::object();
    for (auto& kv : occurrences_) oc.set(kv.first, kv.second);
    j.set("occ", oc);
    Json tm = Json::object();
    for (auto& kv : timers_) tm.set(kv.first, kv.second);
    j.set("timers", tm);
    Json fl = Json::object();
    for (auto& kv : flags_) fl.set(kv.first, kv.second);
    j.set("flags", fl);
    Rng::State s = ref_.rngState();
    Json r = Json::array();
    for (int i = 0; i < 4; ++i) r.push(std::to_string(s.s[i]));
    r.push(s.spare); r.push(s.hasSpare);
    j.set("refRng", r);
    return j;
}

void MatchRules::restore(const Json& j) {
    tStart_ = j["tStart"].num();
    settleStart_ = j["settleStart"].num();
    phase_ = Phase(j["phaseId"].integer());
    cueIndex_ = j["cueIndex"].integer();
    stopped_ = j["stopped"].boolean();
    occurrences_.clear(); timers_.clear(); flags_.clear();
    for (auto& kv : j["occ"].members()) occurrences_[kv.first] = kv.second.integer();
    for (auto& kv : j["timers"].members()) timers_[kv.first] = kv.second.num();
    for (auto& kv : j["flags"].members()) flags_[kv.first] = kv.second.boolean();
    for (int a = 0; a < 2; ++a) {
        const Json& o = j[toString(Alliance(a))];
        AllianceScore& s = score_[a];
        s.foulCredit = o["foulCredit"].integer(); s.tips = o["tips"].integer(); s.leavePark = o["leavePark"].integer(); s.rp = o["rp"].integer();
        s.autoPts = o["auto"].integer(); s.teleopPts = o["teleop"].integer(); s.endPts = o["end"].integer();
        s.breakdown.clear();
        for (auto& kv : o["breakdown"].members()) s.breakdown[kv.first] = kv.second.integer();
    }
    events_.clear();
    for (auto& e : j["scoreEvents"].items())
        events_.push_back({e["t"].num(), e["matchTime"].num(), parseAlliance(e["alliance"].str()), e["kind"].str(), e["points"].integer(),
                           e["robot"].integer(), e["period"].str(), e["detail"].str()});
    violations_.clear();
    for (auto& v : j["violations"].items()) {
        Violation x;
        x.t = v["t"].num(); x.matchTime = v["matchTime"].num(); x.rule = v["rule"].str(); x.robot = v["robot"].integer();
        x.offender = parseAlliance(v["offender"].str()); x.penalty = v["penalty"].str(); x.foulPoints = v["foulPoints"].integer();
        x.card = v["card"].boolean(); x.judgment = v["judgment"].boolean(); x.rationale = v["rationale"].str();
        violations_.push_back(x);
    }
    cueLog_.clear();
    for (auto& c : j["cues"].items()) cueLog_.push_back(c.str());
    Rng::State s;
    for (int i = 0; i < 4; ++i) s.s[i] = std::stoull(j["refRng"][i].str());
    s.spare = j["refRng"][4].num(); s.hasSpare = j["refRng"][5].boolean();
    ref_.setRngState(s);
}

}  // namespace gp
