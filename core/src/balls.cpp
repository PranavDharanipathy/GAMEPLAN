// SCORING ELEMENT physics: hollow perforated balls as rigid bodies with spin and compliant contacts.
//
// * Every contact (floor, walls, HIVE legs, FLOWERS, robot chassis, intake roller, other balls) is a
//   Hertz spring F = k*d^1.5 with damping set from the coefficient of restitution, plus Coulomb
//   friction at the contact point. Friction couples sliding and spin: a ball hitting the floor at an
//   angle starts rolling, a roller spins the ball it drags, colliding balls exchange spin.
// * The compression d is the visible squish (Element::squish) - a few millimetres on impacts.
// * Flight: gravity, drag (Cd) and Magnus lift from spin (perforated-ball coefficients, estimates).
// * Collected balls never disappear: the intake roller drags a ball in by friction, then it rides
//   the robot's internal ball path (intake mouth -> storage -> shooter) as a 1-D body that is
//   driven by belts/rollers, feels gravity along the path and the robot's acceleration (balls
//   slosh when the robot brakes), and collides with its neighbours. A full path blocks the mouth.
//   Reversing the intake spits balls out. A ball reaching the end of the path is launched when
//   the gate is open; its exit speed and backspin come from the flywheel contact and depend on
//   that ball's size (compression), mass and the wheel's speed at that instant.
// Constants live in the season package (scoringElements.*.physics) with provenance.
#include <algorithm>
#include <cmath>

#include "gameplan/ai.hpp"
#include "gameplan/rules.hpp"
#include "gameplan/world.hpp"

namespace gp {

namespace {
constexpr double kAirRho = 1.2;

double dampingRatio(double e) {
    e = clampd(e, 0.05, 0.99);
    double l = std::log(e);
    return -l / std::sqrt(kPi * kPi + l * l);
}

void quatIntegrate(double q[4], const Vec3& w, double h) {
    double dq0 = 0.5 * (-w.x * q[1] - w.y * q[2] - w.z * q[3]);
    double dq1 = 0.5 * (w.x * q[0] + w.y * q[3] - w.z * q[2]);
    double dq2 = 0.5 * (-w.x * q[3] + w.y * q[0] + w.z * q[1]);
    double dq3 = 0.5 * (w.x * q[2] - w.y * q[1] + w.z * q[0]);
    q[0] += dq0 * h; q[1] += dq1 * h; q[2] += dq2 * h; q[3] += dq3 * h;
    double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; ++i) q[i] /= n;
}

Vec3 rotZ(const Vec3& v, double a) { return {v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a), v.z}; }

struct Acc { Vec3 f, tq; double squish = 0; Vec3 sqN{0, 0, 1}; };

}  // namespace

// Height and vertical speed of a ball launched in a vertical plane (speed v, elevation th, backspin
// sense) when it has travelled dx horizontally - same drag/Magnus model as the free-ball step.
double predictBallHeight(const BallPhys& b, double m, double r, double v, double th, double spinSense, double dx, double& vzAt) {
    double area = kPi * r * r;
    double x = 0, z = 0, vx = v * std::cos(th), vz = v * std::sin(th);
    double wspin = spinSense * v / r;          // >0: backspin (lift)
    const double h = 0.004;
    for (int i = 0; i < 1500 && x < dx; ++i) {
        double sp = std::sqrt(vx * vx + vz * vz);
        double S = r * std::fabs(wspin) / std::max(sp, 0.2);
        double cl = std::min(b.clMax, b.clPerS * S) * sgn(wspin);
        double kd = 0.5 * kAirRho * area * sp / m;
        double ax = -kd * b.cd * vx - kd * cl * vz;      // lift is perpendicular to the velocity
        double az = -kG - kd * b.cd * vz + kd * cl * vx;
        vx += ax * h; vz += az * h;
        double xn = x + vx * h, zn = z + vz * h;
        if (xn >= dx) { double f = (dx - x) / std::max(xn - x, 1e-9); vzAt = vz; return z + f * (zn - z); }
        x = xn; z = zn;
        wspin *= std::exp(-b.spinDecay * h);
        if (vx <= 0.05) break;
    }
    vzAt = vz;
    return -1e9;
}

const BallPhys& World::ballPhys(const Element& e) const { return e.type == ElemType::Pollen ? season_.pollenPhys : season_.nectarPhys; }

double World::ballInertia(const Element& e) const { return ballPhys(e).inertiaK * e.mass * e.radius * e.radius; }

// ------------------------------------------------------------------ internal ball path
std::vector<Vec3> World::ballPath(int r) const {
    const Robot& rb = *robots_[r].robot;
    const Mechanism* in = nullptr;
    const Mechanism* fly = nullptr;
    for (auto& m : rb.mechs) {
        if (m.type == MechType::Intake) in = &m;
        if (m.type == MechType::Flywheel) fly = &m;
    }
    if (in && in->path.size() >= 2) return in->path;
    double hl = rb.cfg.length / 2;
    double rBig = season_.nectarD / 2;
    Vec3 mouth{hl - 0.015, 0, rBig + 0.004};
    Vec3 shooter{fly ? fly->launchFwd : 0.0, 0, fly ? std::max(fly->launchHeight - 0.02, mouth.z + 0.05) : 0.2};
    Vec3 mid{0.1 * hl + 0.4 * (shooter.x - 0.1 * hl), 0, mouth.z + 0.4 * (shooter.z - mouth.z)};
    return {mouth, mid, shooter};
}

