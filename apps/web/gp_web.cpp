// WebAssembly entry point for the browser build of GamePlan.
//
// Runs the SAME gameplan_core World (physics, electrical, rules, AI) as gp-sim and the Unreal host.
// Differences, stated plainly to the user in the page:
//  - the FTC Java runtime cannot run in a browser, so the human-driven robot uses a built-in
//    mecanum TeleOp mapping (BuiltInTeleop below), not the team's Java code;
//  - recorded matches (including Java-runtime matches) can be replayed from exported frames.
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "gameplan/ai.hpp"
#include "gameplan/world.hpp"

#define GP_EXPORT(name) extern "C" __attribute__((export_name(#name)))

using namespace gp;

namespace {

struct Pad { float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0; int buttons = 0; };
Pad gPad;

// Built-in driver mapping for the web build (not team code), programmed the way competitive teams
// program a shooter robot:
//   left stick: drive (robot-centric)   right stick X / arrow keys: turn
//   RT: intake   LT: reverse intake   A (hold): FIRE   D-pad up/down: trim the hood
// The flywheel spins all match (velocity control); speed and hood angle follow the robot's distance
// to its own HIVE's up-facing CELL. Holding FIRE turns the robot to face that CELL (the driver keeps
// full translation) and opens the gate only when the wheel is at speed, the hood has settled and the
// robot is aimed - so every ball that leaves is a real attempt. A status LED (and gamepad rumble)
// shows "ready".
class BuiltInTeleop : public AIController {
public:
    BuiltInTeleop(uint64_t seed, bool competition, bool aimAssist)
        : AIController(Personality::preset("balanced"), 0.5, StrategyDirectives(), seed, "human"), competition_(competition), assist_(aimAssist) {}
    void update(World& w, int self, double t, double dt) override {
        Robot& r = *w.robots()[self].robot;
        Phase ph = w.rules().phase();
        bool allowed = competition_ ? ph == Phase::Teleop : (ph == Phase::Auto || ph == Phase::Teleop);
        status_ = 0;
        if (!allowed) {
            for (auto& p : r.ports) p.power = 0;
            if (ph != Phase::PreMatch) for (auto& s : r.elec.servos) s.pwmEnabled = false;   // SDK stop semantics
            return;
        }
        const bool fire = gPad.buttons & 1;
        // --- shooter solution for the CELL that is facing up on our own HIVE right now
        const SeasonDef& s = w.season();
        const HiveState& hs = w.hive(r.alliance);
        double side = s.hives[int(r.alliance)].cells[hs.upCell].side == "audience" ? -1 : 1;
        Vec3 target{s.hives[int(r.alliance)].pivot.x, side * s.cellOffsetY, s.cellCaptureHeight};
        double hood, fly, yaw;
        solved_ = solveShot(w, self, target, hood, fly, yaw);
        if (solved_) { assistHood_ = hood; assistFly_ = fly; aimYaw_ = yaw; }
        else aimYaw_ = std::atan2(target.y - r.pos.y, target.x - r.pos.x);
        // --- drive: driver translation always; rotation from the stick, or heading lock while firing
        double y = -gPad.ly, x = gPad.lx, rot = gPad.rx;
        if (fire && assist_) {
            double e = wrapAngle(aimYaw_ - r.heading);
            rot = clampd(-2.2 * e + 0.3 * r.omega, -0.7, 0.7);     // rot > 0 turns clockwise (omega < 0)
        }
        double d = std::max(1.0, std::fabs(y) + std::fabs(x) + std::fabs(rot));
        for (auto& wh : r.wheels) {
            int m = r.motorIndex(wh.name);
            bool front = wh.pos.x > 0, left = wh.pos.y > 0;
            // standard mecanum mixing (rot > 0 = clockwise): lf = y+x+r, rf = y-x-r, lb = y-x+r, rb = y+x-r
            double v = 0;
            if (front && left) v = y + x + rot;
            if (front && !left) v = y - x - rot;
            if (!front && left) v = y - x + rot;
            if (!front && !left) v = y + x - rot;
            r.ports[m].mode = RunMode::RunWithoutEncoder;
            r.ports[m].power = v / d * r.motorSign[m];
        }
        bool ready = false;
        for (auto& mech : r.mechs) {
            if (mech.type == MechType::Flywheel && mech.joint >= 0) {
                int mi = r.joints[mech.joint].motor;
                const GearedJoint& j = r.joints[mech.joint];
                double maxSurface = r.elec.motors[mi].spec.wFree() / j.ratio * mech.flyRadius;
                double want = solved_ ? assistFly_ : maxSurface * 0.7;    // out of range: keep it spinning
                // DcMotorEx.setVelocity(): the hub firmware's velocity loop tracks ticks/s directly
                auto& pc = r.ports[mi];
                pc.mode = RunMode::RunUsingEncoder;
                pc.velocityCommand = true;
                pc.targetVelocity = want / mech.flyRadius / (2 * kPi) * pc.ticksPerRev * r.motorSign[mi];
                pc.power = 1.0;
                double cur = j.w * mech.flyRadius;
                bool atSpeed = std::fabs(cur - want) < 0.015 * want;
                bool aimed = std::fabs(wrapAngle(aimYaw_ - r.heading)) < 0.04 && std::fabs(r.omega) < 0.6;
                // with auto-aim the gate waits for the heading lock; without it the driver aims and the
                // gate opens whenever the wheel and hood are ready (the light still shows "lined up")
                ready = solved_ && atSpeed && hoodSettled(r) && (aimed || !assist_);
                dbg_[0] = solved_; dbg_[1] = cur / std::max(want, 1e-6); dbg_[2] = wrapAngle(aimYaw_ - r.heading); dbg_[3] = r.omega; dbg_[4] = hoodSettled(r);
                status_ = ready && aimed ? 2 : 1;
            }
        }
        for (auto& mech : r.mechs) {
            if (mech.type == MechType::Intake && mech.joint >= 0)
                // belts carry balls to the shooter: they run to intake and while a ready shot feeds
                r.ports[r.joints[mech.joint].motor].power = gPad.lt > 0.1 ? -1.0 : ((gPad.rt > 0.1 || (fire && ready)) ? 1.0 : 0.0);
            if (mech.type == MechType::Hood && !mech.servos.empty()) {
                if (gPad.buttons & (1 << 6)) trim_ = std::min(0.3, trim_ + dt * 0.25);
                if (gPad.buttons & (1 << 7)) trim_ = std::max(-0.3, trim_ - dt * 0.25);
                hood_ = clampd((solved_ ? assistHood_ : 0.5) + trim_, 0.0, 1.0);
                auto& sv = r.elec.servos[mech.servos[0]];
                sv.pwmEnabled = true;
                sv.pulseUs = 600 + 1800 * hood_;
            }
            if (mech.type == MechType::Gate && !mech.servos.empty()) {
                auto& sv = r.elec.servos[mech.servos[0]];
                sv.pwmEnabled = true;
                sv.pulseUs = (fire && ready) ? 2400 : 600;   // the gate only opens for a ready shot
            }
        }
    }
    // the hood servo has reached (and holds) the commanded angle
    bool hoodSettled(const Robot& r) const {
        for (auto& m : r.mechs)
            if (m.type == MechType::Hood && !m.servos.empty()) {
                const ServoUnit& sv = r.elec.servos[m.servos[0]];
                return std::fabs(sv.angle - sv.targetAngle()) < 0.02 && std::fabs(sv.omega) < 0.2;
            }
        return true;
    }
    double hood_ = 0.5, trim_ = 0, assistHood_ = 0.5, assistFly_ = 0, aimYaw_ = 0;
    bool solved_ = false;
    int status_ = 0;
    double dbg_[5] = {0, 0, 0, 0, 0};   // diagnostics for tests: solved, speed ratio, aim error, yaw rate, hood settled   // 0 shooter off, 1 spinning / not ready, 2 at speed and aimed
private:
    bool competition_;
    bool assist_;
};

std::unique_ptr<SeasonDef> gSeason;
std::unique_ptr<Components> gComps;
std::unique_ptr<World> gWorld;
std::vector<float> gFrame;
std::string gText;
double gPreMatch = 3.0;
int gUser = 0;
std::shared_ptr<AIController> gPartner;
std::shared_ptr<BuiltInTeleop> gHuman;

Pose2 startPose(const SeasonDef& s, Alliance a, int k) {
    double x = (a == Alliance::Red ? -1 : 1) * (s.half - 0.217);
    double ys[2] = {a == Alliance::Red ? 0.05 : -0.05, a == Alliance::Red ? -1.25 : 1.25};
    return {x, ys[k % 2], a == Alliance::Red ? 0.0 : kPi};
}

}  // namespace

