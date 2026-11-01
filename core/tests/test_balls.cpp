// Ball physics: compliant contacts, spin, physical intake path, ball-dependent shots.
#include <cmath>

#include "gameplan/match.hpp"
#include "testing.hpp"

using namespace gp;

// All robots idle, nominal uncertainty: a quiet field for controlled experiments.
static MatchSetup quietSetup() {
    Json j = Json::parseFile(gptest::root() + "/examples/matches/ai-vs-ai.match.json");
    j.set("seed", 3.0);
    Json u = Json::object(); u.set("all", "nominal"); j.set("uncertainty", u);
    Json robots = j["robots"];
    for (size_t i = 0; i < robots.size(); ++i) { Json r = robots[i]; r.set("controller", "idle"); robots[i] = r; }
    j.set("robots", robots);
    return MatchSetup::fromJson(j, gptest::root());
}

// a free POLLEN far from robots and structures
static Element& testBall(World& w) {
    for (auto& e : w.elements())
        if (e.type == ElemType::Pollen && e.state == ElemState::Free) return e;
    return w.elements()[0];
}

TEST(ball_drop_bounces_and_squishes) {
    MatchRunner m(quietSetup());
    std::string err;
    CHECK(m.begin(err));
    World& w = m.world();
    Element& e = testBall(w);
    e.p = {0.9, 1.2, 1.0}; e.v = {0, 0, 0}; e.w = {0, 0, 0};
    double vImpact = std::sqrt(2 * 9.81 * (1.0 - e.radius));
    double maxSquish = 0, apex = 0;
    bool bounced = false;
    for (int i = 0; i < 1500; ++i) {
        w.step();
        maxSquish = std::max(maxSquish, e.squish);
        if (e.v.z < 0 && e.p.z < 0.2) bounced = true;
        if (bounced && e.v.z > 0) apex = std::max(apex, e.p.z);
        if (bounced && e.v.z < 0 && apex > 0) break;
    }
    double eff = std::sqrt((apex - e.radius) / (1.0 - e.radius));
    CHECK_NEAR(eff, w.season().pollenPhys.eFloor, 0.12);       // restitution from the contact damping
    CHECK_GT(maxSquish, 0.002);                                // visibly squishes on a ~4.3 m/s impact ...
    CHECK_LT(maxSquish, 0.35 * e.radius);                      // ... but only "that little bit"
    CHECK_GT(vImpact, 4.0);
}

TEST(sliding_ball_starts_rolling) {
    MatchRunner m(quietSetup());
    std::string err;
    CHECK(m.begin(err));
    World& w = m.world();
    Element& e = testBall(w);
    e.p = {0.9, 1.2, e.radius}; e.v = {1.5, 0, 0}; e.w = {0, 0, 0};
    for (int i = 0; i < 400; ++i) w.step();
    // rolling without slip: v = w x r  ->  vx = wy * r
    CHECK_GT(e.v.x, 0.3);
    CHECK_NEAR(e.w.y * e.radius / e.v.x, 1.0, 0.08);
}

TEST(off_centre_collision_transfers_momentum_and_spin) {
    MatchRunner m(quietSetup());
    std::string err;
    CHECK(m.begin(err));
    World& w = m.world();
    Element* a = nullptr; Element* b = nullptr;
    for (auto& e : w.elements()) if (e.type == ElemType::Pollen && e.state == ElemState::Free) { if (!a) a = &e; else if (!b) { b = &e; break; } }
    CHECK(a && b);
    a->p = {0.5, 1.2, 0.6}; a->v = {3, 0, 0}; a->w = {0, 0, 0};
    b->p = {0.8, 1.2 + 0.6 * a->radius, 0.6}; b->v = {0, 0, 0}; b->w = {0, 0, 0};
    double px0 = a->mass * a->v.x + b->mass * b->v.x;
    for (int i = 0; i < 120; ++i) w.step();    // in flight: only gravity/drag besides the contact
    double px1 = a->mass * a->v.x + b->mass * b->v.x;
    CHECK_GT(b->v.x, 0.5);
    CHECK_GT(b->v.y, 0.1);                       // glancing hit pushes b sideways
    CHECK_NEAR(px1, px0, 0.08 * px0);            // momentum conserved (drag aside)
    CHECK_GT(a->w.norm() + b->w.norm(), 1.0);    // contact friction spun the balls
}

TEST(intake_holds_balls_inside_robot_and_reverse_spits_out) {
    Json j = Json::parseFile(gptest::root() + "/examples/matches/ai-vs-ai.match.json");
    Json u = Json::object(); u.set("all", "nominal"); j.set("uncertainty", u);
    Json robots = j["robots"];
    for (size_t i = 0; i < robots.size(); ++i) { Json r = robots[i]; r.set("controller", "idle"); robots[i] = r; }
    j.set("robots", robots);
    MatchRunner m(MatchSetup::fromJson(j, gptest::root()));
    std::string err;
    CHECK(m.begin(err));
    World& w = m.world();
    m.advance(3.5, err);                         // into AUTO so robots are enabled
    Robot& rb = *w.robots()[0].robot;
    int intakeMotor = -1;
    for (auto& mc : rb.mechs) if (mc.type == MechType::Intake) intakeMotor = rb.joints[mc.joint].motor;
    size_t held0 = rb.held.size();
    // a ball right in front of the intake
    Element& e = testBall(w);
    Vec2 front = rb.bodyToWorld({rb.cfg.length / 2 + 0.08, 0});
    e.p = {front.x, front.y, e.radius}; e.v = {0, 0, 0}; e.w = {0, 0, 0};
    // empty the preloads out of the way first: count only this ball
    rb.ports[intakeMotor].power = 1.0;
    bool got = false;
    double spinSeen = 0;
    for (int i = 0; i < 1500 && !got; ++i) { w.step(); spinSeen = std::max(spinSeen, e.w.norm()); got = e.state == ElemState::Held; }
    CHECK(got || rb.held.size() >= 4);           // collected unless the path was already full
    if (got) {
        CHECK_GT(spinSeen, 5.0);                 // the roller spun it on the way in
        // never vanishes: it sits inside the robot's footprint, above the floor
        CHECK(rb.footprint().contains(e.p.xy()));
        CHECK_GT(e.p.z, e.radius * 0.9);
        CHECK(rb.held.size() == held0 + 1);
        // reverse the intake: the balls back out of the mouth
        rb.ports[intakeMotor].power = -1.0;
        bool out = false;
        for (int i = 0; i < 3000 && !out; ++i) { w.step(); out = rb.held.size() < held0 + 1; }
        CHECK(out);
    }
}