double World::ballPathLength(int r) const {
    auto p = ballPath(r);
    double L = 0;
    for (size_t i = 1; i < p.size(); ++i) L += (p[i] - p[i - 1]).norm();
    return L;
}

Vec3 World::ballPathPoint(int r, double s, Vec3* tangent) const {
    auto p = ballPath(r);
    double acc = 0;
    for (size_t i = 1; i < p.size(); ++i) {
        Vec3 d = p[i] - p[i - 1];
        double l = d.norm();
        if (s <= acc + l || i + 1 == p.size()) {
            if (tangent) *tangent = d * (1.0 / std::max(l, 1e-9));
            return p[i - 1] + d * ((s - acc) / std::max(l, 1e-9));
        }
        acc += l;
    }
    if (tangent) *tangent = {1, 0, 0};
    return p.front();
}

// body-frame point -> world (planar pose; chassis pitch/roll are small)
static Vec3 bodyToWorld3(const Robot& rb, const Vec3& b) {
    Vec2 xy = rb.bodyToWorld({b.x, b.y});
    return {xy.x, xy.y, b.z};
}

// ------------------------------------------------------------------ contacts
// Soft contact of ball e against a surface with outward normal n (towards the ball centre),
// penetration pen and surface velocity vSurf at the contact point. mEff: reduced mass.
static void softContact(const World& w, const Element& e, Acc& A, Vec3 n, double pen, Vec3 vSurf, double mu, double eRest,
                        double k, double mEff, double h) {
    if (pen <= 0) return;
    pen = std::min(pen, 0.35 * e.radius);   // deep overlaps (spawn/teleport) resolve gently, never explosively
    Vec3 rc = n * (-e.radius);
    Vec3 vc = e.v + e.w.cross(rc) - vSurf;
    double vn = vc.dot(n);
    double kLin = 1.5 * k * std::sqrt(pen);
    double c = 2.0 * dampingRatio(eRest) * std::sqrt(kLin * mEff);
    double fn = std::max(0.0, k * pen * std::sqrt(pen) - c * vn);
    Vec3 vt = vc - n * vn;
    double vtm = vt.norm();
    Vec3 ft{0, 0, 0};
    if (vtm > 1e-9) {
        double I = w.ballInertia(e);
        double mt = 1.0 / (1.0 / e.mass + e.radius * e.radius / I);
        double stick = vtm * mt / h * 0.5;          // force that would cancel the slip in ~2 substeps
        ft = vt * (-std::min(mu * fn, stick) / vtm);
    }
    A.f += n * fn + ft;
    A.tq += rc.cross(ft);
    if (pen > A.squish) { A.squish = pen; A.sqN = n; }
}

// ------------------------------------------------------------------ launch
void World::launchElement(int r, int mech, int id) {
    Robot& rb = *robots_[r].robot;
    Mechanism& fm = rb.mechs[mech];
    GearedJoint& j = rb.joints[fm.joint];
    Element& e = elements_[id];
    rb.held.erase(std::remove(rb.held.begin(), rb.held.end(), id), rb.held.end());
    const double nominalR = (e.type == ElemType::Pollen ? season_.pollenD : season_.nectarD) / 2;
    // contact between wheel and hood: squeeze grows with this ball's size; grip saturates with squeeze
    double comp = std::max(0.0005, fm.compression + 2.0 * (e.radius - nominalR) + (e.type == ElemType::Nectar ? 0.004 : 0.0));
    // grip saturates with squeeze: a typical compression (~6 mm) is near saturation, so normal size
    // variation (sigma 0.4 mm) changes exit speed by ~2 % (estimate; measure with real elements)
    auto grip = [](double c) { return 1.0 - std::exp(-c / 0.002); };
    double gripF = grip(comp) / grip(std::max(0.0005, fm.compression));
    double J = j.jl + j.jmOut();
    double mr2 = e.mass * fm.flyRadius * fm.flyRadius;
    double nomMass = e.type == ElemType::Pollen ? season_.pollenMass : season_.nectarMass;
    double massF = (1.0 + nomMass * fm.flyRadius * fm.flyRadius / J) / (1.0 + mr2 / J);   // momentum exchange with the wheel
    double surface = j.w * fm.flyRadius;
    double speed = fm.launchEff * surface * gripF * massF * (e.type == ElemType::Nectar ? 0.93 : 1.0);
    double elev = 0.9, yawOff = 0;
    for (auto& m : rb.mechs) {
        if (m.type == MechType::Hood && !m.servos.empty()) {
            const ServoUnit& sv = rb.elec.servos[m.servos[0]];       // actual hood angle follows the servo's physical angle
            double span = sv.spec.rangeRad > 0 ? sv.spec.rangeRad : 1.0;
            double fAct = clampd(sv.angle / span + 0.5, 0.0, 1.0);
            elev = m.angleAt0 + fAct * (m.angleAt1 - m.angleAt0);
        }
        if (m.type == MechType::Turret) {
            if (m.joint >= 0) yawOff = rb.joints[m.joint].theta / m.turretGear;
            else if (!m.servos.empty()) yawOff = rb.elec.servos[m.servos[0]].angle / m.turretGear;
        }
    }
    double yaw = rb.heading + yawOff;
    Vec3 dir{std::cos(yaw) * std::cos(elev), std::sin(yaw) * std::cos(elev), std::sin(elev)};
    Vec2 origin = rb.bodyToWorld({fm.launchFwd, 0});
    e.state = ElemState::Free;
    e.holder = -1;
    e.launched = true;
    e.lastRobot = r;
    e.lastRobotTime = t_;
    e.launchFrom = origin;
    e.launchYaw = yaw;
    e.passRobot = r;
    e.passUntil = t_ + 0.25;
    e.p = {origin.x, origin.y, fm.launchHeight};
    e.v = Vec3{rb.vel.x, rb.vel.y, 0} + dir * speed;
    // the ball rolls on the hood while the wheel drives it: spin ~ v / r about the horizontal axis
    Vec3 axis = dir.cross(Vec3{0, 0, 1});
    if (axis.norm() > 1e-6) axis = axis * (1.0 / axis.norm());
    e.w = axis * (fm.spinSense * speed / e.radius * std::min(1.0, gripF));
    e.pathS = e.pathV = 0;
    // energy taken from the flywheel: translational + rotational ball energy / transfer efficiency
    double ke = 0.5 * e.mass * speed * speed + 0.5 * ballInertia(e) * e.w.norm2();
    double before = j.w;
    double w2 = j.w * j.w - 2 * (ke / fm.contactEnergyLoss) / J;
    j.w = sgn(j.w) * std::sqrt(std::max(0.0, w2));
    j.wm = j.w;
    rb.lastFeedTime = t_;
    launches.push_back({t_, r, id, speed, elev, yaw, before, j.w});
    if (onEvent) {
        Json d = Json::object();
        d.set("element", id); d.set("speed", speed); d.set("elevation", elev); d.set("spin_radps", e.w.norm());
        d.set("compression_m", comp); d.set("flywheelBefore", before); d.set("flywheelAfter", j.w);
        onEvent("launch", r, d);
    }
}

