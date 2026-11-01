#include "gameplan/error.hpp"
#include "gameplan/world.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "gameplan/ai.hpp"

namespace gp {

namespace {
constexpr double kWallK = 4.0e4, kWallC = 600.0;   // perimeter compliance (barrier slack emerges from k)
constexpr double kRobotK = 3.0e4, kRobotC = 500.0;
}  // namespace

Json WorldConfig::toJson() const {
    Json j = Json::object();
    j.set("seed", double(seed));
    j.set("dt", dt);
    j.set("fidelity", fidelity);
    j.set("uncertainty", uncertainty.toJson());
    Json e = Json::object();
    e.set("humidity", env.humidity); e.set("ambientC", env.ambientC); e.set("antiStatic", env.antiStaticTreated);
    e.set("tileMuScale", env.tileMuScale);
    j.set("env", e);
    j.set("referee", referee.toJson());
    j.set("competitionMode", competitionMode);
    return j;
}

double HiveState::angle() const { return 0.0; }

World::World(const SeasonDef& season, const Components& comps, const WorldConfig& cfg)
    : season_(season), comps_(comps), cfg_(cfg), sampler_(cfg.seed, cfg.uncertainty),
      rngElements_(Rng::derive(cfg.seed, "elements")), rngFaults_(Rng::derive(cfg.seed, "faults")),
      rngHuman_(Rng::derive(cfg.seed, "human")) {
    for (int a = 0; a < 2; ++a) {
        ParamSpec p;
        p.name = std::string("field.hive_threshold.") + toString(Alliance(a));
        p.subsystem = "field";
        p.nominal = season_.tipThreshold;
        p.ideal = season_.tipThreshold;
        p.spread = season_.tipThresholdSigma;
        p.adverse = +1;
        hives_[a].threshold = sampler_.sample(p);
        hives_[a].stateAngle = season_.hives[a].stateAngle;
        hives_[a].initialUp = season_.hives[a].initialUpCell;
        hives_[a].upCell = hives_[a].initialUp;
        humans_[a].alliance = Alliance(a);
    }
    ParamSpec mu;
    mu.name = "field.tile_mu_scale"; mu.subsystem = "field"; mu.nominal = 1.0; mu.ideal = 1.0; mu.dist = Dist::LogNormal;
    mu.spread = 0.07; mu.adverse = -1;
    cfg_.env.tileMuScale *= sampler_.sample(mu);
    flowers_.resize(season_.flowers.size());
    rules_.reset(new MatchRules(season_, cfg_.referee, cfg_.seed));
}

World::~World() = default;

int World::addRobot(const RobotConfig& rc, Alliance a, const std::string& label, Pose2 start, bool runtime,
                    std::shared_ptr<AIController> ai) {
    RobotSlot s;
    s.robot.reset(new Robot());
    Robot& r = *s.robot;
    r.cfg = rc;
    r.alliance = a;
    r.label = label;
    r.isAI = ai != nullptr;
    r.elec.ambient = cfg_.env.ambientC;
    r.build(comps_, sampler_, label + ".");
    r.pos = {start.x, start.y};
    r.heading = start.heading;
    r.startX = start.x;
    r.startY = start.y;
    s.sensors.reset(new SensorSuite(r, sampler_, cfg_.seed, label + "."));
    s.runtimeControlled = runtime;
    s.ai = ai;
    ParamSpec ps;
    ps.name = label + ".esd.susceptibility"; ps.subsystem = "esd"; ps.nominal = 1.0; ps.ideal = 0.0; ps.dist = Dist::LogNormal;
    ps.spread = 0.6; ps.adverse = +1;
    s.esdSusceptibility = sampler_.sample(ps);
    robots_.push_back(std::move(s));
    contactTime_.assign(robots_.size(), std::vector<double>(robots_.size(), 0.0));
    intakeLoadUntil_.assign(robots_.size(), -1.0);
    intakeLoad_.assign(robots_.size(), 0.0);
    return int(robots_.size()) - 1;
}

void World::stageField() {
    elements_.clear();
    int id = 0;
    auto mk = [&](ElemType t, Alliance c) {
        Element e;
        e.id = id++;
        e.type = t;
        e.color = c;
        e.radius = (t == ElemType::Pollen ? season_.pollenD : season_.nectarD) / 2;
        Rng r = Rng::derive(cfg_.seed, "element-mass:" + std::to_string(e.id));
        double nominal = t == ElemType::Pollen ? season_.pollenMass : season_.nectarMass;
        double sigma = t == ElemType::Pollen ? season_.pollenMassSigma : season_.nectarMassSigma;
        ULevel l = cfg_.uncertainty.level("elements");
        bool varied = l == ULevel::Realistic || l == ULevel::Extreme;
        e.mass = varied ? std::max(0.5 * nominal, r.normal(nominal, sigma)) : nominal;
        // Manual 9.8: elements are not perfectly spherical and vary in size
        const BallPhys& bp = t == ElemType::Pollen ? season_.pollenPhys : season_.nectarPhys;
        if (varied) e.radius += clampd(r.normal(0, bp.radiusSigma), -3 * bp.radiusSigma, 3 * bp.radiusSigma);
        // arbitrary resting orientation (the hole pattern is not aligned between balls)
        double u1 = r.uniform(), u2 = r.uniform(), u3 = r.uniform();
        e.q[0] = std::sqrt(1 - u1) * std::sin(2 * kPi * u2); e.q[1] = std::sqrt(1 - u1) * std::cos(2 * kPi * u2);
        e.q[2] = std::sqrt(u1) * std::sin(2 * kPi * u3); e.q[3] = std::sqrt(u1) * std::cos(2 * kPi * u3);
        return e;
    };
    // FLOWERS: 4 POLLEN stacked from the tile
    for (size_t f = 0; f < season_.flowers.size(); ++f) {
        flowers_[f].stack.clear();
        for (int k = 0; k < 4; ++k) {
            Element e = mk(ElemType::Pollen, Alliance::Red);
            e.state = ElemState::InFlower;
            e.container = int(f);
            flowers_[f].stack.push_back(e.id);
            elements_.push_back(e);
        }
    }
    // GARDENS
    for (int a = 0; a < 2; ++a)
        for (auto& p : season_.gardenPollen[a]) {
            Element e = mk(ElemType::Pollen, Alliance::Red);
            e.p = p;
            elements_.push_back(e);
        }
    // Robot pre-loads (4 POLLEN in or touching each robot)
    for (size_t r = 0; r < robots_.size(); ++r) {
        Robot& rb = *robots_[r].robot;
        int cap = 0;
        for (auto& m : rb.mechs) if (m.type == MechType::Intake) cap = m.capacity;
        for (int k = 0; k < season_.preloadPerRobot; ++k) {
            Element e = mk(ElemType::Pollen, Alliance::Red);
            if (k < cap) {
                e.state = ElemState::Held;
                e.holder = int(r);
                // resting in the ball path, queued from the shooter end
                e.pathS = ballPathLength(int(r)) - e.radius - 0.0005 - k * (season_.pollenD + 0.0005);
                e.p = {rb.pos.x, rb.pos.y, 0.15};
                rb.held.push_back(e.id);
            } else {
                // placed on the tiles contacting the robot, behind it, on its own side
                Vec2 back = rb.bodyToWorld({-rb.cfg.length / 2 - e.radius - 0.002, (k - 1.5) * 0.075});
                e.p = {back.x, back.y, e.radius};
            }
            elements_.push_back(e);
        }
    }
    // NECTAR: 3 in each initially-up CELL, 5 in each ALLIANCE AREA
    for (int a = 0; a < 2; ++a) {
        HiveState& h = hives_[a];
        h.contents[0].clear();
        h.contents[1].clear();
        h.weight = 0;
        for (auto& p : season_.cellNectar[a]) {
            Element e = mk(ElemType::Nectar, Alliance(a));
            e.state = ElemState::InCell;
            e.container = a * 2 + h.upCell;
            e.p = p;
            h.contents[h.upCell].push_back(e.id);
            h.weight += season_.nectarTipW;
            elements_.push_back(e);
        }
        humans_[a].waitingIds.clear();
        humans_[a].nectarInArea = season_.allianceAreaNectar;
        for (int k = 0; k < season_.allianceAreaNectar; ++k) {
            Element e = mk(ElemType::Nectar, Alliance(a));
            e.state = ElemState::HumanArea;
            const AABB2& area = season_.allianceArea[a];
            e.p = {(area.xMin + area.xMax) / 2, area.yMin + 0.3 + 0.1 * k, e.radius};
            humans_[a].waitingIds.push_back(e.id);
            elements_.push_back(e);
        }
    }
}

// ---------------------------------------------------------------------------------------
Vec3 World::cellOpeningCenter(int h, int c) const {
    const HiveDef& hd = season_.hives[h];
    double side = hd.cells[c].side == "audience" ? -1.0 : 1.0;
    return {hd.pivot.x, side * season_.cellOffsetY, season_.cellCaptureHeight};
}

std::vector<int> World::allTagIds() const {
    std::vector<int> ids;
    for (int h = 0; h < 2; ++h) for (int c = 0; c < 2; ++c) for (int t : season_.hives[h].cells[c].tags) ids.push_back(t);
    return ids;
}

bool World::tagCell(int tagId, int& hive, int& cell, int& k) const {
    for (int h = 0; h < 2; ++h)
        for (int c = 0; c < 2; ++c) {
            const auto& tags = season_.hives[h].cells[c].tags;
            for (size_t i = 0; i < tags.size(); ++i)
                if (tags[i] == tagId) { hive = h; cell = c; k = int(i); return true; }
        }
    return false;
}

World::ClusterFrame World::clusterFrame(int h, int c) const {
    const HiveDef& hd = season_.hives[h];
    const HiveState& hs = hives_[h];
    const CellDef& cd = hd.cells[c];
    // downness of this cell: 0 when up, 1 when down, interpolated while tipping
    double down = (hs.upCell == c) ? 0.0 : 1.0;
    if (hs.tipping) {
        double f = clampd((t_ - hs.tipStart) / season_.tipSettleS, 0, 1);
        down = (hs.upCell == c) ? f : 1.0 - f;
    }
    double sigma = cd.side == "audience" ? 1.0 : -1.0;
    double phi = sigma * down * hs.stateAngle;
    // up-state axes: n0 = outward tag normal (towards a viewer), "up" in the sticker plane from world up
    Vec3 n0 = cd.upClusterNormal.normalized();
    Vec3 wz{0, 0, 1};
    Vec3 yUp = (wz - n0 * wz.dot(n0)).normalized();
    ClusterFrame f;
    f.yd = yUp * -1.0;
    f.za = n0 * -1.0;
    f.xr = f.yd.cross(f.za);
    // upClusterCenter (CAD) is the centre of the tag row; the frame origin sits at minus the member mean
    Vec3 mean{0, 0, 0};
    for (auto& m : season_.clusterMembers) mean = mean + m;
    if (!season_.clusterMembers.empty()) mean = mean * (1.0 / double(season_.clusterMembers.size()));
    f.origin = cd.upClusterCenter - (f.xr * mean.x + f.yd * mean.y + f.za * mean.z);
    auto rot = [&](Vec3 v) {
        double y = v.y, z = v.z;
        return Vec3{v.x, y * std::cos(phi) - z * std::sin(phi), y * std::sin(phi) + z * std::cos(phi)};
    };
    f.origin = hd.pivot + rot(f.origin - hd.pivot);
    f.xr = rot(f.xr); f.yd = rot(f.yd); f.za = rot(f.za);
    return f;
}

Vec3 World::tagPosition(int tagId, Vec3* normal) const {
    int h, c, k;
    if (!tagCell(tagId, h, c, k)) GP_THROW(std::runtime_error("unknown AprilTag id " + std::to_string(tagId)));
    ClusterFrame f = clusterFrame(h, c);
    Vec3 m = size_t(k) < season_.clusterMembers.size() ? season_.clusterMembers[size_t(k)] : Vec3{0, 0, 0};
    if (normal) *normal = f.za * -1.0;
    return f.origin + f.xr * m.x + f.yd * m.y + f.za * m.z;
}

bool World::robotTouchesWall(int r, double margin) const {
    Vec2 c[4];
    robots_[r].robot->footprint().corners(c);
    for (auto& p : c)
        if (std::fabs(p.x) > season_.half - margin || std::fabs(p.y) > season_.half - margin) return true;
    return false;
}
bool World::robotInLoadingZone(int r) const {
    const Robot& rb = *robots_[r].robot;
    return season_.loadingZone[int(rb.alliance)].intersectsOBB(rb.footprint());
}
double World::robotContactTime(int a, int b) const { return contactTime_[a][b]; }
bool World::robotsInContact(int a, int b) const { return contactTime_[a][b] > 0; }

// ---------------------------------------------------------------------------------------
void World::advanceTo(double t, const std::function<void()>& perStep) {
    while (t_ + cfg_.dt * 0.5 < t) {
        if (perStep) perStep();
        step();
    }
}

void World::step() {
    const double dt = cfg_.dt;
    for (size_t r = 0; r < robots_.size(); ++r)
        if (robots_[r].ai) robots_[r].ai->update(*this, int(r), t_, dt);
    for (size_t r = 0; r < robots_.size(); ++r) {
        Robot& rb = *robots_[r].robot;
        if (intakeLoadUntil_[r] > t_)
            for (auto& m : rb.mechs)
                if (m.type == MechType::Intake && m.joint >= 0) {
                    rb.joints[m.joint].loadTorque += intakeLoad_[r] * sgn(rb.joints[m.joint].w);
                    rb.joints[m.joint].loadDamping += 0.002;
                }
        size_t evBefore = rb.elec.events.size();
        bool hubWasReset[8] = {false};
        for (size_t h = 0; h < rb.elec.hubs.size() && h < 8; ++h) hubWasReset[h] = rb.elec.hubs[h].inReset;
        rb.step(dt, t_, cfg_.env.tileMuScale);
        // Electrical consequences that are system faults (brownout => controller resets)
        for (size_t k = evBefore; k < rb.elec.events.size(); ++k) {
            auto& ev = rb.elec.events[k];
            if (ev.kind == "brownout") {
                int hi = rb.elec.hubIndex(ev.device);
                bool isRC = hi >= 0 && rb.elec.hubs[hi].spec.role == "robot-controller";
                FaultEvent f{t_, int(r), isRC ? "rc_crash" : "hub_reset", ev.device, isRC ? -1.0 : rb.elec.hubs[hi].spec.rebootS, 0,
                             "brownout: hub bus fell to " + std::to_string(ev.value) + " V"};
                if (isRC) rb.rcAlive = false;
                faults_.push_back(f);
                if (onFault) onFault(f);
            }
            if (ev.kind == "fuse_blown") {
                FaultEvent f{t_, int(r), "power_loss", "main-fuse", -1.0, 0, "20 A main fuse I^2t exceeded"};
                rb.rcAlive = false;
                faults_.push_back(f);
                if (onFault) onFault(f);
            }
        }
        // hub recovered => SDK re-initialises: modes reset, encoder counts restart at 0
        for (size_t h = 0; h < rb.elec.hubs.size() && h < 8; ++h) {
            if (hubWasReset[h] && !rb.elec.hubs[h].inReset) {
                for (size_t m = 0; m < rb.ports.size(); ++m) {
                    if (rb.elec.motorHub[m] != int(h)) continue;
                    auto& pc = rb.ports[m];
                    pc.mode = RunMode::RunWithoutEncoder;
                    pc.power = 0;
                    pc.velocityCommand = false;
                    pc.encoderOffset = (long long)std::llround(rb.encoderTicks(int(m)));
                }
                for (size_t s = 0; s < rb.elec.servos.size(); ++s)
                    if (rb.elec.servoHub[s] == int(h)) rb.elec.servos[s].pwmEnabled = false;
                if (onEvent) { Json d = Json::object(); d.set("hub", rb.elec.hubs[h].id); onEvent("hub_recovered", int(r), d); }
            }
        }
    }
    collideRobots(dt);
    stepBalls(dt);
    stepHives(dt);
    stepHumans(dt);
    stepFaults(dt);
    for (size_t r = 0; r < robots_.size(); ++r) {
        robots_[r].sensors->step(*this, dt);
        if (robots_[r].sensors->visionActive) robots_[r].sensors->captureFrames(*this, int(r));
    }
    rules_->update(*this, t_, dt);
    ++steps_;
    t_ = double(steps_) * dt;   // integer step count: no accumulated floating-point drift
}

// ---------------------------------------------------------------------------------------
void World::collideRobots(double dt) {
    const double half = season_.half;
    const size_t n = robots_.size();
    for (size_t i = 0; i < n; ++i) {
        Robot& r = *robots_[i].robot;
        OBB2 fb = r.footprint();
        Vec2 corners[4];
        fb.corners(corners);
        // perimeter walls (spring-damper contacts at penetrating corners)
        for (auto& c : corners) {
            Vec2 pv = r.vel + Vec2{-r.omega * (c - r.pos).y, r.omega * (c - r.pos).x};
            for (int axis = 0; axis < 2; ++axis)
                for (int sgnw = -1; sgnw <= 1; sgnw += 2) {
                    double coord = axis == 0 ? c.x : c.y;
                    double pen = sgnw * coord - half;
                    if (pen <= 0) continue;
                    Vec2 nrm = axis == 0 ? Vec2{double(-sgnw), 0} : Vec2{0, double(-sgnw)};
                    double vn = pv.dot(nrm);
                    double fn = std::max(0.0, kWallK * pen - kWallC * vn);
                    Vec2 tang = nrm.perp();
                    double vt = pv.dot(tang);
                    Vec2 f = nrm * fn + tang * (-0.3 * fn * std::tanh(vt / 0.05));
                    r.applyImpulse(f * dt, c);
                }
        }
        // HIVE frame legs (metal; also ESD discharge paths)
        double hR = r.cfg.height;
        for (auto& leg : season_.legs) {
            double f = std::min(1.0, hR / std::max(leg.apex.z, 1e-6));
            Vec2 a = leg.base.xy(), b = (leg.base + (leg.apex - leg.base) * f).xy();
            double best = 1e9;
            Vec2 bp, sp;
            for (int k = 0; k <= 6; ++k) {
                Vec2 q = a + (b - a) * (k / 6.0);
                Vec2 cp = fb.closest(q);
                double d = (q - cp).norm();
                if (fb.contains(q)) d = -0.01;
                if (d < best) { best = d; bp = cp; sp = q; }
            }
            if (best < season_.legRadius) {
                Vec2 nrm = (bp - sp).normalized();
                if (nrm.norm2() < 0.5) nrm = (r.pos - sp).normalized();
                double pen = season_.legRadius - best;
                Vec2 pv = r.vel + Vec2{-r.omega * (bp - r.pos).y, r.omega * (bp - r.pos).x};
                double vn = pv.dot(nrm);
                if (vn < -0.05) rules_->onHiveContact(*this, int(i), -vn, t_);
                double fn = std::max(0.0, kWallK * pen - kWallC * vn);
                r.applyImpulse(nrm * fn * dt, bp);
                if (vn < -0.05 && onEvent) { Json d = Json::object(); d.set("speed", -vn); d.set("what", "hive_frame"); onEvent("contact", int(i), d); }
            }
        }
        // FLOWERS (rigid bodies mounted on the perimeter)
        for (auto& fl : season_.flowers) {
            Vec2 cp = fb.closest(fl.axis);
            double d = (fl.axis - cp).norm();
            if (d < season_.flowerOuterR) {
                Vec2 nrm = (cp - fl.axis).normalized();
                if (nrm.norm2() < 0.5) nrm = (r.pos - fl.axis).normalized();
                Vec2 pv = r.vel + Vec2{-r.omega * (cp - r.pos).y, r.omega * (cp - r.pos).x};
                double fn = std::max(0.0, kWallK * (season_.flowerOuterR - d) - kWallC * pv.dot(nrm));
                r.applyImpulse(nrm * fn * dt, cp);
            }
        }
    }
    // robot-robot
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) {
            Robot& a = *robots_[i].robot;
            Robot& b = *robots_[j].robot;
            Vec2 nrm, cp;
            double depth;
            if (obbOverlap(a.footprint(), b.footprint(), nrm, depth, cp)) {
                Vec2 va = a.vel + Vec2{-a.omega * (cp - a.pos).y, a.omega * (cp - a.pos).x};
                Vec2 vb = b.vel + Vec2{-b.omega * (cp - b.pos).y, b.omega * (cp - b.pos).x};
                double vn = (vb - va).dot(nrm);  // <0 approaching
                if (contactTime_[i][j] == 0 && vn < -0.3) {
                    a.collisions++; b.collisions++;
                    if (onEvent) { Json d = Json::object(); d.set("other", int(j)); d.set("speed", -vn); onEvent("robot_collision", int(i), d); }
                }
                double fn = std::max(0.0, kRobotK * depth - kRobotC * vn);
                Vec2 tang = nrm.perp();
                double vt = (vb - va).dot(tang);
                Vec2 f = nrm * fn + tang * (-0.4 * fn * std::tanh(vt / 0.05));
                b.applyImpulse(f * dt, cp);
                a.applyImpulse(-f * dt, cp);
                contactTime_[i][j] += dt;
                contactTime_[j][i] = contactTime_[i][j];
            } else {
                contactTime_[i][j] = contactTime_[j][i] = 0;
            }
        }
}