GP_EXPORT(gp_alloc) char* gp_alloc(int n) { return static_cast<char*>(std::malloc(size_t(n) + 1)); }
GP_EXPORT(gp_free) void gp_free(char* p) { std::free(p); }

// settings: {seed, alliance:"red"|"blue", mode:"competition"|"practice", difficulty, partnerInstruction,
//            opponentPersonality, partnerPersonality, uncertainty:"realistic",
//            aimAssist: true (Fire turns the robot to its HIVE), demo: false (the user robot is AI-driven too,
//            for the match that plays behind the menus)}
GP_EXPORT(gp_init) int gp_init(const char* season, const char* comps, const char* userRobot, const char* aiRobot, const char* settings) {
    Json st = Json::parse(settings);
    gSeason.reset(new SeasonDef(SeasonDef::fromText(season)));
    gComps.reset(new Components(Components::fromText(comps)));
    WorldConfig wc;
    wc.seed = uint64_t(st["seed"].num(1));
    wc.dt = 0.001;
    wc.competitionMode = st["mode"].str("competition") != "practice";
    wc.uncertainty.setAll(parseULevel(st["uncertainty"].str("realistic")));
    gWorld.reset(new World(*gSeason, *gComps, wc));
    RobotConfig ur = RobotConfig::fromJson(Json::parse(userRobot));
    RobotConfig ar = RobotConfig::fromJson(Json::parse(aiRobot));
    Alliance ua = st["alliance"].str("red") == "blue" ? Alliance::Blue : Alliance::Red;
    Alliance oa = other(ua);
    double diff = st["difficulty"].num(0.6);
    std::shared_ptr<AIController> human;
    if (st["demo"].boolean(false)) {
        human = std::make_shared<AIController>(Personality::preset("aggressive"), diff, StrategyDirectives(), wc.seed, "demo");
        gHuman.reset();
    } else {
        gHuman = std::make_shared<BuiltInTeleop>(wc.seed, wc.competitionMode, st["aimAssist"].boolean(true));
        human = gHuman;
    }
    gPartner = std::make_shared<AIController>(Personality::preset(st["partnerPersonality"].str("coordinated")), diff,
                                              StrategyDirectives::parse(st["partnerInstruction"].str("")), wc.seed, "partner");
    auto o1 = std::make_shared<AIController>(Personality::preset(st["opponentPersonality"].str("aggressive")), diff, StrategyDirectives(), wc.seed, "opp1");
    auto o2 = std::make_shared<AIController>(Personality::preset("defensive"), diff, StrategyDirectives(), wc.seed, "opp2");
    std::string ul = ua == Alliance::Red ? "red" : "blue", ol = oa == Alliance::Red ? "red" : "blue";
    gUser = gWorld->addRobot(ur, ua, ul + "1", startPose(*gSeason, ua, 0), false, human);
    int partner = gWorld->addRobot(ar, ua, ul + "2", startPose(*gSeason, ua, 1), false, gPartner);
    int opp1 = gWorld->addRobot(ar, oa, ol + "1", startPose(*gSeason, oa, 0), false, o1);
    int opp2 = gWorld->addRobot(ar, oa, ol + "2", startPose(*gSeason, oa, 1), false, o2);
    gPartner->partner = gUser;
    o1->partner = opp2;
    o2->partner = opp1;
    (void)partner;
    gWorld->stageField();
    return int(gWorld->robots().size());
}