void World::ejectElement(Element& e) {
    e.state = ElemState::OutOfField;
    e.v = {0, 0, 0};
    e.w = {0, 0, 0};
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

// Position of the k-th element resting in a cell (packed from the bottom), moving with the HIVE.
Vec3 World::cellSlot(int h, int c, int k) const {
    const HiveDef& hd = season_.hives[h];
    const HiveState& hs = hives_[h];
    Vec3 o = cellOpeningCenter(h, c);
    double pitch = season_.nectarD * 1.02;
    int perRow = std::max(1, int(2 * season_.cellHalfX / pitch));
    int perLayer = perRow * std::max(1, int(2 * season_.cellHalfY / pitch));
    int layer = k / perLayer, inL = k % perLayer;
    Vec3 p{o.x - season_.cellHalfX + pitch * (0.5 + inL % perRow), o.y - season_.cellHalfY + pitch * (0.5 + inL / perRow),
           o.z - 0.26 + pitch * (0.5 + layer)};
    // rotate with the tip state (same convention as the AprilTag clusters)
    double down = (hs.upCell == c) ? 0.0 : 1.0;
    if (hs.tipping) {
        double f = clampd((t_ - hs.tipStart) / season_.tipSettleS, 0, 1);
        down = (hs.upCell == c) ? f : 1.0 - f;
    }
    double phi = (hd.cells[c].side == "audience" ? 1.0 : -1.0) * down * hs.stateAngle;
    Vec3 rel = p - hd.pivot;
    return hd.pivot + Vec3{rel.x, rel.y * std::cos(phi) - rel.z * std::sin(phi), rel.y * std::sin(phi) + rel.z * std::cos(phi)};
}

// ------------------------------------------------------------------ main step
void World::stepBalls(double dt) {
    const double half = season_.half;
    const int nsub = dt > 0.0003 ? int(std::ceil(dt / 0.00025)) : 1;
    const double h = dt / nsub;
    if (prevRobotVel_.size() != robots_.size()) {
        prevRobotVel_.clear();
        for (auto& rs : robots_) prevRobotVel_.push_back(rs.robot->vel);
    }
    // per-robot mechanism lookup (constant during the step)
    struct RM { int intake = -1, fly = -1, gate = -1; double L = 0; Vec2 acc; bool alive = false; double roller = 0; };
    std::vector<RM> rm(robots_.size());
    for (size_t r = 0; r < robots_.size(); ++r) {
        Robot& rb = *robots_[r].robot;
        for (size_t m = 0; m < rb.mechs.size(); ++m) {
            if (rb.mechs[m].type == MechType::Intake) rm[r].intake = int(m);
            if (rb.mechs[m].type == MechType::Flywheel) rm[r].fly = int(m);
            if (rb.mechs[m].type == MechType::Gate) rm[r].gate = int(m);
        }
        rm[r].L = ballPathLength(int(r));
        rm[r].acc = (rb.vel - prevRobotVel_[r]) / dt;
        rm[r].alive = rb.rcAlive && !rb.disabled;
        rm[r].roller = rm[r].intake >= 0 && rm[r].alive ? rb.surfaceSpeed(rm[r].intake) : 0.0;
        prevRobotVel_[r] = rb.vel;
    }
    for (auto& e : elements_) e.squish = 0;
    for (auto& l : intakeLoad_) l = 0;
    std::vector<Acc> acc(elements_.size());

    for (int sub = 0; sub < nsub; ++sub) {
        const double ts = t_ + sub * h;
        for (auto& a : acc) a = Acc();
        // ---------------- held balls: 1-D dynamics along each robot's ball path
        for (size_t r = 0; r < robots_.size(); ++r) {
            Robot& rb = *robots_[r].robot;
            if (rb.held.empty()) continue;
            std::sort(rb.held.begin(), rb.held.end(), [&](int a, int b) { return elements_[a].pathS < elements_[b].pathS; });
            const RM& R = rm[r];
            const Mechanism* im = R.intake >= 0 ? &rb.mechs[R.intake] : nullptr;
            bool gateOpen = true;
            if (R.gate >= 0 && !rb.mechs[R.gate].servos.empty()) gateOpen = rb.servoFraction(rb.mechs[R.gate].servos[0]) > rb.mechs[R.gate].openAbove;
            bool flyReady = false;
            if (R.fly >= 0) {
                const Mechanism& fm = rb.mechs[R.fly];
                const GearedJoint& j = rb.joints[fm.joint];
                double maxSurface = rb.elec.motors[j.motor].spec.wFree() / j.ratio * fm.flyRadius;
                double interval = R.gate >= 0 ? rb.mechs[R.gate].feedInterval : 0.3;
                flyReady = R.alive && rb.surfaceSpeed(R.fly) >= fm.minLaunchFrac * maxSurface && ts - rb.lastFeedTime >= interval;
            }
            size_t n = rb.held.size();
            std::vector<double> a(n, 0.0);
            double beltLoad = 0;
            for (size_t i = 0; i < n; ++i) {
                Element& e = elements_[rb.held[i]];
                Vec3 tb;
                ballPathPoint(int(r), e.pathS, &tb);
                Vec3 tw = rotZ(tb, rb.heading);
                // Belts / compliant rollers squeeze the ball: they act as a velocity source whose force
                // is friction-limited (~3 g). Stopped rollers therefore hold balls against gravity, but a
                // hard stop or hit of the robot (> 3 g along the path) makes the balls slosh.
                double grip = im ? im->beltGripS : 0.03;
                double ext = -kG * tb.z - (R.acc.x * tw.x + R.acc.y * tw.y);
                double belt = clampd((R.roller - e.pathV) / grip - ext, -3.0 * kG, 3.0 * kG);
                a[i] += belt + ext;
                beltLoad += std::fabs(belt) * e.mass;
            }
            for (size_t i = 0; i + 1 < n; ++i) {   // neighbour contacts along the path
                Element& e0 = elements_[rb.held[i]];
                Element& e1 = elements_[rb.held[i + 1]];
                double pen = (e0.radius + e1.radius) - (e1.pathS - e0.pathS);
                if (pen <= 0) continue;
                double k = ballPhys(e0).hertzK * 0.354;
                double mEff = e0.mass * e1.mass / (e0.mass + e1.mass);
                double c = 2.0 * dampingRatio(ballPhys(e0).eBall) * std::sqrt(1.5 * k * std::sqrt(pen) * mEff);
                double f = std::max(0.0, k * pen * std::sqrt(pen) - c * (e1.pathV - e0.pathV));
                a[i] -= f / e0.mass;
                a[i + 1] += f / e1.mass;
                e0.squish = std::max(e0.squish, pen / 2); e1.squish = std::max(e1.squish, pen / 2);
            }
            {   // end stop at the shooter while the gate is closed / the wheel is not ready
                Element& last = elements_[rb.held[n - 1]];
                double pen = last.pathS + last.radius - rm[r].L;   // ball front against the gate / wheel
                if (pen > 0 && !(gateOpen && flyReady && R.fly >= 0)) {
                    double k = ballPhys(last).hertzK;
                    double c = 2.0 * dampingRatio(0.3) * std::sqrt(1.5 * k * std::sqrt(pen) * last.mass);
                    a[n - 1] -= std::max(0.0, k * pen * std::sqrt(pen) + c * last.pathV) / last.mass;
                    last.squish = std::max(last.squish, pen);
                }
            }
            for (size_t i = 0; i < n; ++i) {
                Element& e = elements_[rb.held[i]];
                e.pathV += a[i] * h;
                e.pathS += e.pathV * h;
            }
            if (im && im->joint >= 0 && beltLoad > 0) {
                intakeLoad_[r] += beltLoad * im->rollerRadius / nsub;
                intakeLoadUntil_[r] = t_ + dt;
            }
            // exits: launch at the shooter end, spit out at the mouth when reversed
            Element& last = elements_[rb.held.back()];
            if (last.pathS + last.radius >= rm[r].L - 0.004 && gateOpen && flyReady && R.fly >= 0) launchElement(int(r), R.fly, last.id);
            if (!rb.held.empty()) {
                Element& first = elements_[rb.held.front()];
                if (first.pathS < -first.radius) {
                    Vec3 tb;
                    Vec3 pb = ballPathPoint(int(r), 0.0, &tb);
                    Vec3 pw = bodyToWorld3(rb, pb + Vec3{first.radius + 0.01, 0, 0});
                    Vec3 tw = rotZ(tb, rb.heading);
                    first.state = ElemState::Free;
                    first.holder = -1;
                    first.lastRobot = int(r);
                    first.lastRobotTime = ts;
                    first.launched = false;
                    first.passRobot = int(r);
                    first.passUntil = ts + 0.15;
                    first.p = pw;
                    first.v = Vec3{rb.vel.x, rb.vel.y, 0} + tw * first.pathV;
                    first.pathS = first.pathV = 0;
                    rb.held.erase(rb.held.begin());
                }
            }
            // world pose of every held ball (rolling along the channel floor)
            for (int id : rb.held) {
                Element& e = elements_[id];
                Vec3 tb;
                Vec3 pb = ballPathPoint(int(r), clampd(e.pathS, 0.0, rm[r].L), &tb);   // centre on the path
                Vec3 tw = rotZ(tb, rb.heading);
                Vec2 rr = rb.bodyToWorld({pb.x, pb.y}) - rb.pos;
                e.p = bodyToWorld3(rb, pb);
                e.v = Vec3{rb.vel.x - rb.omega * rr.y, rb.vel.y + rb.omega * rr.x, 0} + tw * e.pathV;
                e.w = Vec3{0, 0, 1}.cross(tw) * (e.pathV / e.radius) + Vec3{0, 0, rb.omega};
                quatIntegrate(e.q, e.w, h);
                if (e.squish > 0) e.squishN = tw;
            }
        }

        // ---------------- free balls: forces and contacts
        for (size_t i = 0; i < elements_.size(); ++i) {
            Element& e = elements_[i];
            if (e.state != ElemState::Free) continue;
            const BallPhys& b = ballPhys(e);
            Acc& A = acc[i];
            double area = kPi * e.radius * e.radius;
            double sp = e.v.norm();
            A.f += Vec3{0, 0, -kG * e.mass};
            A.f += e.v * (-0.5 * kAirRho * b.cd * area * sp);
            double wm = e.w.norm();
            if (sp > 0.2 && wm > 1e-3) {   // Magnus lift, perpendicular to spin axis and velocity
                double S = e.radius * wm / sp;
                double cl = std::min(b.clMax, b.clPerS * S);
                Vec3 dirL = e.w.cross(e.v);
                double dn = dirL.norm();
                if (dn > 1e-9) A.f += dirL * (0.5 * kAirRho * area * cl * sp * sp / dn);
            }
            A.tq += e.w * (-b.spinDecay * ballInertia(e));   // air torque spins the ball down
            // floor
            softContact(*this, e, A, {0, 0, 1}, e.radius - e.p.z, {0, 0, 0}, b.muFloor, b.eFloor, b.hertzK, e.mass, h);
            if (e.p.z < e.radius + 0.001) {   // rolling resistance
                Vec3 wp{e.w.x, e.w.y, 0};
                double n = wp.norm();
                if (n > 1e-6) A.tq += wp * (-b.rollRes * e.mass * kG * e.radius / n * std::min(1.0, n / 2.0));
                if (std::fabs(e.w.z) > 1e-6) A.tq.z -= sgn(e.w.z) * 0.3 * b.rollRes * e.mass * kG * e.radius;
            }
            // perimeter walls
            for (int axis = 0; axis < 2; ++axis) {
                double c = axis == 0 ? e.p.x : e.p.y;
                double over = std::fabs(c) - (half - e.radius);
                if (over > 0 && e.p.z < season_.wallHeight + e.radius * 0.5) {
                    Vec3 n = axis == 0 ? Vec3{-sgn(c), 0, 0} : Vec3{0, -sgn(c), 0};
                    softContact(*this, e, A, n, over, {0, 0, 0}, b.muWall, b.eFloor * 0.9, b.hertzK, e.mass, h);
                }
            }
            // HIVE frame legs (capsules)
            for (auto& leg : season_.legs) {
                Vec3 cp;
                double d = segmentPointDistance(leg.base, leg.apex, e.p, &cp);
                double pen = e.radius + season_.legRadius - d;
                if (pen > 0 && d > 1e-9) softContact(*this, e, A, (e.p - cp) * (1.0 / d), pen, {0, 0, 0}, b.muWall, b.eFloor, b.hertzK, e.mass, h);
            }
            // FLOWER outer tubes (entry through the top opening is handled after integration)
            for (size_t f = 0; f < season_.flowers.size(); ++f) {
                Vec2 dxy = e.p.xy() - season_.flowers[f].axis;
                double hd = dxy.norm();
                if (e.p.z < season_.flowerTopH && hd < season_.flowerOuterR + e.radius && hd > 1e-9) {
                    Vec2 n2 = dxy / hd;
                    softContact(*this, e, A, {n2.x, n2.y, 0}, season_.flowerOuterR + e.radius - hd, {0, 0, 0}, b.muWall, b.eFloor, b.hertzK, e.mass, h);
                }
            }
            // robots: chassis box (open intake mouth) + intake roller
            for (size_t r = 0; r < robots_.size(); ++r) {
                Robot& rb = *robots_[r].robot;
                double hl = rb.cfg.length / 2, hw = rb.cfg.width / 2;
                Vec2 l = rb.footprint().toLocal(e.p.xy());
                if (std::fabs(l.x) > hl + 0.2 || std::fabs(l.y) > hw + 0.1 || e.p.z > rb.cfg.height + e.radius) continue;
                const Mechanism* im = rm[r].intake >= 0 ? &rb.mechs[rm[r].intake] : nullptr;
                // a ball that just left this robot through its shooter or mouth passes its own frame
                if (e.passRobot == int(r) && ts < e.passUntil) continue;
                bool inMouthBand = im && std::fabs(l.y) < im->width / 2 - e.radius * 0.3 && e.p.z < im->mouthHeight;
                double front = inMouthBand ? hl - 2.2 * e.radius : hl;   // the mouth is open
                // closest point of the (local) box
                double cx = clampd(l.x, -hl, front), cy = clampd(l.y, -hw, hw), cz = clampd(e.p.z, 0.0, rb.cfg.height);
                Vec2 cw = rb.bodyToWorld({cx, cy});
                Vec3 d = e.p - Vec3{cw.x, cw.y, cz};
                double dist = d.norm();
                if (dist < e.radius) {
                    Vec3 n;
                    double pen;
                    if (dist > 1e-9) { n = d * (1.0 / dist); pen = e.radius - dist; }
                    else {   // centre inside: push out of the nearest face
                        double px = std::min(front - l.x, l.x + hl), py = hw - std::fabs(l.y), pz = rb.cfg.height - e.p.z;
                        Vec3 ax = rotZ({1, 0, 0}, rb.heading), ay = rotZ({0, 1, 0}, rb.heading);
                        if (pz < px && pz < py) { n = {0, 0, 1}; pen = pz + e.radius; }
                        else if (px < py) { n = ax * (front - l.x < l.x + hl ? 1.0 : -1.0); pen = px + e.radius; }
                        else { n = ay * sgn(l.y); pen = py + e.radius; }
                    }
                    Vec2 rr = cw - rb.pos;
                    Vec3 vs{rb.vel.x - rb.omega * rr.y, rb.vel.y + rb.omega * rr.x, 0};
                    softContact(*this, e, A, n, pen, vs, b.muWall, b.eFloor, b.hertzK, e.mass, h);
                    e.lastRobot = int(r);
                    e.lastRobotTime = ts;
                    if (e.fromTip) {
                        rules_->addViolation(*this, "G409", int(r), "VERBAL_WARNING", "YELLOW_CARD", true, false,
                                             "robot deflected an element released by a TIPPED HIVE before it touched anything else", ts);
                        e.fromTip = false;
                    }
                }
                if (im) {   // intake roller: compliant cylinder across the mouth, surface pulling inwards
                    double rz = im->rollerZ > 0 ? im->rollerZ : season_.pollenD + im->rollerRadius - 0.014;
                    double rx = hl + im->reach - im->rollerRadius;
                    if (std::fabs(l.y) < im->width / 2) {
                        double dx = l.x - rx, dz = e.p.z - rz;
                        double dd = std::sqrt(dx * dx + dz * dz);
                        double pen = e.radius + im->rollerRadius - dd;
                        if (pen > 0 && dd > 1e-9) {
                            Vec3 nb{dx / dd, 0, dz / dd};
                            Vec3 n = rotZ(nb, rb.heading);
                            double om = rm[r].roller / std::max(im->rollerRadius, 1e-3);   // + : bottom surface moves inwards
                            Vec3 cpb{rx + nb.x * im->rollerRadius, l.y, rz + nb.z * im->rollerRadius};
                            Vec3 rel = cpb - Vec3{rx, l.y, rz};
                            Vec3 vsb = Vec3{0, om, 0}.cross(rel);
                            Vec2 cw2 = rb.bodyToWorld({cpb.x, cpb.y});
                            Vec2 rr = cw2 - rb.pos;
                            Vec3 vs = rotZ(vsb, rb.heading) + Vec3{rb.vel.x - rb.omega * rr.y, rb.vel.y + rb.omega * rr.x, 0};
                            Vec3 fBefore = A.f;
                            softContact(*this, e, A, n, pen, vs, b.muRoller, 0.3, b.hertzK * 0.15, e.mass, h);
                            if (im->joint >= 0) {
                                intakeLoad_[r] += (A.f - fBefore).norm() * im->rollerRadius / nsub;
                                intakeLoadUntil_[r] = t_ + dt;
                            }
                        }
                    }
                }
            }
        }
        // ball-ball contacts (free-free and free-held)
        for (size_t i = 0; i < elements_.size(); ++i) {
            Element& a = elements_[i];
            bool aFree = a.state == ElemState::Free, aHeld = a.state == ElemState::Held;
            if (!aFree && !aHeld) continue;
            for (size_t k = i + 1; k < elements_.size(); ++k) {
                Element& c = elements_[k];
                bool cFree = c.state == ElemState::Free, cHeld = c.state == ElemState::Held;
                if ((!cFree && !cHeld) || (!aFree && !cFree)) continue;
                Vec3 d = c.p - a.p;
                double rr = a.radius + c.radius;
                double d2 = d.norm2();
                if (d2 >= rr * rr || d2 < 1e-12) continue;
                double dist = std::sqrt(d2);
                Vec3 n = d * (1.0 / dist);   // a -> c
                double pen = rr - dist;
                double k2 = 1.0 / std::pow(std::pow(1.0 / ballPhys(a).hertzK, 2.0 / 3.0) + std::pow(1.0 / ballPhys(c).hertzK, 2.0 / 3.0), 1.5);
                double mEff = aFree && cFree ? a.mass * c.mass / (a.mass + c.mass) : (aFree ? a.mass : c.mass);
                double mu = std::min(ballPhys(a).muBall, ballPhys(c).muBall);
                double eR = std::min(ballPhys(a).eBall, ballPhys(c).eBall);
                // contact point velocities
                Vec3 va = a.v + a.w.cross(n * a.radius);
                Vec3 vc = c.v + c.w.cross(n * (-c.radius));
                if (cFree) {
                    Acc tmp;
                    softContact(*this, c, tmp, n, pen, va, mu, eR, k2, mEff, h);
                    acc[k].f += tmp.f; acc[k].tq += tmp.tq;
                    if (tmp.squish > acc[k].squish) { acc[k].squish = tmp.squish * 0.5; acc[k].sqN = n; }
                    if (aFree) {   // equal and opposite force; torque from a's own lever arm
                        acc[i].f -= tmp.f;
                        Vec3 fn = n * tmp.f.dot(n);
                        Vec3 ft = tmp.f - fn;
                        acc[i].tq += (n * a.radius).cross(ft * -1.0);
                        if (tmp.squish > acc[i].squish) { acc[i].squish = tmp.squish * 0.5; acc[i].sqN = n * -1.0; }
                    }
                } else {
                    Acc tmp;
                    softContact(*this, a, tmp, n * -1.0, pen, vc, mu, eR, k2, mEff, h);
                    acc[i].f += tmp.f; acc[i].tq += tmp.tq;
                    if (tmp.squish > acc[i].squish) { acc[i].squish = tmp.squish * 0.5; acc[i].sqN = n * -1.0; }
                }
                if (aFree) a.fromTip = false;
                if (cFree) c.fromTip = false;
            }
        }
        // integrate free balls
        for (size_t i = 0; i < elements_.size(); ++i) {
            Element& e = elements_[i];
            if (e.state != ElemState::Free) continue;
            Vec3 prev = e.p;
            e.v += acc[i].f * (h / e.mass);
            e.w += acc[i].tq * (h / ballInertia(e));
            e.p += e.v * h;
            quatIntegrate(e.q, e.w, h);
            if (acc[i].squish > e.squish) { e.squish = acc[i].squish; e.squishN = acc[i].sqN; }
            if (e.p.z < e.radius * 1.2) e.fromTip = false;
            // left the field over the perimeter
            if ((std::fabs(e.p.x) > half + 0.05 || std::fabs(e.p.y) > half + 0.05) && e.p.z > season_.wallHeight * 0.5) { ejectElement(e); continue; }
            // FLOWER entry through the top opening
            bool captured = false;
            for (size_t f = 0; f < season_.flowers.size() && !captured; ++f) {
                double hd = (e.p.xy() - season_.flowers[f].axis).norm();
                bool crossingTop = prev.z >= season_.flowerTopH && e.p.z < season_.flowerTopH && e.v.z < 0;
                if (!crossingTop || hd >= season_.flowerOpeningD / 2) continue;
                FlowerState& fs = flowers_[f];
                double top = 0;
                for (size_t q = 0; q < fs.stack.size(); ++q) top += elements_[fs.stack[q]].radius * 2 * 1.03;
                if (top + 2 * e.radius < season_.flowerTopH + e.radius) {
                    e.state = ElemState::InFlower;
                    e.container = int(f);
                    e.w = {0, 0, 0};
                    fs.stack.push_back(e.id);
                    if (e.type == ElemType::Nectar) rules_->onNectarEnteredFlower(*this, e.id, int(f), ts);
                    if (onEvent) { Json d = Json::object(); d.set("element", e.id); d.set("flower", int(f)); onEvent("flower_entry", e.lastRobot, d); }
                    captured = true;
                }
            }
            if (captured) continue;
            // CELL capture through the up-facing opening; solid underside otherwise
            for (int hh = 0; hh < 2; ++hh) {
                HiveState& hs = hives_[hh];
                if (hs.tipping) continue;
                Vec3 o = cellOpeningCenter(hh, hs.upCell);
                bool inside = std::fabs(e.p.x - o.x) < season_.cellHalfX && std::fabs(e.p.y - o.y) < season_.cellHalfY;
                if (inside && prev.z >= o.z && e.p.z < o.z && e.v.z < 0) {
                    e.state = ElemState::InCell;
                    e.container = hh * 2 + hs.upCell;
                    hs.contents[hs.upCell].push_back(e.id);
                    double wgt = e.type == ElemType::Pollen ? season_.pollenTipW : season_.nectarTipW;
                    hs.weight += wgt + season_.impactBonusPerMps * std::fabs(e.v.z);  // 'tossed in' elements tip more readily (EFSG 12.3)
                    e.v = {0, 0, 0};
                    e.w = {0, 0, 0};
                    if (onEvent) { Json d = Json::object(); d.set("element", e.id); d.set("hive", toString(Alliance(hh))); d.set("weight", hs.weight); onEvent("cell_entry", e.lastRobot, d); }
                    break;
                }
                bool underUp = inside && e.p.z < o.z - 0.02 && e.p.z > o.z - 0.30;
                if (underUp && e.v.z > 0 && prev.z <= o.z - 0.30) { e.v.z = -e.v.z * ballPhys(e).eFloor; e.fromTip = false; }
            }
            if (e.state != ElemState::Free) continue;
            // intake: a ball pulled through the mouth enters the robot's ball path (if there is room)
            for (size_t r = 0; r < robots_.size(); ++r) {
                if (rm[r].intake < 0 || !rm[r].alive) continue;
                Robot& rb = *robots_[r].robot;
                const Mechanism& im = rb.mechs[rm[r].intake];
                Vec2 l = rb.footprint().toLocal(e.p.xy());
                double hl = rb.cfg.length / 2;
                if (std::fabs(l.y) > im.width / 2 || e.p.z > im.mouthHeight || l.x > hl - e.radius * 0.6 || l.x < hl - 2.4 * e.radius) continue;
                Vec3 tb;
                ballPathPoint(int(r), 0.0, &tb);
                Vec3 tw = rotZ(tb, rb.heading);
                double inward = (e.v - Vec3{rb.vel.x, rb.vel.y, 0}).dot(tw);
                if (inward <= 0.05 || int(rb.held.size()) >= im.capacity) continue;
                double firstS = 1e9;
                for (int id : rb.held) firstS = std::min(firstS, elements_[id].pathS - elements_[id].radius);
                if (firstS < e.radius) continue;   // path backed up to the mouth: the ball stays at the roller
                e.state = ElemState::Held;
                e.holder = int(r);
                e.pathS = 0;
                e.pathV = inward;
                e.lastRobot = int(r);
                e.lastRobotTime = ts;
                rb.held.push_back(e.id);
                if (e.fromTip) {
                    rules_->addViolation(*this, "G409", int(r), "VERBAL_WARNING", "YELLOW_CARD", true, robots_[r].ai && robots_[r].ai->intendsCatch(),
                                         "robot collected an element released by a TIPPED HIVE before it touched anything else", ts);
                    e.fromTip = false;
                }
                if (onEvent) { Json d = Json::object(); d.set("element", e.id); d.set("type", e.type == ElemType::Pollen ? "POLLEN" : "NECTAR"); onEvent("intake", int(r), d); }
                break;
            }
        }
    }

    // FLOWER bottom retrieval (only POLLEN fits the retrieval opening): the bottom ball rolls into the mouth
    for (size_t r = 0; r < robots_.size(); ++r) {
        Robot& rb = *robots_[r].robot;
        if (rm[r].intake < 0 || !rm[r].alive) continue;
        const Mechanism& im = rb.mechs[rm[r].intake];
        if (rb.surfaceSpeed(rm[r].intake) <= 0.6 * im.captureSpeed || int(rb.held.size()) >= im.capacity || t_ - rb.lastFeedTime <= 0.05) continue;
        double firstS = 1e9;
        for (int id : rb.held) firstS = std::min(firstS, elements_[id].pathS - elements_[id].radius);
        FlowerState* fsel = nullptr;
        size_t fi = 0;
        for (size_t f = 0; f < season_.flowers.size(); ++f) {
            Vec2 l = rb.footprint().toLocal(season_.flowers[f].axis);
            if (l.x > rb.cfg.length / 2 - 0.02 && l.x < rb.cfg.length / 2 + im.reach + season_.flowerOuterR && std::fabs(l.y) < im.width / 2 && !flowers_[f].stack.empty()) {
                fsel = &flowers_[f];
                fi = f;
                break;
            }
        }
        if (!fsel) continue;
        Element& e = elements_[fsel->stack.front()];
        if (e.type != ElemType::Pollen || firstS < e.radius) continue;
        fsel->stack.erase(fsel->stack.begin());
        e.state = ElemState::Held;
        e.holder = int(r);
        e.container = -1;
        e.pathS = 0;
        e.pathV = 0.5;
        rb.held.push_back(e.id);
        e.lastRobot = int(r);
        intakeLoadUntil_[r] = t_ + 0.06;
        intakeLoad_[r] = 0.25;
        if (onEvent) { Json d = Json::object(); d.set("flower", int(fi)); d.set("element", e.id); onEvent("flower_retrieve", int(r), d); }
    }

    // contained elements: FLOWER stacks and CELL contents sit at their physical places
    for (auto& e : elements_) {
        if (e.state == ElemState::InFlower) {
            const FlowerState& fs = flowers_[e.container];
            size_t k = std::find(fs.stack.begin(), fs.stack.end(), e.id) - fs.stack.begin();
            double z = 0;
            for (size_t i = 0; i <= k; ++i) {
                const Element& o = elements_[fs.stack[i]];
                z += (i == 0 ? o.radius : elements_[fs.stack[i - 1]].radius + o.radius) * 1.0;
            }
            Vec2 ax = season_.flowers[e.container].axis;
            double zig = (k % 2 ? 1 : -1) * 0.006;
            e.p = {ax.x + zig, ax.y, z};
            e.v = {0, 0, 0};
            if (k > 0) { e.squish = 0.0015; e.squishN = {0, 0, 1}; }   // resting weight of the stack
        } else if (e.state == ElemState::InCell) {
            int hh = e.container / 2, c = e.container % 2;
            const auto& ct = hives_[hh].contents[c];
            size_t k = std::find(ct.begin(), ct.end(), e.id) - ct.begin();
            e.p = cellSlot(hh, c, int(k));
            e.v = {0, 0, 0};
        } else if (e.state == ElemState::OutOfField) {
            if (e.reentryAt >= 0 && t_ + dt >= e.reentryAt) {
                Vec2 p = e.p.xy();
                p.x = clampd(p.x, -half + 0.1, half - 0.1);
                p.y = clampd(p.y, -half + 0.1, half - 0.1);
                e.p = {p.x, p.y, e.radius};
                e.v = {0, 0, 0};
                e.w = {0, 0, 0};
                e.state = ElemState::Free;
                e.reentryAt = -1;
            }
        }
    }
}

}  // namespace gp