// ---------------------------------------------------------------------------------------
void World::releaseFromCell(int h, int c) {
    HiveState& hs = hives_[h];
    const HiveDef& hd = season_.hives[h];
    double side = hd.cells[c].side == "audience" ? -1.0 : 1.0;
    for (size_t k = 0; k < hs.contents[c].size(); ++k) {
        Element& e = elements_[hs.contents[c][k]];
        e.state = ElemState::Free;
        e.container = -1;
        e.p = cellSlot(h, c, int(k));
        e.v = {rngElements_.normal(0, 0.3), side * rngElements_.uniform(0.2, 0.9), rngElements_.uniform(-0.5, 0.2)};
        e.fromTip = true;
        e.launched = false;
    }
    hs.contents[c].clear();
}

void World::stepHives(double dt) {
    for (int h = 0; h < 2; ++h) {
        HiveState& hs = hives_[h];
        if (!hs.tipping && hs.weight >= hs.threshold) {
            hs.tipping = true;
            hs.tipStart = t_;
            hs.spilled = false;
            if (onEvent) { Json d = Json::object(); d.set("hive", toString(Alliance(h))); d.set("weight", hs.weight); d.set("threshold", hs.threshold); onEvent("tip_start", -1, d); }
        }
        if (hs.tipping) {
            double f = (t_ - hs.tipStart) / season_.tipSettleS;
            if (f >= 0.35 && !hs.spilled) { releaseFromCell(h, hs.upCell); hs.spilled = true; }
            if (f >= 1.0) {
                // damper contacts the frame: TIP complete (10.5.1)
                hs.upCell = 1 - hs.upCell;
                hs.tipping = false;
                hs.tips++;
                hs.weight = 0;
                for (int id : hs.contents[hs.upCell]) hs.weight += elements_[id].type == ElemType::Pollen ? season_.pollenTipW : season_.nectarTipW;
                humans_[h].unlocked++;
                rules_->onTip(*this, Alliance(h), t_);
            }
        }
    }
}

