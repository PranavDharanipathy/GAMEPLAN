#include "gameplan/error.hpp"
#include "gameplan/world.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "gameplan/ai.hpp"

namespace gp {

namespace {
constexpr double kAirRho = 1.2, kBallCd = 0.47;
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
        e.mass = (l == ULevel::Realistic || l == ULevel::Extreme) ? std::max(0.5 * nominal, r.normal(nominal, sigma)) : nominal;
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

Vec3 World::tagPosition(int tagId, Vec3* normal) const {
    for (int h = 0; h < 2; ++h) {
        const HiveDef& hd = season_.hives[h];
        const HiveState& hs = hives_[h];
        for (int c = 0; c < 2; ++c) {
            const CellDef& cd = hd.cells[c];
            for (size_t k = 0; k < cd.tags.size(); ++k) {
                if (cd.tags[k] != tagId) continue;
                // downness of this cell: 0 when up, 1 when down, interpolated while tipping
                double down = (hs.upCell == c) ? 0.0 : 1.0;
                if (hs.tipping) {
                    double f = clampd((t_ - hs.tipStart) / season_.tipSettleS, 0, 1);
                    down = (hs.upCell == c) ? f : 1.0 - f;
                }
                double sigma = cd.side == "audience" ? 1.0 : -1.0;
                double phi = sigma * down * hs.stateAngle;
                // tag layout in the cluster (2x2), in the sticker plane
                Vec3 n0 = cd.upClusterNormal.normalized();
                Vec3 across{1, 0, 0};
                Vec3 along = n0.cross(across).normalized();
                double ox = (k % 2 == 0 ? -1 : 1) * 0.0889, oy = (k < 2 ? -1 : 1) * 0.0508;
                Vec3 pUp = cd.upClusterCenter + across * ox + along * oy;
                auto rot = [&](Vec3 v) {
                    double y = v.y, z = v.z;
                    return Vec3{v.x, y * std::cos(phi) - z * std::sin(phi), y * std::sin(phi) + z * std::cos(phi)};
                };
                Vec3 rel = pUp - hd.pivot;
                Vec3 p = hd.pivot + rot(rel);
                if (normal) *normal = rot(n0);
                return p;
            }
        }
    }
    GP_THROW(std::runtime_error("unknown AprilTag id " + std::to_string(tagId)));
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
    interactRobotsElements(dt);
    stepElements(dt);
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
void World::interactRobotsElements(double dt) {
    for (size_t r = 0; r < robots_.size(); ++r) {
        Robot& rb = *robots_[r].robot;
        OBB2 fb = rb.footprint();
        int intake = -1, fly = -1, gate = -1, hood = -1, turret = -1;
        for (size_t m = 0; m < rb.mechs.size(); ++m) {
            switch (rb.mechs[m].type) {
                case MechType::Intake: intake = int(m); break;
                case MechType::Flywheel: fly = int(m); break;
                case MechType::Gate: gate = int(m); break;
                case MechType::Hood: hood = int(m); break;
                case MechType::Turret: turret = int(m); break;
                default: break;
            }
        }
        (void)hood; (void)turret;
        bool alive = rb.rcAlive && !rb.disabled;
        // Ground elements vs chassis and intake
        for (auto& e : elements_) {
            if (e.state != ElemState::Free) continue;
            if (e.p.z > rb.cfg.height + e.radius) continue;
            Vec2 l = fb.toLocal(e.p.xy());
            if (std::fabs(l.x) > fb.halfL + e.radius + 0.10 || std::fabs(l.y) > fb.halfW + e.radius) continue;
            if (intake >= 0 && alive) {
                const Mechanism& im = rb.mechs[intake];
                bool inZone = l.x > fb.halfL - 0.01 && l.x < fb.halfL + im.reach + e.radius && std::fabs(l.y) < im.width / 2 &&
                              e.p.z < 0.25;
                double sp = rb.surfaceSpeed(intake);
                if (inZone && sp > 0.6 * im.captureSpeed && int(rb.held.size()) < im.capacity) {
                    e.state = ElemState::Held;
                    e.holder = int(r);
                    rb.held.push_back(e.id);
                    if (e.fromTip) {
                        rules_->addViolation(*this, "G409", int(r), "VERBAL_WARNING", "YELLOW_CARD", true, robots_[r].ai && robots_[r].ai->intendsCatch(),
                                             "robot collected an element released by a TIPPED HIVE before it touched anything else", t_);
                        e.fromTip = false;
                    }
                    e.lastRobot = int(r);
                    e.lastRobotTime = t_;
                    intakeLoadUntil_[r] = t_ + 0.04;
                    intakeLoad_[r] = e.mass * sp / 0.04 * im.rollerRadius + 0.12;
                    if (onEvent) { Json d = Json::object(); d.set("element", e.id); d.set("type", e.type == ElemType::Pollen ? "POLLEN" : "NECTAR"); onEvent("intake", int(r), d); }
                    continue;
                }
            }
            // pushed by the chassis
            if (std::fabs(l.x) < fb.halfL + e.radius && std::fabs(l.y) < fb.halfW + e.radius) {
                Vec2 cp = fb.closest(e.p.xy());
                Vec2 nrm = (e.p.xy() - cp);
                double d = nrm.norm();
                if (d < 1e-9) {  // centre inside the box: push out along the shallow axis
                    double px = fb.halfL - std::fabs(l.x), py = fb.halfW - std::fabs(l.y);
                    nrm = px < py ? fb.axisX() * sgn(l.x) : fb.axisY() * sgn(l.y);
                    d = 0;
                } else nrm = nrm / d;
                Vec2 target = cp + nrm * (e.radius + 0.001);
                e.p.x = target.x; e.p.y = target.y;
                Vec2 pv = rb.vel + Vec2{-rb.omega * (cp - rb.pos).y, rb.omega * (cp - rb.pos).x};
                Vec2 rel = e.v.xy() - pv;
                double vn = rel.dot(nrm);
                if (vn < 0) { Vec2 nv = e.v.xy() - nrm * (vn * (1 + season_.restitution)); e.v.x = nv.x; e.v.y = nv.y; }
                if (e.fromTip) {
                    rules_->addViolation(*this, "G409", int(r), "VERBAL_WARNING", "YELLOW_CARD", true, false,
                                         "robot deflected an element released by a TIPPED HIVE before it touched anything else", t_);
                    e.fromTip = false;
                }
                e.lastRobot = int(r);
                e.lastRobotTime = t_;
            }
        }
        // FLOWER bottom retrieval (only POLLEN fits the retrieval opening)
        if (intake >= 0 && alive && rb.surfaceSpeed(intake) > 0.6 * rb.mechs[intake].captureSpeed &&
            int(rb.held.size()) < rb.mechs[intake].capacity && t_ - rb.lastFeedTime > 0.05) {
            const Mechanism& im = rb.mechs[intake];
            for (size_t f = 0; f < season_.flowers.size(); ++f) {
                Vec2 l = fb.toLocal(season_.flowers[f].axis);
                if (l.x > fb.halfL - 0.02 && l.x < fb.halfL + im.reach + season_.flowerOuterR && std::fabs(l.y) < im.width / 2 &&
                    !flowers_[f].stack.empty()) {
                    int bottom = flowers_[f].stack.front();
                    Element& e = elements_[bottom];
                    if (e.type != ElemType::Pollen) continue;
                    flowers_[f].stack.erase(flowers_[f].stack.begin());
                    e.state = ElemState::Held;
                    e.holder = int(r);
                    e.container = -1;
                    rb.held.push_back(e.id);
                    e.lastRobot = int(r);
                    intakeLoadUntil_[r] = t_ + 0.06;
                    intakeLoad_[r] = 0.25;
                    if (onEvent) { Json d = Json::object(); d.set("flower", int(f)); d.set("element", e.id); onEvent("flower_retrieve", int(r), d); }
                    break;
                }
            }
        }
        // Launching
        if (fly >= 0 && alive && !rb.held.empty()) {
            const Mechanism& fm = rb.mechs[fly];
            const GearedJoint& j = rb.joints[fm.joint];
            const MotorUnit& mu = rb.elec.motors[j.motor];
            double maxSurface = mu.spec.wFree() / j.ratio * fm.flyRadius;
            bool open = true;
            if (gate >= 0 && !rb.mechs[gate].servos.empty()) open = rb.servoFraction(rb.mechs[gate].servos[0]) > rb.mechs[gate].openAbove;
            double interval = gate >= 0 ? rb.mechs[gate].feedInterval : 0.3;
            if (open && t_ - rb.lastFeedTime >= interval && rb.surfaceSpeed(fly) >= fm.minLaunchFrac * maxSurface) launchFrom(int(r), fly);
        }
    }
}

void World::launchFrom(int r, int mech) {
    Robot& rb = *robots_[r].robot;
    Mechanism& fm = rb.mechs[mech];
    GearedJoint& j = rb.joints[fm.joint];
    int id = rb.held.back();
    rb.held.pop_back();
    Element& e = elements_[id];
    double surface = j.w * fm.flyRadius;
    double speed = fm.launchEff * surface * (e.type == ElemType::Nectar ? 0.93 : 1.0);
    double elev = 0.9;
    double yawOff = 0;
    for (auto& m : rb.mechs) {
        if (m.type == MechType::Hood && !m.servos.empty()) {
            double f = rb.servoFraction(m.servos[0]);
            // actual hood angle follows the servo's physical angle, not the command
            const ServoUnit& sv = rb.elec.servos[m.servos[0]];
            double span = sv.spec.rangeRad > 0 ? sv.spec.rangeRad : 1.0;
            double fAct = clampd(sv.angle / span + 0.5, 0.0, 1.0);
            (void)f;
            elev = m.angleAt0 + fAct * (m.angleAt1 - m.angleAt0);
        }
        if (m.type == MechType::Turret) {
            if (m.joint >= 0) yawOff = rb.joints[m.joint].theta / m.turretGear;
            else if (!m.servos.empty()) yawOff = rb.elec.servos[m.servos[0]].angle / m.turretGear;
        }
    }
    double yaw = rb.heading + yawOff;
    Vec2 origin = rb.bodyToWorld({fm.launchFwd, 0});
    e.state = ElemState::Free;
    e.holder = -1;
    e.launched = true;
    e.lastRobot = r;
    e.lastRobotTime = t_;
    e.p = {origin.x, origin.y, fm.launchHeight};
    e.v = Vec3{rb.vel.x, rb.vel.y, 0} + Vec3{std::cos(yaw) * std::cos(elev), std::sin(yaw) * std::cos(elev), std::sin(elev)} * speed;
    // energy extracted from the flywheel (ball KE / transfer efficiency)
    double J = j.jl + j.jmOut();
    double eLoss = 0.5 * e.mass * speed * speed / fm.contactEnergyLoss;
    double before = j.w;
    double w2 = j.w * j.w - 2 * eLoss / J;
    j.w = sgn(j.w) * std::sqrt(std::max(0.0, w2));
    j.wm = j.w;
    rb.lastFeedTime = t_;
    launches.push_back({t_, r, id, speed, elev, yaw, before, j.w});
    if (onEvent) {
        Json d = Json::object();
        d.set("element", id); d.set("speed", speed); d.set("elevation", elev); d.set("flywheelBefore", before); d.set("flywheelAfter", j.w);
        onEvent("launch", r, d);
    }
}

// ---------------------------------------------------------------------------------------
void World::ejectElement(Element& e) {
    e.state = ElemState::OutOfField;
    e.v = {0, 0, 0};
    rules_->onElementLeftField(*this, e.id, t_);
    if (e.type == ElemType::Pollen) {
        e.reentryAt = t_ + 5.0 + rngElements_.uniform(0, 5);  // FIELD STAFF reintroduce at the earliest safe opportunity
    } else {
        e.state = ElemState::HumanArea;                      // NECTAR returns to its ALLIANCE's DRIVE TEAM
        humans_[int(e.color)].waitingIds.push_back(e.id);
        humans_[int(e.color)].nectarInArea++;
        const AABB2& area = season_.allianceArea[int(e.color)];
        e.p = {(area.xMin + area.xMax) / 2, (area.yMin + area.yMax) / 2, e.radius};
    }
}

void World::stepElements(double dt) {
    const double half = season_.half;
    for (auto& e : elements_) {
        switch (e.state) {
            case ElemState::Held: {
                Robot& rb = *robots_[e.holder].robot;
                e.p = {rb.pos.x, rb.pos.y, 0.18};
                e.v = {rb.vel.x, rb.vel.y, 0};
                continue;
            }
            case ElemState::InFlower: {
                const FlowerState& fs = flowers_[e.container];
                size_t k = std::find(fs.stack.begin(), fs.stack.end(), e.id) - fs.stack.begin();
                double z = 0;
                for (size_t i = 0; i <= k; ++i) {
                    const Element& o = elements_[fs.stack[i]];
                    z += (i == 0 ? o.radius : elements_[fs.stack[i - 1]].radius + o.radius) * 1.03;
                }
                Vec2 ax = season_.flowers[e.container].axis;
                double zig = (k % 2 ? 1 : -1) * 0.006;
                e.p = {ax.x + zig, ax.y, z};
                e.v = {0, 0, 0};
                continue;
            }
            case ElemState::InCell:
            case ElemState::HumanArea:
                continue;
            case ElemState::OutOfField:
                if (e.reentryAt >= 0 && t_ >= e.reentryAt) {
                    // nearest convenient location: just inside the closest wall
                    Vec2 p = e.p.xy();
                    p.x = clampd(p.x, -half + 0.1, half - 0.1);
                    p.y = clampd(p.y, -half + 0.1, half - 0.1);
                    e.p = {p.x, p.y, e.radius};
                    e.state = ElemState::Free;
                    e.reentryAt = -1;
                }
                continue;
            case ElemState::Free: break;
        }
        Vec3 prev = e.p;
        double area = kPi * e.radius * e.radius;
        double sp = e.v.norm();
        Vec3 drag = e.v * (-0.5 * kAirRho * kBallCd * area * sp / e.mass);
        e.v += (drag + Vec3{0, 0, -kG}) * dt;
        e.p += e.v * dt;
        // floor
        if (e.p.z < e.radius) {
            e.p.z = e.radius;
            if (e.v.z < 0) {
                e.v.z = -e.v.z * season_.restitution;
                if (e.v.z < 0.25) e.v.z = 0;
                e.v.x *= 0.92;
                e.v.y *= 0.92;
            }
            e.fromTip = false;
        }
        if (e.p.z <= e.radius + 1e-4) {  // rolling resistance
            Vec2 vh = e.v.xy();
            double s = vh.norm();
            double dv = season_.rollingRes * kG * dt * 3.0;
            if (s <= dv) { e.v.x = e.v.y = 0; }
            else { Vec2 nv = vh * ((s - dv) / s); e.v.x = nv.x; e.v.y = nv.y; }
        }
        // perimeter
        for (int axis = 0; axis < 2; ++axis) {
            double& c = axis == 0 ? e.p.x : e.p.y;
            double& v = axis == 0 ? e.v.x : e.v.y;
            if (std::fabs(c) > half - e.radius) {
                if (e.p.z < season_.wallHeight + e.radius * 0.5) {
                    c = sgn(c) * (half - e.radius);
                    if (v * sgn(c) > 0) v = -v * 0.5;
                    e.fromTip = false;
                } else if (std::fabs(c) > half + 0.05) {
                    ejectElement(e);
                    break;
                }
            }
        }
        if (e.state != ElemState::Free) continue;
        // HIVE frame legs
        for (auto& leg : season_.legs) {
            Vec3 cp;
            double d = segmentPointDistance(leg.base, leg.apex, e.p, &cp);
            if (d < e.radius + season_.legRadius) {
                Vec3 nrm = (e.p - cp).normalized();
                e.p = cp + nrm * (e.radius + season_.legRadius + 1e-4);
                double vn = e.v.dot(nrm);
                if (vn < 0) e.v -= nrm * (vn * (1 + season_.restitution));
                e.fromTip = false;
            }
        }
        // FLOWERS: entry through the top opening, otherwise bounce off the structure
        for (size_t f = 0; f < season_.flowers.size(); ++f) {
            Vec2 ax = season_.flowers[f].axis;
            Vec2 dxy = e.p.xy() - ax;
            double hd = dxy.norm();
            if (hd > season_.flowerOuterR + e.radius || e.p.z > season_.flowerTopH + 0.05) continue;
            bool crossingTop = prev.z >= season_.flowerTopH && e.p.z < season_.flowerTopH && e.v.z < 0;
            if (crossingTop && hd < season_.flowerOpeningD / 2) {
                FlowerState& fs = flowers_[f];
                double top = 0;
                for (size_t i = 0; i < fs.stack.size(); ++i) top += elements_[fs.stack[i]].radius * 2 * 1.03;
                if (top + 2 * e.radius < season_.flowerTopH + e.radius) {
                    e.state = ElemState::InFlower;
                    e.container = int(f);
                    fs.stack.push_back(e.id);
                    if (e.type == ElemType::Nectar) rules_->onNectarEnteredFlower(*this, e.id, int(f), t_);
                    if (onEvent) { Json d = Json::object(); d.set("element", e.id); d.set("flower", int(f)); onEvent("flower_entry", e.lastRobot, d); }
                    break;
                }
            }
            if (e.p.z < season_.flowerTopH && hd < season_.flowerOuterR + e.radius) {
                Vec2 nrm = hd > 1e-9 ? dxy / hd : Vec2{1, 0};
                Vec2 np = ax + nrm * (season_.flowerOuterR + e.radius + 1e-4);
                e.p.x = np.x; e.p.y = np.y;
                double vn = e.v.xy().dot(nrm);
                if (vn < 0) { e.v.x -= nrm.x * vn * (1 + season_.restitution); e.v.y -= nrm.y * vn * (1 + season_.restitution); }
                e.fromTip = false;
            }
        }
        if (e.state != ElemState::Free) continue;
        // CELLS: capture through the up-facing opening
        for (int h = 0; h < 2; ++h) {
            HiveState& hs = hives_[h];
            if (hs.tipping) continue;
            Vec3 o = cellOpeningCenter(h, hs.upCell);
            bool inside = std::fabs(e.p.x - o.x) < season_.cellHalfX && std::fabs(e.p.y - o.y) < season_.cellHalfY;
            if (inside && prev.z >= o.z && e.p.z < o.z && e.v.z < 0) {
                e.state = ElemState::InCell;
                e.container = h * 2 + hs.upCell;
                hs.contents[hs.upCell].push_back(e.id);
                double w = e.type == ElemType::Pollen ? season_.pollenTipW : season_.nectarTipW;
                hs.weight += w + season_.impactBonusPerMps * std::fabs(e.v.z);  // 'tossed in' elements tip more readily (EFSG 12.3)
                e.p = {o.x + rngElements_.uniform(-0.15, 0.15), o.y, o.z - 0.12};
                e.v = {0, 0, 0};
                if (onEvent) { Json d = Json::object(); d.set("element", e.id); d.set("hive", toString(Alliance(h))); d.set("weight", hs.weight); onEvent("cell_entry", e.lastRobot, d); }
                break;
            }
            // solid underside of the up cell and the down cell bodies
            bool underUp = inside && e.p.z < o.z - 0.02 && e.p.z > o.z - 0.30;
            if (underUp && e.v.z > 0 && prev.z <= o.z - 0.30) { e.v.z = -e.v.z * season_.restitution; e.fromTip = false; }
        }
    }
    // element-element collisions (free elements only)
    for (size_t i = 0; i < elements_.size(); ++i) {
        Element& a = elements_[i];
        if (a.state != ElemState::Free) continue;
        for (size_t k = i + 1; k < elements_.size(); ++k) {
            Element& b = elements_[k];
            if (b.state != ElemState::Free) continue;
            Vec3 d = b.p - a.p;
            double rr = a.radius + b.radius;
            double d2 = d.norm2();
            if (d2 >= rr * rr || d2 < 1e-12) continue;
            double dist = std::sqrt(d2);
            Vec3 nrm = d / dist;
            double pen = rr - dist;
            double ma = a.mass, mb = b.mass;
            a.p -= nrm * (pen * mb / (ma + mb));
            b.p += nrm * (pen * ma / (ma + mb));
            double vn = (b.v - a.v).dot(nrm);
            if (vn < 0) {
                double jimp = -(1 + 0.6) * vn / (1 / ma + 1 / mb);
                a.v -= nrm * (jimp / ma);
                b.v += nrm * (jimp / mb);
            }
            a.fromTip = false;
            b.fromTip = false;
        }
    }
}

void World::releaseFromCell(int h, int c) {
    HiveState& hs = hives_[h];
    const HiveDef& hd = season_.hives[h];
    double side = hd.cells[c].side == "audience" ? -1.0 : 1.0;
    for (int id : hs.contents[c]) {
        Element& e = elements_[id];
        e.state = ElemState::Free;
        e.container = -1;
        e.p = {hd.pivot.x + rngElements_.uniform(-0.18, 0.18), side * rngElements_.uniform(0.25, 0.40), 0.95};
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
                         double(e.lastRobot), e.fromTip ? 1.0 : 0.0, e.reentryAt, e.mass})
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