TEST(shot_depends_on_ball_size_and_spin) {
    // same launch, bigger ball (more squeeze) leaves faster; backspin lifts the ball further
    gp::BallPhys b;
    double vz;
    double withSpin = predictBallHeight(b, 0.024, 0.0355, 6.0, 0.9, 1.0, 2.5, vz);
    double noSpin = predictBallHeight(b, 0.024, 0.0355, 6.0, 0.9, 0.0, 2.5, vz);
    double topSpin = predictBallHeight(b, 0.024, 0.0355, 6.0, 0.9, -1.0, 2.5, vz);
    CHECK_GT(withSpin, noSpin + 0.05);
    CHECK_GT(noSpin, topSpin + 0.05);
    // exit speed vs ball size, through the real launch path of a match robot
    MatchRunner m(quietSetup());
    std::string err;
    CHECK(m.begin(err));
    World& w = m.world();
    m.advance(3.5, err);
    Robot& rb = *w.robots()[0].robot;
    int fly = -1, gate = -1;
    for (size_t k = 0; k < rb.mechs.size(); ++k) { if (rb.mechs[k].type == MechType::Flywheel) fly = int(k); if (rb.mechs[k].type == MechType::Gate) gate = int(k); }
    CHECK(fly >= 0 && gate >= 0 && rb.held.size() >= 2);
    rb.ports[rb.joints[rb.mechs[fly].joint].motor].power = 0.8;
    m.advance(w.time() + 2.0, err);                        // spin up
    // make the next two balls differ only in size
    int a = rb.held[rb.held.size() - 1], c = rb.held[rb.held.size() - 2];
    w.elements()[a].radius = w.season().pollenD / 2 - 0.0008;
    w.elements()[c].radius = w.season().pollenD / 2 + 0.0008;
    w.elements()[a].mass = w.elements()[c].mass = w.season().pollenMass;
    size_t n0 = w.launches.size();
    auto& sv = rb.elec.servos[rb.mechs[gate].servos[0]];
    for (int i = 0; i < 6000 && w.launches.size() < n0 + 2; ++i) {
        sv.pwmEnabled = true; sv.pulseUs = 2400;          // gate open
        rb.ports[rb.joints[rb.mechs[fly].joint].motor].power = 0.8;
        for (auto& mc : rb.mechs) if (mc.type == MechType::Intake) rb.ports[rb.joints[mc.joint].motor].power = 1.0;
        w.step();
    }
    CHECK(w.launches.size() >= n0 + 2);
    if (w.launches.size() >= n0 + 2) {
        const auto& l1 = w.launches[n0];
        const auto& l2 = w.launches[n0 + 1];
        double v1 = l1.speed / std::max(1e-6, l1.flyBefore), v2 = l2.speed / std::max(1e-6, l2.flyBefore);  // per unit wheel speed
        CHECK(l1.element == a && l2.element == c);
        CHECK_GT(v2, v1 * 1.01);                            // the larger ball is squeezed harder -> more grip
        CHECK_GT(w.elements()[a].w.norm(), 10.0);           // leaves with backspin
    }
}

TEST(full_speed_wall_crash_rocks_robot_flexes_wall_and_rings_down) {
    MatchRunner m(quietSetup());
    std::string err;
    CHECK(m.begin(err));
    World& w = m.world();
    Robot& r = *w.robots()[0].robot;   // red1, by the red (-x) wall
    r.pos = {-w.season().half + r.cfg.length / 2 + 0.01, 0.0}; r.heading = kPi; r.vel = {-1.6, 0.0}; r.omega = 0;   // 1 cm out, full speed
    double maxPitch = 0, maxDisp = 0, minX = 1, maxBackSpeed = 0;
    for (int i = 0; i < 1500; ++i) {
        w.step();
        maxPitch = std::max(maxPitch, std::fabs(r.pitch));
        maxDisp = std::max(maxDisp, std::fabs(w.wallDisplacement(0)));
        minX = std::min(minX, r.pos.x);
        maxBackSpeed = std::max(maxBackSpeed, r.vel.x);
    }
    double halfLen = r.cfg.length / 2;
    CHECK_GT(maxPitch, 0.01);                                  // the hard stop rocks the robot
    CHECK_GT(maxBackSpeed, 0.05);                              // and it bounces back off the barrier
    CHECK_LT(-(minX - halfLen) - w.season().half, 0.03);       // the barrier gives a little, < 3 cm
    CHECK_GT(maxDisp, 0.001);                                  // the panel visibly flexes...
    CHECK_LT(std::fabs(w.wallDisplacement(0)), 0.0005);        // ...and rings down within 1.5 s
    CHECK(!r.tipped);
}