void World::stepHumans(double dt) {
    Phase ph = rules_->phase();
    if (ph != Phase::Auto && ph != Phase::Transition && ph != Phase::Teleop) return;
    for (int a = 0; a < 2; ++a) {
        HumanPlayer& hp = humans_[a];
        if (hp.waitingIds.empty() || t_ < hp.nextActionTime) continue;
        bool unlocked60 = rules_->flowerUnlocked(t_);
        bool permitted = hp.unlocked > 0 || unlocked60;
        if (!permitted && hp.obeyRules) continue;
        const AABB2& lz = season_.loadingZone[a];
        Vec2 drop{(lz.xMin + lz.xMax) / 2 + rngHuman_.uniform(-0.05, 0.05), (lz.yMin + lz.yMax) / 2 + rngHuman_.uniform(-0.15, 0.15)};
        bool blocked = false;
        for (auto& s : robots_) if (s.robot->footprint().contains(drop, 0.06)) blocked = true;
        if (blocked) { hp.nextActionTime = t_ + 0.5; continue; }
        int id = hp.waitingIds.back();
        hp.waitingIds.pop_back();
        hp.nectarInArea--;
        Element& e = elements_[id];
        e.state = ElemState::Free;
        e.p = {drop.x, drop.y, e.radius};
        e.v = {0, 0, 0};
        e.fromTip = false;
        if (!unlocked60 && hp.unlocked > 0) hp.unlocked--;
        rules_->onHumanNectarEntry(*this, Alliance(a), t_, permitted);
        hp.nextActionTime = t_ + rngHuman_.lognormal(hp.reactionS, 0.3);
    }
}