GP_EXPORT(gp_set_pad) void gp_set_pad(float lx, float ly, float rx, float ry, float lt, float rt, int buttons) {
    gPad.lx = lx; gPad.ly = ly; gPad.rx = rx; gPad.ry = ry; gPad.lt = lt; gPad.rt = rt; gPad.buttons = buttons;
}

// Advance simulated time by dt seconds (fixed internal step). Returns 1 when the match is final.
GP_EXPORT(gp_step) int gp_step(double dt) {
    if (!gWorld) return 1;
    double until = gWorld->time() + dt;
    while (gWorld->time() + 1e-9 < until) {
        if (gWorld->rules().phase() == Phase::PreMatch && gWorld->time() >= gPreMatch) gWorld->rules().startMatch(gPreMatch);
        gWorld->step();
        if (gWorld->rules().isFinal()) return 1;
    }
    return 0;
}

// Frame buffer layout (floats):
// [0] t [1] phase [2] scoreRed [3] scoreBlue [4] nRobots [5] nElements [6] nCells
// robots: x y heading alliance held tipped length width height intakeAngle flyAngle hoodAngle
//         wheelAngle[4] (lf rf lb rb) pitch shooterStatus roll  (19 each)
// elements: x y z kind(0 pollen,1 red nectar,2 blue nectar) radius visible qw qx qy qz squish nx ny nz (14 each)
// cells: x y z up(1/0) alliance  (5 each), then hiveAngle[2] (rad about +x through the pivot),
// then walls[4] x (flex m, flex speed m/s) for -x, +x, -y, +y
GP_EXPORT(gp_frame) float* gp_frame() {
    gFrame.clear();
    if (!gWorld) return gFrame.data();
    World& w = *gWorld;
    gFrame.push_back(float(w.time()));
    gFrame.push_back(float(int(w.rules().phase())));
    gFrame.push_back(float(w.rules().score(Alliance::Red).total()));
    gFrame.push_back(float(w.rules().score(Alliance::Blue).total()));
    gFrame.push_back(float(w.robots().size()));
    gFrame.push_back(float(w.elements().size()));
    gFrame.push_back(4.0f);
    for (auto& s : w.robots()) {
        const Robot& r = *s.robot;
        for (double v : {r.pos.x, r.pos.y, r.heading, double(int(r.alliance)), double(r.held.size()), r.tipped ? 1.0 : 0.0,
                         r.cfg.length, r.cfg.width, r.cfg.height})
            gFrame.push_back(float(v));
        double ang[3] = {0, 0, 0};
        for (auto& m : r.mechs) {
            if (m.type == MechType::Intake && m.joint >= 0) ang[0] = r.joints[m.joint].theta;
            if (m.type == MechType::Flywheel && m.joint >= 0) ang[1] = r.joints[m.joint].theta;
            if (m.type == MechType::Hood && !m.servos.empty()) {
                const ServoUnit& sv = r.elec.servos[m.servos[0]];
                double span = sv.spec.rangeRad > 0 ? sv.spec.rangeRad : 1.0;
                ang[2] = m.angleAt0 + clampd(sv.angle / span + 0.5, 0.0, 1.0) * (m.angleAt1 - m.angleAt0);
            }
        }
        for (double v : ang) gFrame.push_back(float(v));
        double wa[4] = {0, 0, 0, 0};
        for (auto& wh : r.wheels) {
            int k = (wh.pos.x > 0 ? 0 : 2) + (wh.pos.y > 0 ? 0 : 1);
            if (wh.joint >= 0) wa[k] = r.joints[wh.joint].theta;
        }
        for (double v : wa) gFrame.push_back(float(v));
        gFrame.push_back(float(r.pitch));
        // shooter status LED: 0 off, 1 spinning up / not aimed, 2 ready (user robot's TeleOp only)
        size_t idx = size_t(&s - &w.robots()[0]);
        gFrame.push_back(float(idx == size_t(gUser) && gHuman ? gHuman->status_ : 0));
        gFrame.push_back(float(r.roll));
    }
    for (auto& e : w.elements()) {
        bool vis = e.state != ElemState::HumanArea && e.state != ElemState::OutOfField;
        double kind = e.type == ElemType::Pollen ? 0 : (e.color == Alliance::Red ? 1 : 2);
        for (double v : {e.p.x, e.p.y, e.p.z, kind, e.radius, vis ? 1.0 : 0.0, e.q[0], e.q[1], e.q[2], e.q[3], e.squish,
                         e.squishN.x, e.squishN.y, e.squishN.z})
            gFrame.push_back(float(v));
    }
    for (int h = 0; h < 2; ++h)
        for (int c = 0; c < 2; ++c) {
            const CellDef& cd = gSeason->hives[h].cells[c];
            Vec3 p{0, 0, 0};
            for (int id : cd.tags) p += w.tagPosition(id);
            p = p / double(cd.tags.size());
            bool up = w.hive(Alliance(h)).upCell == c && !w.hive(Alliance(h)).tipping;
            for (double v : {p.x, p.y, p.z, up ? 1.0 : 0.0, double(h)}) gFrame.push_back(float(v));
        }
    // HIVE body rotation about its pivot (+x), 0 = audience CELL up (same convention as World::cellSlot)
    for (int h = 0; h < 2; ++h) {
        const HiveState& hs = w.hive(Alliance(h));
        int aud = gSeason->hives[h].cells[0].side == "audience" ? 0 : 1;
        double down = hs.upCell == aud ? 0.0 : 1.0;
        if (hs.tipping) {
            double f = clampd((w.time() - hs.tipStart) / gSeason->tipSettleS, 0.0, 1.0);
            down = hs.upCell == aud ? f : 1.0 - f;
        }
        gFrame.push_back(float(down * hs.stateAngle));
    }
    // perimeter walls (-x, +x, -y, +y): panel flex (m) and its speed (m/s), for the wobble and the thud
    for (int k = 0; k < 4; ++k) { gFrame.push_back(float(w.wallDisplacement(k))); gFrame.push_back(float(w.wallVelocity(k))); }
    return gFrame.data();
}
GP_EXPORT(gp_frame_len) int gp_frame_len() { return int(gFrame.size()); }
// test diagnostics of the built-in TeleOp's shooter (not shown in the app)
GP_EXPORT(gp_shooter_debug) const char* gp_shooter_debug() {
    Json j = Json::array();
    if (gHuman) for (double v : gHuman->dbg_) j.push(v);
    gText = j.dump(); return gText.c_str();
}

