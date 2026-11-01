#include "gameplan/ai.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "gameplan/world.hpp"

namespace gp {

Personality Personality::preset(const std::string& n) {
    Personality p;
    p.name = n;
    if (n == "aggressive") { p.aggression = 0.9; p.caution = 0.2; p.shootAtHeld = 1; p.maxSpeedFrac = 1.0; p.reactionS = 0.25; }
    else if (n == "defensive") { p.defense = 0.6; p.aggression = 0.6; p.caution = 0.3; }
    else if (n == "cautious") { p.caution = 0.9; p.maxSpeedFrac = 0.6; p.shootAtHeld = 3; p.aggression = 0.2; }
    else if (n == "inconsistent") { p.consistency = 0.35; p.aimSigmaRad = 0.09; p.errorRate = 0.12; }
    else if (n == "coordinated") { p.coordination = 1.0; p.errorRate = 0.01; p.reactionS = 0.2; p.aimSigmaRad = 0.02; }
    else if (n == "rookie") { p.reactionS = 0.8; p.errorRate = 0.2; p.aimSigmaRad = 0.12; p.autoQuality = 0.2; p.maxSpeedFrac = 0.6; p.consistency = 0.5; }
    else p.name = "balanced";
    return p;
}
Json Personality::toJson() const {
    Json j = Json::object();
    j.set("name", name); j.set("aggression", aggression); j.set("defense", defense); j.set("caution", caution);
    j.set("consistency", consistency); j.set("reactionS", reactionS); j.set("errorRate", errorRate); j.set("coordination", coordination);
    j.set("autoQuality", autoQuality); j.set("aimSigmaRad", aimSigmaRad); j.set("maxSpeedFrac", maxSpeedFrac); j.set("shootAtHeld", shootAtHeld);
    return j;
}
Personality Personality::fromJson(const Json& j) {
    Personality p = preset(j["name"].str("balanced"));
    p.aggression = j["aggression"].num(p.aggression); p.defense = j["defense"].num(p.defense); p.caution = j["caution"].num(p.caution);
    p.consistency = j["consistency"].num(p.consistency); p.reactionS = j["reactionS"].num(p.reactionS); p.errorRate = j["errorRate"].num(p.errorRate);
    p.coordination = j["coordination"].num(p.coordination); p.autoQuality = j["autoQuality"].num(p.autoQuality);
    p.aimSigmaRad = j["aimSigmaRad"].num(p.aimSigmaRad); p.maxSpeedFrac = j["maxSpeedFrac"].num(p.maxSpeedFrac);
    p.shootAtHeld = int(j["shootAtHeld"].num(p.shootAtHeld));
    return p;
}

// ------------------------------------------------------------------ instruction parsing
StrategyDirectives StrategyDirectives::parse(const std::string& text) {
    StrategyDirectives d;
    d.text = text;
    std::string t;
    for (char c : text) t += char(std::tolower((unsigned char)c));
    auto has = [&](const char* k) { return t.find(k) != std::string::npos; };
    auto hit = [&](const char* k, const std::string& effect) { d.matched.push_back(std::string("\"") + k + "\" -> " + effect); };
    if (t.empty()) return d;
    if (has("do nothing") || has("stand still") || has("don't move") || has("dont move") || has("stay still") || has("remain still")) {
        d.idle = true;
        d.parkAtEnd = false;
        hit(has("do nothing") ? "do nothing" : "stand still", "robot stays motionless for the entire MATCH (overrides all defaults, including PARK)");
    }
    if (has("pollen") && (has("prioriti") || has("focus") || has("collect"))) { d.wPollen = 3.0; hit("pollen", "POLLEN collection weight x3"); }
    if (has("nectar")) { d.wFlower = std::max(d.wFlower, 2.0); hit("nectar", "NECTAR/FLOWER weight x2"); }
    if (has("flower")) { d.wFlower = 3.0; hit("flower", "FLOWER scoring weight x3 in the last 60 s"); }
    if (has("hive") || has("tip")) { d.wHive = 3.0; hit(has("hive") ? "hive" : "tip", "HIVE launching weight x3"); }
    if (has("garden")) { d.wGarden = 2.0; hit("garden", "GARDEN element weight x2"); }
    if (has("defend") || has("defense") || has("defence") || has("block")) {
        d.defend = (has("only") || has("all")) ? 1.0 : 0.6;
        hit("defend", "legal defence share " + std::to_string(int(d.defend * 100)) + "% of TELEOP (no pins > 3 s)");
    }
    if (has("out of my way") || has("away from me") || has("give me space") || has("don't get in my way") || has("dont get in my way")) {
        d.keepAwayFromPartner = 1.0;
        hit("out of my way", "keeps >= 1.0 m extra clearance from the partner and avoids its target elements");
    }
    if (has("safe") || has("careful")) { d.safeScoringOnly = true; hit("safe", "launches only with no opponent within 1 m"); }
    if (has("our side") || has("own side") || has("my side")) { d.stayOnOwnSide = true; hit("own side", "stays on the alliance side of the FIELD"); }
    if (has("don't park") || has("dont park") || has("no park")) { d.parkAtEnd = false; hit("don't park", "does not PARK at the end"); }
    else if (has("park early")) { d.parkEarlyS = 25; hit("park early", "PARK starts with 25 s remaining"); }
    if (has("audience side")) { d.zone = "audience"; hit("audience side", "works the audience half (y<0)"); }
    if (has("back") && has("field") || has("rear side") || has("scoring side")) { d.zone = "rear"; hit("rear side", "works the rear half (y>0)"); }
    if (d.matched.empty()) d.unmatched.push_back(text);
    return d;
}
Json StrategyDirectives::toJson() const {
    Json j = Json::object();
    j.set("text", text); j.set("idle", idle); j.set("parkAtEnd", parkAtEnd); j.set("parkEarlyS", parkEarlyS);
    j.set("wPollen", wPollen); j.set("wHive", wHive); j.set("wFlower", wFlower); j.set("wGarden", wGarden);
    j.set("defend", defend); j.set("keepAwayFromPartner", keepAwayFromPartner); j.set("safeScoringOnly", safeScoringOnly);
    j.set("stayOnOwnSide", stayOnOwnSide); j.set("zone", zone);
    Json m = Json::array(); for (auto& s : matched) m.push(s); j.set("matched", m);
    Json u = Json::array(); for (auto& s : unmatched) u.push(s); j.set("notUnderstood", u);
    return j;
}

// ------------------------------------------------------------------ controller
AIController::AIController(Personality p, double difficulty, StrategyDirectives d, uint64_t seed, const std::string& name)
    : p_(p), difficulty_(clampd(difficulty, 0, 1)), d_(d), rng_(Rng::derive(seed, "ai:" + name)), name_(name) {
    // difficulty scales skill factors only
    p_.aimSigmaRad *= 1.6 - 1.2 * difficulty_;
    p_.reactionS *= 1.5 - difficulty_;
    p_.errorRate *= 1.5 - difficulty_;
    if (d_.defend >= 0) p_.defense = d_.defend;
}

Json AIController::describe() const {
    Json j = Json::object();
    j.set("name", name_); j.set("difficulty", difficulty_); j.set("personality", p_.toJson()); j.set("directives", d_.toJson());
    j.set("perception", "noisy pose/element estimates (no hidden state, no tip threshold, no opponent intent)");
    return j;
}

AIController::Perceived AIController::perceiveSelf(World& w, int self) {
    Robot& r = *w.robots()[self].robot;
    localizationError_ = localizationError_ * 0.995 + Vec2{rng_.normal(), rng_.normal()} * 0.0004;
    return {r.pos + localizationError_, r.heading, r.vel};
}

bool AIController::solveShot(World& w, int self, const Vec3& target, double& hoodFrac, double& flySurface, double& yaw) {
    Robot& r = *w.robots()[self].robot;
    const Mechanism* fly = nullptr;
    const Mechanism* hood = nullptr;
    for (auto& m : r.mechs) { if (m.type == MechType::Flywheel) fly = &m; if (m.type == MechType::Hood) hood = &m; }
    if (!fly) return false;
    Vec2 o = r.bodyToWorld({fly->launchFwd, 0});
    Vec2 dxy = target.xy() - o;
    double dx = dxy.norm(), dz = target.z - fly->launchHeight;
    yaw = std::atan2(dxy.y, dxy.x);
    double a0 = hood ? hood->angleAt0 : 0.9, a1 = hood ? hood->angleAt1 : 0.9;
    const GearedJoint& j = r.joints[fly->joint];
    double vmax = r.elec.motors[j.motor].spec.wFree() / j.ratio * fly->flyRadius * fly->launchEff * 0.85;
    double bestV = 1e9, bestA = a0;
    for (int k = 0; k <= 24; ++k) {
        double th = a0 + (a1 - a0) * k / 24.0;
        double den = 2 * std::cos(th) * std::cos(th) * (dx * std::tan(th) - dz);
        if (den <= 1e-6) continue;
        double v = std::sqrt(kG * dx * dx / den);
        double tf = dx / (v * std::cos(th));
        double vz = v * std::sin(th) - kG * tf;
        if (vz > -0.8) continue;  // must descend into the opening
        if (v < bestV) { bestV = v; bestA = th; }
    }
    if (bestV > vmax) return false;
    bestV *= 1.0 + 0.035 * dx;  // drag allowance (AI's own model of its launcher)
    hoodFrac = (a1 - a0) > 1e-6 ? (bestA - a0) / (a1 - a0) : 0.5;
    flySurface = bestV / fly->launchEff;
    return true;
}

int AIController::chooseElement(World& w, int self, double t, bool nectarOnly) {
    Robot& r = *w.robots()[self].robot;
    const SeasonDef& s = w.season();
    bool autoPhase = w.rules().phase() == Phase::Auto;
    int best = -1;
    double bestCost = 1e9;
    Vec2 partnerPos{99, 99};
    if (partner >= 0) partnerPos = w.robots()[partner].robot->pos;
    for (auto& e : w.elements()) {
        if (e.state != ElemState::Free || e.p.z > 0.12) continue;
        if (e.type == ElemType::Nectar && e.color != r.alliance) continue;  // G408: never control opponent NECTAR
        if (nectarOnly && e.type != ElemType::Nectar) continue;
        Vec2 p = e.p.xy();
        if ((autoPhase || d_.stayOnOwnSide) && s.sideOf(p.x) != r.alliance) continue;  // G402
        if (d_.zone == "audience" && p.y > 0.2) continue;
        if (d_.zone == "rear" && p.y < -0.2) continue;
        if (std::fabs(p.x) > s.half - 0.06 || std::fabs(p.y) > s.half - 0.06) {
            // against the wall: reachable only head-on; still allowed but costlier
        }
        double cost = (p - r.pos).norm();
        if (e.type == ElemType::Pollen) cost /= d_.wPollen;
        if (s.garden[int(r.alliance)].intersectsCircle(p, e.radius)) cost += 1.5 / d_.wGarden;   // keep own GARDEN points
        if (s.garden[int(other(r.alliance))].intersectsCircle(p, e.radius)) cost -= 0.3 * p_.aggression;
        double dp = (p - partnerPos).norm();
        if (dp < 0.8) cost += (0.8 - dp) * 3.0 * p_.coordination + d_.keepAwayFromPartner * 2.0;
        if (std::fabs(p.x) < 0.70 && std::fabs(p.y) < 0.55) cost += 0.6 * p_.caution;  // under the HIVE frame
        if (cost < bestCost) { bestCost = cost; best = e.id; }
    }
    return best;
}

// Liang-Barsky: does segment a->b pass through the axis-aligned box?
static bool segmentHitsBox(Vec2 a, Vec2 b, double x0, double x1, double y0, double y1) {
    double t0 = 0, t1 = 1;
    Vec2 d = b - a;
    const double p[4] = {-d.x, d.x, -d.y, d.y}, q[4] = {a.x - x0, x1 - a.x, a.y - y0, y1 - a.y};
    for (int i = 0; i < 4; ++i) {
        if (std::fabs(p[i]) < 1e-12) { if (q[i] < 0) return false; continue; }
        double t = q[i] / p[i];
        if (p[i] < 0) t0 = std::max(t0, t); else t1 = std::min(t1, t);
        if (t0 > t1) return false;
    }
    return true;
}

// Route around the HIVE frame like a driver would: if the straight line to the goal crosses the
// frame's footprint (legs + clearance), aim for the corner waypoint that gives the shortest
// unobstructed two-leg path. Goals inside the clearance band are driven to directly.
static Vec2 routeAroundHive(const SeasonDef& s, Vec2 from, Vec2 goal, double clearance) {
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (auto& leg : s.legs) {
        x0 = std::min(x0, leg.base.x); x1 = std::max(x1, leg.base.x);
        y0 = std::min(y0, leg.base.y); y1 = std::max(y1, leg.base.y);
    }
    if (s.legs.empty()) return goal;
    double bx0 = x0 - clearance, bx1 = x1 + clearance, by0 = y0 - clearance, by1 = y1 + clearance;
    auto inside = [&](Vec2 p) { return p.x > bx0 && p.x < bx1 && p.y > by0 && p.y < by1; };
    if (inside(goal) || inside(from) || !segmentHitsBox(from, goal, bx0, bx1, by0, by1)) return goal;
    const double m = 0.12;   // waypoint sits just outside the band
    Vec2 corners[4] = {{bx0 - m, by0 - m}, {bx1 + m, by0 - m}, {bx0 - m, by1 + m}, {bx1 + m, by1 + m}};
    Vec2 best = goal;
    double bestLen = 1e9;
    for (auto& c : corners) {
        if (std::fabs(c.x) > s.half - 0.25 || std::fabs(c.y) > s.half - 0.25) continue;
        if (segmentHitsBox(from, c, bx0, bx1, by0, by1)) continue;
        double len = (c - from).norm() + (goal - c).norm() + (segmentHitsBox(c, goal, bx0, bx1, by0, by1) ? 2.0 : 0.0);
        if (len < bestLen) { bestLen = len; best = c; }
    }
    return best;
}

void AIController::drive(World& w, int self, Vec2 goal, double goalHeading, double speedFrac, bool faceGoal) {
    Robot& r = *w.robots()[self].robot;
    Perceived me = perceiveSelf(w, self);
    const SeasonDef& s = w.season();
    double halfDiag = 0.5 * std::sqrt(r.cfg.length * r.cfg.length + r.cfg.width * r.cfg.width);
    Vec2 finalGoal = goal;
    goal = routeAroundHive(s, me.pos, goal, halfDiag + 0.12);
    bool waypoint = (goal - finalGoal).norm() > 1e-9;
    Vec2 to = goal - me.pos;
    double dist = to.norm();
    Vec2 vDes = dist > 0.02 ? to.normalized() * (waypoint ? 1.0 : std::min(1.0, dist / 0.35)) : Vec2{0, 0};
    // obstacle repulsion: robots, HIVE legs, FLOWERS, walls
    auto repel = [&](Vec2 c, double radius, double gain) {
        Vec2 d = me.pos - c;
        double n = d.norm();
        if (n < radius && n > 1e-6) vDes += d / n * (gain * (radius - n) / radius);
    };
    for (size_t k = 0; k < w.robots().size(); ++k) {
        if (int(k) == self) continue;
        double rad = 0.55 + 0.2 * p_.caution + (int(k) == partner ? d_.keepAwayFromPartner : 0.0);
        repel(w.robots()[k].robot->pos, rad, 1.2);
        // G421: drivers back off before a PIN is called (3 s); the sooner, the more careful
        if (w.robots()[k].robot->alliance != r.alliance && w.robotContactTime(self, int(k)) > 1.0 + 0.8 * (1.0 - p_.caution)) {
            Vec2 away = me.pos - w.robots()[k].robot->pos;
            if (away.norm() > 1e-6) vDes = away.normalized() * 1.0;
        }
    }
    // HIVE frame legs: repel from the leg's footprint between the floor and robot height
    for (auto& leg : s.legs) {
        double f = std::min(1.0, r.cfg.height / std::max(leg.apex.z, 1e-6));
        Vec2 a = leg.base.xy(), b = (leg.base + (leg.apex - leg.base) * f).xy();
        Vec2 ab = b - a;
        double u = clampd((me.pos - a).dot(ab) / std::max(ab.norm2(), 1e-9), 0.0, 1.0);
        repel(a + ab * u, 0.50 + 0.1 * p_.caution, 2.0);
    }
    // keep the intake away from opponent NECTAR (G408) while collecting
    if (wantIntake_)
        for (auto& e : w.elements())
            if (e.state == ElemState::Free && e.type == ElemType::Nectar && e.color != r.alliance) repel(e.p.xy(), 0.40, 0.8);
    for (auto& f : s.flowers) if ((f.axis - goal).norm() > 0.3) repel(f.axis, 0.32, 0.8);
    double legClear = 9;
    for (auto& leg : s.legs) legClear = std::min(legClear, (me.pos - leg.base.xy()).norm());
    if (legClear < 0.7) speedFrac = std::min(speedFrac, 0.45);
    double heading = faceGoal && dist > 0.15 ? std::atan2(to.y, to.x) : goalHeading;
    double eh = wrapAngle(heading - me.heading);
    double wCmd = clampd(2.2 * eh, -1.0, 1.0);
    Vec2 vb = vDes.rotated(-me.heading) * (speedFrac * p_.maxSpeedFrac);
    double vx = vb.x, vy = vb.y;
    std::vector<double> pw(4, 0.0);
    int idx[4] = {-1, -1, -1, -1};  // lf rf lb rb
    for (auto& wh : r.wheels) {
        int m = r.motorIndex(wh.name);
        int k = (wh.pos.x > 0 ? 0 : 2) + (wh.pos.y > 0 ? 0 : 1);
        idx[k] = m;
    }
    double lf = vx - vy - wCmd * 0.6, rf = vx + vy + wCmd * 0.6, lb = vx + vy - wCmd * 0.6, rb = vx - vy + wCmd * 0.6;
    double mx = std::max({1.0, std::fabs(lf), std::fabs(rf), std::fabs(lb), std::fabs(rb)});
    double v[4] = {lf / mx, rf / mx, lb / mx, rb / mx};
    for (int k = 0; k < 4; ++k) if (idx[k] >= 0) { r.ports[idx[k]].mode = RunMode::RunWithoutEncoder; r.ports[idx[k]].power = v[k] * r.motorSign[idx[k]]; }
}

void AIController::setMechanisms(World& w, int self, double t) {
    Robot& r = *w.robots()[self].robot;
    for (auto& m : r.mechs) {
        if (m.type == MechType::Intake && m.joint >= 0) r.ports[r.joints[m.joint].motor].power = wantIntake_ ? 1.0 : 0.0;
        if (m.type == MechType::Flywheel && m.joint >= 0) {
            int mi = r.joints[m.joint].motor;
            double cur = r.joints[m.joint].w * m.flyRadius;
            double maxS = r.elec.motors[mi].spec.wFree() / r.joints[m.joint].ratio * m.flyRadius;
            double ff = flyTarget_ / std::max(maxS, 1e-6) * 12.0 / std::max(r.elec.hubs[r.elec.motorHub[mi]].busV, 6.0);
            double e = flyTarget_ - cur;
            flyInteg_ = clampd(flyInteg_ + e * 0.02, -0.3, 0.3);
            r.ports[mi].power = wantSpin_ ? clampd(ff + 0.12 * e + flyInteg_, 0.0, 1.0) : 0.0;
            if (!wantSpin_) flyInteg_ = 0;
        }
        auto setServo = [&](int si, double pos) {
            auto& sv = r.elec.servos[si];
            sv.pwmEnabled = true;
            sv.pulseUs = 600 + 1800 * clampd(pos, 0, 1);
        };
        if (m.type == MechType::Hood && !m.servos.empty()) setServo(m.servos[0], hoodTarget_);
        if (m.type == MechType::Gate && !m.servos.empty()) setServo(m.servos[0], wantShoot_ ? 1.0 : 0.0);
    }
}

void AIController::update(World& w, int self, double t, double dt) {
    Robot& r = *w.robots()[self].robot;
    Phase ph = w.rules().phase();
    auto stopAll = [&]() {
        for (auto& p : r.ports) p.power = 0;
        wantIntake_ = wantShoot_ = wantSpin_ = false;
        // When the DS stops an OpMode the SDK zeroes motors and disables servo PWM; mirror that outside
        // AUTO/TELEOP so no powered motion occurs (G403/G404). Pre-match INIT keeps servos holding.
        if (ph != Phase::PreMatch) for (auto& s : r.elec.servos) s.pwmEnabled = false;
    };
    if (d_.idle || ph == Phase::PreMatch || ph == Phase::Transition || ph == Phase::Post || ph == Phase::Final) {
        task_ = d_.idle ? "idle (instruction)" : "waiting";
        stopAll();
        return;
    }
    if (t >= nextDecision_) {
        nextDecision_ = t + 0.05 + p_.reactionS * rng_.uniform(0.2, 0.6);
        const SeasonDef& s = w.season();
        double remaining = w.rules().remaining(t);
        bool flowersOpen = w.rules().flowerUnlocked(t);
        int capacity = 0;
        for (auto& m : r.mechs) if (m.type == MechType::Intake) capacity = m.capacity;
        int held = int(r.held.size());
        bool holdingOwnNectar = false;
        for (int e : r.held) if (w.elements()[e].type == ElemType::Nectar) holdingOwnNectar = true;
        // stuck recovery
        if ((r.pos - lastPos_).norm() < 0.04) stuckTimer_ += 0.05; else stuckTimer_ = 0;
        lastPos_ = r.pos;
        wantShoot_ = false;
        bool decideWrong = rng_.bernoulli(p_.errorRate);
        const AABB2& parkLz = s.loadingZone[int(r.alliance)];
        Vec2 parkGoal{(parkLz.xMin + parkLz.xMax) / 2 + (r.alliance == Alliance::Red ? 0.12 : -0.12), (parkLz.yMin + parkLz.yMax) / 2};
        // a driver leaves for PARK early enough for the trip: ~0.45 m/s effective average speed around
        // the HIVE and other robots, plus a few seconds of margin (scaled down for cautious drivers)
        double parkLeadS = std::max(d_.parkEarlyS, 4.0 + (parkGoal - r.pos).norm() / (0.35 + 0.2 * p_.maxSpeedFrac));
        if (ph == Phase::Teleop && d_.parkAtEnd && remaining < parkLeadS) {
            task_ = "park";
            goal_ = parkGoal;
            wantIntake_ = false;
            wantSpin_ = false;
        } else if (ph == Phase::Auto && w.rules().remaining(t) < 4.0 && p_.autoQuality > 0.5) {
            task_ = "auto-park";
            const AABB2& lz = s.loadingZone[int(r.alliance)];
            goal_ = {(lz.xMin + lz.xMax) / 2 + (r.alliance == Alliance::Red ? 0.12 : -0.12), (lz.yMin + lz.yMax) / 2};
            wantSpin_ = false;
        } else if (ph == Phase::Teleop && p_.defense > 0 && rng_.uniform() < p_.defense && held == 0) {
            task_ = "defend";
            // stand between the nearest opponent and its HIVE (legal zone defence, no pinning)
            int opp = -1;
            double bd = 1e9;
            for (size_t k = 0; k < w.robots().size(); ++k) {
                if (w.robots()[k].robot->alliance == r.alliance) continue;
                double d = (w.robots()[k].robot->pos - r.pos).norm();
                if (d < bd) { bd = d; opp = int(k); }
            }
            if (opp >= 0) {
                Vec2 op = w.robots()[opp].robot->pos;
                Vec3 cell = w.robots()[opp].robot->alliance == Alliance::Red ? Vec3{-0.32, 0, 1.4} : Vec3{0.32, 0, 1.4};
                Vec2 between = op + (cell.xy() - op).normalized() * 0.7;
                goal_ = between;
                if (w.robotContactTime(self, opp) > 1.2) goal_ = r.pos + (r.pos - op).normalized() * 0.8;  // break contact before PIN count
            }
            wantIntake_ = false;
            wantSpin_ = false;
        } else if (held > 0 && (held >= std::min(capacity, p_.shootAtHeld) || targetElement_ < 0 || (flowersOpen && holdingOwnNectar && d_.wFlower >= 1))) {
            // score: FLOWER with NECTAR in the last 60 s, otherwise our HIVE's up-facing CELL
            Vec3 target;
            bool flower = flowersOpen && holdingOwnNectar && w.elements()[r.held.back()].type == ElemType::Nectar;
            int fi = -1;
            if (flower) {
                double bd = 1e9;
                for (size_t f = 0; f < s.flowers.size(); ++f) {
                    double d = (s.flowers[f].axis - r.pos).norm();
                    if (d < bd) { bd = d; fi = int(f); }
                }
                target = {s.flowers[fi].axis.x, s.flowers[fi].axis.y, s.flowerTopH + 0.02};
                task_ = "score-flower";
            } else {
                const HiveState& hs = w.hive(r.alliance);
                double side = s.hives[int(r.alliance)].cells[hs.upCell].side == "audience" ? -1 : 1;
                target = {s.hives[int(r.alliance)].pivot.x, side * s.cellOffsetY, s.cellCaptureHeight};
                task_ = "score-hive";
            }
            // shooting spot: 1.3-1.7 m from the target on our side
            Vec2 dir = (r.pos - target.xy());
            if (dir.norm() < 1e-3) dir = {r.alliance == Alliance::Red ? -1.0 : 1.0, 0};
            dir = dir.normalized();
            double range = flower ? 0.9 : 1.45;
            Vec2 spot = target.xy() + dir * range;
            spot.x = clampd(spot.x, -s.half + 0.3, s.half - 0.3);
            spot.y = clampd(spot.y, -s.half + 0.3, s.half - 0.3);
            goal_ = spot;
            double hood, fly, yaw;
            if (solveShot(w, self, target, hood, fly, yaw)) {
                hoodTarget_ = hood;
                flyTarget_ = fly;
                wantSpin_ = true;
                if (std::fabs(aimError_) < 1e-9 || rng_.bernoulli(0.3)) aimError_ = rng_.normal(0, p_.aimSigmaRad);
                goalHeading_ = yaw + aimError_;
                double cur = 0;
                for (auto& m : r.mechs) if (m.type == MechType::Flywheel) cur = r.joints[m.joint].w * m.flyRadius;
                bool spun = std::fabs(cur - fly) < 0.025 * fly;
                bool aimed = std::fabs(wrapAngle(goalHeading_ - r.heading)) < 0.05;
                bool slow = r.vel.norm() < 0.25;
                bool safe = true;
                if (d_.safeScoringOnly)
                    for (auto& o : w.robots()) if (o.robot->alliance != r.alliance && (o.robot->pos - r.pos).norm() < 1.0) safe = false;
                bool inRange = (r.pos - spot).norm() < 0.35;
                wantShoot_ = spun && aimed && slow && safe && inRange && !decideWrong;
            } else {
                wantSpin_ = true;
                flyTarget_ = 3.0;
                goalHeading_ = std::atan2(target.y - r.pos.y, target.x - r.pos.x);
            }
            wantIntake_ = false;
            shotTargetFlower_ = fi;
        } else {
            task_ = "collect";
            if (targetElement_ < 0 || w.elements()[targetElement_].state != ElemState::Free || rng_.bernoulli(0.1) || decideWrong)
                targetElement_ = chooseElement(w, self, t, false);
            if (targetElement_ >= 0) {
                Vec2 p = w.elements()[targetElement_].p.xy();
                Vec2 approach = (p - r.pos).normalized();
                goal_ = p - approach * (r.cfg.length / 2 + 0.02);
                goal_ += approach * 0.12;   // drive through it with the intake
                goalHeading_ = std::atan2(approach.y, approach.x);
            } else {
                // nothing free: go retrieve POLLEN from the nearest FLOWER bottom (allowed by G418.B)
                double bd = 1e9;
                int fi = -1;
                for (size_t f = 0; f < s.flowers.size(); ++f) {
                    const auto& st = w.flowers()[f].stack;
                    if (st.empty() || w.elements()[st.front()].type != ElemType::Pollen) continue;
                    if ((ph == Phase::Auto || d_.stayOnOwnSide) && s.sideOf(s.flowers[f].axis.x) != r.alliance) continue;
                    double d = (s.flowers[f].axis - r.pos).norm();
                    if (d < bd) { bd = d; fi = int(f); }
                }
                if (fi >= 0) {
                    Vec2 ax = s.flowers[fi].axis;
                    Vec2 inward = Vec2{-ax.x, -ax.y}.normalized();
                    goal_ = ax + inward * (r.cfg.length / 2 + s.flowerOuterR + 0.01);
                    goalHeading_ = std::atan2(-inward.y, -inward.x);
                    task_ = "flower-retrieve";
                } else {
                    goal_ = r.pos;
                    task_ = "hold";
                }
            }
            wantIntake_ = true;
            wantSpin_ = held > 0 && p_.aggression > 0.6;
            flyTarget_ = 4.0;
        }
        if (stuckTimer_ > 1.5) {
            goal_ = r.pos + Vec2{rng_.uniform(-0.6, 0.6), rng_.uniform(-0.6, 0.6)};
            stuckTimer_ = 0;
            targetElement_ = -1;
        }
    }
    bool face = task_ == "collect";
    double speed = (task_ == "score-hive" || task_ == "score-flower") && (r.pos - goal_).norm() < 0.4 ? 0.35 : 1.0;
    drive(w, self, goal_, goalHeading_, speed * (1.0 - 0.4 * p_.caution) + 0.4 * p_.caution * 0.6, face);
    setMechanisms(w, self, t);
}

}  // namespace gp