void World::stepFaults(double dt) {
    ULevel lvl = cfg_.uncertainty.level("esd");
    if (lvl == ULevel::Disabled) return;
    const double rh = clampd(cfg_.env.humidity, 0.05, 0.95);
    const double tau = clampd(60.0 * std::exp(-(rh - 0.20) / 0.12), 0.5, 120.0);  // leakage time constant
    const double cRobot = 150e-12;                                                // frame capacitance to ground (F)
    double tribo = 2.0e-9 * (cfg_.env.antiStaticTreated ? 0.4 : 1.0);           // C per metre rolled (estimate)
    if (lvl == ULevel::Extreme) tribo *= 2.5;
    for (size_t r = 0; r < robots_.size(); ++r) {
        RobotSlot& s = robots_[r];
        Robot& rb = *s.robot;
        double v = rb.vel.norm();
        double slipTerm = 0;
        for (auto& w : rb.wheels) slipTerm += std::fabs(w.slip);
        s.esdCharge += tribo * (v + 0.5 * slipTerm) * dt;
        s.esdCharge -= s.esdCharge * dt / tau;
        // discharge paths: HIVE frame (metal), other robots (potential difference)
        auto discharge = [&](double dq, const std::string& path) {
            double V = std::fabs(dq) / cRobot;
            if (V < 2000) return;
            double energy = 0.5 * cRobot * V * V * 1000.0;  // mJ
            FaultEvent ev{t_, int(r), "esd_discharge", path, 0, energy, "contact discharge"};
            faults_.push_back(ev);
            if (onFault) onFault(ev);
            double pFault = clampd(s.esdSusceptibility * (1 - std::exp(-energy / 40.0)) * 0.35, 0, 0.95);
            if (rngFaults_.bernoulli(pFault)) {
                double u = rngFaults_.uniform();
                FaultEvent f{t_, int(r), "", "", 0, energy, "ESD via " + path};
                if (u < 0.55) {
                    int hi = rb.elec.hubs.size() > 1 ? 1 : 0;
                    f.kind = "hub_comm_loss"; f.target = rb.elec.hubs[hi].id; f.duration = rngFaults_.lognormal(0.8, 0.9);
                    rb.elec.hubs[hi].inReset = true; rb.elec.hubs[hi].resetUntil = t_ + f.duration;
                } else if (u < 0.75) {
                    f.kind = "hub_reset"; f.target = rb.elec.hubs[0].id; f.duration = rb.elec.hubs[0].spec.rebootS;
                    rb.elec.hubs[0].inReset = true; rb.elec.hubs[0].resetUntil = t_ + f.duration;
                } else if (u < 0.92) {
                    f.kind = "ds_disconnect"; f.target = "driver-station"; f.duration = rngFaults_.lognormal(3.0, 0.7);
                    rb.disabled = true;  // OpMode stopped by loss of DS heartbeat; restored by link layer policy
                } else {
                    f.kind = "rc_crash"; f.target = "robot-controller"; f.duration = -1;
                    rb.rcAlive = false;
                }
                faults_.push_back(f);
                if (onFault) onFault(f);
            }
        };
        bool legContact = false;
        OBB2 fb = rb.footprint();
        for (auto& leg : season_.legs) {
            Vec2 q = leg.base.xy();
            if ((fb.closest(q) - q).norm() < season_.legRadius + 0.005) legContact = true;
        }
        if (prevLegContact_.size() != robots_.size()) prevLegContact_.assign(robots_.size(), 0);
        if (legContact && !prevLegContact_[r]) { discharge(s.esdCharge, "hive_frame"); s.esdCharge *= 0.05; }
        prevLegContact_[r] = legContact;
        for (size_t o = r + 1; o < robots_.size(); ++o) {
            if (contactTime_[r][o] > 0 && contactTime_[r][o] <= dt * 1.5) {
                double dq = (s.esdCharge - robots_[o].esdCharge) / 2;
                discharge(dq, "robot_contact:" + robots_[o].robot->label);
                double avg = (s.esdCharge + robots_[o].esdCharge) / 2;
                s.esdCharge = robots_[o].esdCharge = avg;
            }
        }
        // DS reconnection: drivers re-run TELEOP after reconnect (policy documented in ELECTRICAL_MODEL.md)
        if (rb.disabled) {
            for (auto it = faults_.rbegin(); it != faults_.rend(); ++it) {
                if (it->robot == int(r) && it->kind == "ds_disconnect") {
                    if (t_ > it->t + it->duration + 4.0 && rules_->phase() == Phase::Teleop) rb.disabled = false;
                    break;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------------------
Json World::snapshot() const {
    Json j = Json::object();
    j.set("t", t_);
    j.set("steps", double(steps_));
    Json rs = Json::array();
    for (auto& s : robots_) {
        Json o = s.robot->snapshot();
        o.set("esdCharge", s.esdCharge);
        rs.push(o);
    }
    j.set("robots", rs);
    Json es = Json::array();
    for (auto& e : elements_) {
        Json o = Json::array();
        for (double v : {e.p.x, e.p.y, e.p.z, e.v.x, e.v.y, e.v.z, double(int(e.state)), double(e.holder), double(e.container),
                         double(e.lastRobot), e.fromTip ? 1.0 : 0.0, e.reentryAt, e.mass, e.w.x, e.w.y, e.w.z,
                         e.q[0], e.q[1], e.q[2], e.q[3], e.pathS, e.pathV, e.radius})
            o.push(v);
        es.push(o);
    }
    j.set("elements", es);
    Json hs = Json::array();
    for (auto& h : hives_) {
        Json o = Json::object();
        o.set("up", h.upCell); o.set("tipping", h.tipping); o.set("tipStart", h.tipStart); o.set("spilled", h.spilled);
        o.set("weight", h.weight); o.set("threshold", h.threshold); o.set("tips", h.tips);
        for (int c = 0; c < 2; ++c) { Json a = Json::array(); for (int id : h.contents[c]) a.push(id); o.set(c ? "c1" : "c0", a); }
        hs.push(o);
    }
    j.set("hives", hs);
    Json fs = Json::array();
    for (auto& f : flowers_) { Json a = Json::array(); for (int id : f.stack) a.push(id); fs.push(a); }
    j.set("flowers", fs);
    Json hu = Json::array();
    for (auto& h : humans_) {
        Json o = Json::object();
        o.set("n", h.nectarInArea); o.set("unlocked", h.unlocked); o.set("next", h.nextActionTime);
        Json w = Json::array(); for (int id : h.waitingIds) w.push(id); o.set("ids", w);
        hu.push(o);
    }
    j.set("humans", hu);
    j.set("rules", rules_->snapshot());
    auto rngJ = [](const Rng& r) {
        Rng::State s = r.state();
        Json a = Json::array();
        for (int i = 0; i < 4; ++i) a.push(std::to_string(s.s[i]));
        a.push(s.spare); a.push(s.hasSpare);
        return a;
    };
    j.set("rngElements", rngJ(rngElements_));
    j.set("rngFaults", rngJ(rngFaults_));
    j.set("rngHuman", rngJ(rngHuman_));
    j.set("faultCount", double(faults_.size()));
    j.set("launchCount", double(launches.size()));
    return j;
}

void World::restore(const Json& j) {
    t_ = j["t"].num();
    steps_ = (long long)j["steps"].num();
    for (size_t i = 0; i < robots_.size(); ++i) {
        robots_[i].robot->restore(j["robots"][i]);
        robots_[i].esdCharge = j["robots"][i]["esdCharge"].num();
    }
    for (size_t i = 0; i < elements_.size(); ++i) {
        const Json& o = j["elements"][i];
        auto& e = elements_[i];
        e.p = {o[0].num(), o[1].num(), o[2].num()}; e.v = {o[3].num(), o[4].num(), o[5].num()};
        e.state = ElemState(o[6].integer()); e.holder = o[7].integer(); e.container = o[8].integer();
        e.lastRobot = o[9].integer(); e.fromTip = o[10].num() != 0; e.reentryAt = o[11].num(); e.mass = o[12].num();
        if (o.size() >= 22) {
            e.w = {o[13].num(), o[14].num(), o[15].num()};
            for (int q = 0; q < 4; ++q) e.q[q] = o[16 + q].num();
            e.pathS = o[20].num(); e.pathV = o[21].num();
            if (o.size() >= 23) e.radius = o[22].num();
        }
    }
    for (int h = 0; h < 2; ++h) {
        const Json& o = j["hives"][h];
        auto& hs = hives_[h];
        hs.upCell = o["up"].integer(); hs.tipping = o["tipping"].boolean(); hs.tipStart = o["tipStart"].num();
        hs.spilled = o["spilled"].boolean(); hs.weight = o["weight"].num(); hs.threshold = o["threshold"].num(); hs.tips = o["tips"].integer();
        for (int c = 0; c < 2; ++c) { hs.contents[c].clear(); for (auto& x : o[c ? "c1" : "c0"].items()) hs.contents[c].push_back(x.integer()); }
    }
    for (size_t f = 0; f < flowers_.size(); ++f) {
        flowers_[f].stack.clear();
        for (auto& x : j["flowers"][f].items()) flowers_[f].stack.push_back(x.integer());
    }
    for (int a = 0; a < 2; ++a) {
        const Json& o = j["humans"][a];
        humans_[a].nectarInArea = o["n"].integer(); humans_[a].unlocked = o["unlocked"].integer(); humans_[a].nextActionTime = o["next"].num();
        humans_[a].waitingIds.clear();
        for (auto& x : o["ids"].items()) humans_[a].waitingIds.push_back(x.integer());
    }
    rules_->restore(j["rules"]);
    auto rngR = [](Rng& r, const Json& a) {
        Rng::State s;
        for (int i = 0; i < 4; ++i) s.s[i] = std::stoull(a[i].str());
        s.spare = a[4].num(); s.hasSpare = a[5].boolean();
        r.setState(s);
    };
    rngR(rngElements_, j["rngElements"]);
    rngR(rngFaults_, j["rngFaults"]);
    rngR(rngHuman_, j["rngHuman"]);
    faults_.resize(size_t(j["faultCount"].num()));
    launches.resize(size_t(j["launchCount"].num()));
}

std::string World::stateHash() const { return fnv1a64Hex(snapshot().dump()); }

}  // namespace gp