// Static geometry of robot i for rendering: chassis, wheels, intake roller and mouth, internal ball
// path, shooter. Body frame metres (x forward, y left, z up).
GP_EXPORT(gp_robot_info) const char* gp_robot_info(int i) {
    Json j = Json::object();
    if (!gWorld || i < 0 || i >= int(gWorld->robots().size())) { gText = "{}"; return gText.c_str(); }
    const World& w = *gWorld;
    const Robot& r = *w.robots()[i].robot;
    j.set("label", r.label); j.set("name", r.cfg.name); j.set("alliance", toString(r.alliance));
    j.set("length", r.cfg.length); j.set("width", r.cfg.width); j.set("height", r.cfg.height);
    Json wh = Json::array();
    for (auto& x : r.wheels) {
        Json o = Json::object();
        o.set("x", x.pos.x); o.set("y", x.pos.y); o.set("r", x.radius);
        o.set("roller", std::atan2(x.n.y, x.n.x));   // traction direction -> roller angle of a mecanum wheel
        wh.push(o);
    }
    j.set("wheels", wh);
    Json path = Json::array();
    for (auto& p : w.ballPath(i)) { Json o = Json::array(); o.push(p.x); o.push(p.y); o.push(p.z); path.push(o); }
    j.set("ballPath", path);
    for (auto& m : r.mechs) {
        Json o = Json::object();
        if (m.type == MechType::Intake) {
            o.set("rollerRadius", m.rollerRadius); o.set("width", m.width); o.set("reach", m.reach);
            o.set("rollerZ", m.rollerZ > 0 ? m.rollerZ : w.season().pollenD + m.rollerRadius - 0.014);
            o.set("mouthHeight", m.mouthHeight);
            j.set("intake", o);
        }
        if (m.type == MechType::Flywheel) {
            o.set("radius", m.flyRadius); o.set("x", m.launchFwd); o.set("z", m.launchHeight);
            j.set("flywheel", o);
        }
        if (m.type == MechType::Hood) { o.set("a0", m.angleAt0); o.set("a1", m.angleAt1); j.set("hood", o); }
    }
    gText = j.dump();
    return gText.c_str();
}

// Driver-facing HUD data only (no electrical telemetry during the match).
GP_EXPORT(gp_hud) const char* gp_hud() {
    Json j = Json::object();
    if (!gWorld) { gText = "{}"; return gText.c_str(); }
    World& w = *gWorld;
    j.set("timer", w.rules().timerText(w.time()));
    j.set("phase", toString(w.rules().phase()));
    j.set("red", w.rules().score(Alliance::Red).total());
    j.set("blue", w.rules().score(Alliance::Blue).total());
    const Robot& r = *w.robots()[gUser].robot;
    std::string conn = "Connected";
    if (!r.rcAlive) conn = "Disconnected";
    else if (r.disabled) conn = "Reconnecting";
    else for (auto& h : r.elec.hubs) if (h.inReset) conn = "Reconnecting";
    j.set("connection", conn);
    j.set("held", int(r.held.size()));
    j.set("final", w.rules().isFinal());
    j.set("partnerTask", gPartner ? gPartner->task() : "");
    gText = j.dump();
    return gText.c_str();
}

// Post-match result incl. electrical summary of the user's robot (shown only after the match).
GP_EXPORT(gp_result) const char* gp_result() {
    if (!gWorld) { gText = "{}"; return gText.c_str(); }
    Json j = gWorld->rules().resultJson();
    const Robot& r = *gWorld->robots()[gUser].robot;
    Json e = Json::object();
    e.set("energy_J", r.elec.battery.st.energyJ);
    e.set("peakBatteryA", r.elec.battery.st.peakCurrent);
    e.set("finalSoc", r.elec.battery.st.soc);
    double minBus = 99;
    for (auto& h : r.elec.hubs) minBus = std::min(minBus, h.minBusV);
    e.set("minHubBusV", minBus);
    Json motors = Json::array();
    for (auto& m : r.elec.motors) {
        Json o = Json::object();
        o.set("name", m.name); o.set("energy_J", m.energyJ); o.set("peak_A", m.peakCurrent); o.set("winding_C", m.tWinding);
        motors.push(o);
    }
    e.set("motors", motors);
    Json ev = Json::array();
    for (auto& x : r.elec.events) {
        Json o = Json::object();
        o.set("t", x.t); o.set("kind", x.kind); o.set("device", x.device); o.set("value", x.value);
        ev.push(o);
    }
    e.set("events", ev);
    j.set("userRobotElectrical", e);
    j.set("distance_m", r.distance);
    j.set("partnerDirectives", gPartner ? gPartner->directives().toJson() : Json());
    gText = j.dump();
    return gText.c_str();
}

GP_EXPORT(gp_parse_instruction) const char* gp_parse_instruction(const char* text) {
    gText = StrategyDirectives::parse(text).toJson().dump();
    return gText.c_str();
}

// Electrical live sample of the user's robot for the post-match chart recorder (the page buffers
// these and only displays them after the match).
GP_EXPORT(gp_elec_sample) float* gp_elec_sample() {
    gFrame.clear();
    if (!gWorld) return gFrame.data();
    const Robot& r = *gWorld->robots()[gUser].robot;
    gFrame.push_back(float(gWorld->time()));
    gFrame.push_back(float(r.elec.battery.st.vTerm));
    gFrame.push_back(float(r.elec.battery.st.current));
    gFrame.push_back(float(r.elec.hubs[0].busV));
    for (auto& m : r.elec.motors) gFrame.push_back(float(m.current));
    return gFrame.data();
}

int main() { return 0; }

// WASI libc++ is built without exception support but some of its objects still reference the
// throw ABI (bad_alloc, length_error). Any such path is fatal in the browser build.
extern "C" void* __cxa_allocate_exception(size_t) { std::abort(); }
extern "C" void __cxa_throw(void*, void*, void (*)(void*)) { std::abort(); }
extern "C" void __cxa_rethrow() { std::abort(); }
