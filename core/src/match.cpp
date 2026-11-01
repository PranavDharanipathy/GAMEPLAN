#include "gameplan/match.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <cstdlib>

namespace gp {

std::string gameplanVersion() { return "0.1.0"; }

static bool fileExists(const std::string& p) {
    std::ifstream f(p);
    if (f) return true;
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}
static std::string absolute(const std::string& p) {
    if (p.empty() || p[0] == '/' || (p.size() > 1 && p[1] == ':')) return p;
#ifdef _WIN32
    char b[4096];
    return _fullpath(b, p.c_str(), sizeof b) ? std::string(b) : p;
#else
    char* r = realpath(p.c_str(), nullptr);
    if (!r) return p;
    std::string s(r);
    free(r);
    return s;
#endif
}

std::string resolvePath(const std::string& p, const std::string& baseDir) {
    if (p.empty()) return p;
    if (p[0] == '/' || (p.size() > 1 && p[1] == ':')) return p;
    if (fileExists(p)) return absolute(p);
    if (!baseDir.empty() && fileExists(baseDir + "/" + p)) return absolute(baseDir + "/" + p);
    std::string root = GP_SOURCE_ROOT;
    if (fileExists(root + "/" + p)) return absolute(root + "/" + p);
    return baseDir.empty() ? p : baseDir + "/" + p;
}

// ------------------------------------------------------------------ gamepad scripts
static void setKey(GamepadState& g, const std::string& k, double v) {
    if (k == "lx") g.lx = float(v); else if (k == "ly") g.ly = float(v); else if (k == "rx") g.rx = float(v);
    else if (k == "ry") g.ry = float(v); else if (k == "lt") g.lt = float(v); else if (k == "rt") g.rt = float(v);
    else {
        static const std::map<std::string, uint32_t> bits = {
            {"a", BTN_A}, {"b", BTN_B}, {"x", BTN_X}, {"y", BTN_Y}, {"left_bumper", BTN_LB}, {"right_bumper", BTN_RB},
            {"dpad_up", BTN_DUP}, {"dpad_down", BTN_DDOWN}, {"dpad_left", BTN_DLEFT}, {"dpad_right", BTN_DRIGHT},
            {"back", BTN_BACK}, {"start", BTN_START}, {"guide", BTN_GUIDE}, {"left_stick_button", BTN_LSTICK},
            {"right_stick_button", BTN_RSTICK}, {"touchpad", BTN_TOUCHPAD}};
        auto it = bits.find(k);
        if (it == bits.end()) throw std::runtime_error("unknown gamepad key: " + k);
        if (v != 0) g.buttons |= it->second; else g.buttons &= ~it->second;
    }
}

ScriptedGamepad ScriptedGamepad::load(const std::string& path) {
    ScriptedGamepad s;
    Json j = Json::parseFile(path);
    for (auto& e : j["events"].items()) {
        int pad = int(e["gamepad"].num(1));
        for (auto& kv : e["set"].members())
            s.events.push_back({e["t"].num(), pad, kv.first, kv.second.isBool() ? (kv.second.boolean() ? 1.0 : 0.0) : kv.second.num()});
    }
    std::stable_sort(s.events.begin(), s.events.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    return s;
}
ScriptedGamepad ScriptedGamepad::fromRecording(const std::string& path) {
    ScriptedGamepad s;
    s.fromRecordingTimeline = true;
    for (auto& row : readGzTable(path)) {
        if (row.size() < 15) continue;
        s.timeline.push_back({row[0], {GamepadState::fromVec(row, 1), GamepadState::fromVec(row, 8)}});
    }
    return s;
}
void ScriptedGamepad::stateAt(double mt, GamepadState& g1, GamepadState& g2) const {
    if (fromRecordingTimeline) {
        g1 = GamepadState(); g2 = GamepadState();
        for (auto& e : timeline) { if (e.first > mt + 1e-9) break; g1 = e.second.first; g2 = e.second.second; }
        return;
    }
    g1 = GamepadState(); g2 = GamepadState();
    for (auto& e : events) {
        if (e.t > mt + 1e-9) break;
        setKey(e.pad == 2 ? g2 : g1, e.key, e.value);
    }
}

// ------------------------------------------------------------------ setup
MatchSetup MatchSetup::fromJson(const Json& j, const std::string& base) {
    MatchSetup s;
    s.raw = j;
    s.baseDir = base;
    s.seasonPath = resolvePath(j["season"].str("seasons/biobuzz-2027/season.json"), base);
    s.componentsPath = resolvePath(j["components"].str("data/hardware/components.json"), base);
    s.recordDir = j["record"].str("");
    s.world.seed = uint64_t(j["seed"].num(1));
    s.world.competitionMode = j["mode"].str("competition") != "practice";
    s.world.fidelity = j["fidelity"].str("realtime");
    s.world.dt = s.world.fidelity == "engineering" ? 0.00025 : 0.001;
    if (j["dt"].isNumber()) s.world.dt = j["dt"].num();
    s.world.uncertainty = UncertaintyConfig::fromJson(j["uncertainty"]);
    const Json& e = j["env"];
    s.world.env.humidity = e["humidity"].num(0.40);
    s.world.env.ambientC = e["ambientC"].num(25);
    s.world.env.antiStaticTreated = e["antiStatic"].boolean(true);
    s.world.referee = RefereeConfig::fromJson(j["referee"]);
    s.preMatchS = j["preMatch_s"].num(3.0);
    const Json& rt = j["runtime"];
    s.runtime.java = rt["java"].str("java");
    s.runtime.runtimeJar = resolvePath(rt["jar"].str("runtime/build/gameplan-runtime.jar"), base);
    s.runtime.timingProfile = resolvePath(rt["timingProfile"].str("data/hardware/timing_profiles.json"), base);
    s.runtime.deterministicCompute = rt["deterministicCompute"].boolean(true);
    s.runtime.sandbox = rt["sandbox"].boolean(true);
    s.runtime.watchdogS = rt["watchdog_s"].num(30);
    for (auto& r : j["robots"].items()) {
        RobotEntry re;
        re.label = r["label"].str();
        re.alliance = parseAlliance(r["alliance"].str());
        re.robotPath = resolvePath(r["robot"].str("examples/robots/generic-launcher.robot.json"), base);
        re.controller = r["controller"].str("idle");
        if (r["start"].isArray()) {
            re.start = {r["start"][0].num(), r["start"][1].num(), r["start"][2].num() * kPi / 180.0};
            re.startGiven = true;
        }
        const Json& rr = r["runtime"];
        re.packageDir = rr["package"].isString() ? resolvePath(rr["package"].str(), base) : "";
        re.autoOpMode = rr["auto"].str("");
        re.teleOpMode = rr["teleop"].str("");
        re.gamepadScript = rr["gamepadScript"].isString() ? resolvePath(rr["gamepadScript"].str(), base) : "";
        const Json& ai = r["ai"];
        re.personality = ai["personality"].isObject() ? Personality::fromJson(ai["personality"]) : Personality::preset(ai["personality"].str("balanced"));
        re.difficulty = ai["difficulty"].num(0.6);
        re.instruction = ai["instruction"].str("");
        s.robots.push_back(re);
    }
    return s;
}
MatchSetup MatchSetup::load(const std::string& path) {
    std::string base = path.substr(0, path.find_last_of("/\\") == std::string::npos ? 0 : path.find_last_of("/\\"));
    return fromJson(Json::parseFile(path), base.empty() ? "." : base);
}

// ------------------------------------------------------------------ parameter changes
bool applyParam(World& w, const ParamChange& c, std::string& err) {
    std::string key = c.key;
    if (key.rfind("env.", 0) == 0) {
        err = "env.* changes are applied at setup time only";
        return false;
    }
    size_t dot = key.find('.');
    if (dot == std::string::npos) { err = "parameter must be <robotLabel>.<path>: " + key; return false; }
    std::string label = key.substr(0, dot), path = key.substr(dot + 1);
    Robot* r = nullptr;
    for (auto& s : w.robots()) if (s.robot->label == label) r = s.robot.get();
    if (!r) { err = "unknown robot label: " + label; return false; }
    auto parts = [&](const std::string& p) {
        std::vector<std::string> out;
        std::stringstream ss(p);
        std::string x;
        while (std::getline(ss, x, '.')) out.push_back(x);
        return out;
    };
    auto ps = parts(path);
    if (path == "battery.soc") { r->elec.battery.st.soc = c.value; return true; }
    if (path == "battery.r0_scale") { r->elec.battery.r0Scale = c.value; return true; }
    if (path == "battery.cycles") { r->elec.battery.st.cycles = c.value; return true; }
    if (path == "mass") { r->cfg.mass = c.value; r->yawInertia = c.value * (r->cfg.length * r->cfg.length + r->cfg.width * r->cfg.width) / 12.0; return true; }
    if (path == "cog_z") { r->cfg.cogZ = c.value; return true; }
    if (path == "tile_mu") { r->cfg.tileMu = c.value; return true; }
    if (path == "main_path_scale") { r->elec.mainPath.scale = c.value; return true; }
    if (ps.size() == 3 && ps[0] == "motor") {
        int m = r->motorIndex(ps[1]);
        if (m < 0) { err = "unknown motor " + ps[1]; return false; }
        auto& mu = r->elec.motors[m];
        if (ps[2] == "kt_scale") { mu.ktScale = c.value; return true; }
        if (ps[2] == "r_scale") { mu.rScale = c.value; return true; }
        if (ps[2] == "friction_scale") { mu.frictionScale = c.value; return true; }
        if (ps[2] == "gear_ratio") {
            auto& j = r->joints[r->motorJoint[m]];
            double k = c.value / j.ratio;
            j.ratio = c.value;
            j.wm /= k; j.thetam /= k;  // keep output state, re-express motor side
            r->ports[m].ticksPerRev = mu.spec.encoderCPR * c.value;
            return true;
        }
        if (ps[2] == "backlash") { r->joints[r->motorJoint[m]].backlash = c.value; return true; }
    }
    if (ps.size() == 3 && ps[0] == "mech") {
        for (auto& m : r->mechs) {
            if (m.id != ps[1]) continue;
            if (ps[2] == "capacity") { m.capacity = int(c.value); return true; }
            if (ps[2] == "launch_eff") { m.launchEff = c.value; return true; }
            if (ps[2] == "capture_speed") { m.captureSpeed = c.value; return true; }
            if (ps[2] == "feed_interval") { m.feedInterval = c.value; return true; }
        }
        err = "unknown mechanism parameter " + path;
        return false;
    }
    err = "unsupported parameter: " + path;
    return false;
}

// ------------------------------------------------------------------ runner
MatchRunner::MatchRunner(const MatchSetup& s)
    : setup_(s), season_(SeasonDef::load(s.seasonPath)), comps_(Components::load(s.componentsPath)) {}
MatchRunner::~MatchRunner() {
    for (auto& l : links_) if (l) l->shutdown();
}

RuntimeLink* MatchRunner::link(int r) { return r < int(links_.size()) ? links_[r].get() : nullptr; }
void MatchRunner::setLiveGamepads(int r, const GamepadState& g1, const GamepadState& g2) {
    if (r >= int(live_.size())) return;
    live_[r] = {g1, g2};
    hasLive_[r] = true;
}

static Pose2 defaultStart(const SeasonDef& s, Alliance a, int k) {
    // legal G304 starts: touching own alliance wall, own side, clear of FLOWERS and LOADING ZONE
    double x = (a == Alliance::Red ? -1 : 1) * (s.half - 0.217);
    double ys[2] = {a == Alliance::Red ? 0.05 : -0.05, a == Alliance::Red ? -1.25 : 1.25};
    return {x, ys[k % 2], a == Alliance::Red ? 0.0 : kPi};
}

bool MatchRunner::begin(std::string& err) {
    world_.reset(new World(season_, comps_, setup_.world));
    int perAlliance[2] = {0, 0};
    for (size_t i = 0; i < setup_.robots.size(); ++i) {
        RobotEntry& re = setup_.robots[i];
        RobotConfig rc = RobotConfig::load(re.robotPath);
        Pose2 st = re.startGiven ? re.start : defaultStart(season_, re.alliance, perAlliance[int(re.alliance)]);
        perAlliance[int(re.alliance)]++;
        std::shared_ptr<AIController> ai;
        if (re.controller == "ai") {
            ai = std::make_shared<AIController>(re.personality, re.difficulty, StrategyDirectives::parse(re.instruction), setup_.world.seed, re.label);
        }
        int idx = world_->addRobot(rc, re.alliance, re.label, st, re.controller == "runtime", ai);
        // experimental overrides from the robot description (flagged by the legality checker)
        for (auto& kv : rc.overrides.members()) {
            std::string e;
            if (!applyParam(*world_, {re.label + "." + kv.first, kv.second.num()}, e)) warnings.push_back("override " + kv.first + ": " + e);
        }
        (void)idx;
    }
    // alliance partner links for AI coordination
    for (size_t i = 0; i < world_->robots().size(); ++i)
        if (auto& ai = world_->robots()[i].ai)
            for (size_t k = 0; k < world_->robots().size(); ++k)
                if (k != i && world_->robots()[k].robot->alliance == world_->robots()[i].robot->alliance) ai->partner = int(k);
    world_->stageField();
    size_t n = world_->robots().size();
    links_.resize(n);
    scripts_.resize(n);
    hasScript_.assign(n, false);
    hasLive_.assign(n, false);
    live_.resize(n);
    if (!setup_.recordDir.empty()) recorder_.reset(new Recorder(setup_.recordDir));
    if (recorder_) {
        world_->onEvent = [this](const char* kind, int robot, const Json& d) {
            recorder_->event(world_->time(), kind, robot, d);
            lastEventT_ = world_->time();
        };
        world_->onFault = [this](const FaultEvent& f) {
            Json d = Json::object();
            d.set("kind", f.kind); d.set("target", f.target); d.set("duration", f.duration); d.set("energy_mJ", f.energyMJ); d.set("cause", f.cause);
            recorder_->event(f.t, "fault", f.robot, d);
            lastEventT_ = f.t;
        };
    }
    // FTC runtimes
    for (size_t i = 0; i < n; ++i) {
        RobotEntry& re = setup_.robots[i];
        if (re.controller != "runtime") continue;
        RuntimeConfig rc = setup_.runtime;
        rc.packageDir = re.packageDir;
        links_[i].reset(new RuntimeLink(*world_, int(i), rc, recorder_.get()));
        if (!links_[i]->start(err)) { err = re.label + ": " + err; return false; }
        if (!re.gamepadScript.empty()) { scripts_[i] = ScriptedGamepad::load(re.gamepadScript); hasScript_[i] = true; }
        if (replay_.count(int(i))) {
            ScriptedGamepad* sg = &replay_[int(i)];
            links_[i]->replayInputs = [sg](double t, GamepadState& a, GamepadState& b) { sg->stateAt(t, a, b); };
        }
        double ms = setup_.preMatchS;
        double autoEnd = ms + season_.autoS, teleopStart = autoEnd + season_.transitionS, end = ms + season_.matchLength();
        if (setup_.world.competitionMode) {
            if (!re.autoOpMode.empty()) {
                sched_.push_back({0.0, int(i), 1, re.autoOpMode, false});
                sched_.push_back({ms, int(i), 2, "", false});
                sched_.push_back({autoEnd, int(i), 3, "", false});   // DS 30 s timer stops AUTO
            }
            if (!re.teleOpMode.empty()) {
                sched_.push_back({autoEnd + 1.0, int(i), 1, re.teleOpMode, false});  // drivers INIT during transition
                sched_.push_back({teleopStart, int(i), 2, "", false});
                sched_.push_back({end, int(i), 3, "", false});
            }
        } else {
            std::string op = !re.teleOpMode.empty() ? re.teleOpMode : re.autoOpMode;
            sched_.push_back({0.0, int(i), 1, op, false});
            sched_.push_back({ms, int(i), 2, "", false});
            sched_.push_back({end, int(i), 3, "", false});
        }
    }
    std::stable_sort(sched_.begin(), sched_.end(), [](const Sched& a, const Sched& b) { return a.t < b.t; });
    if (recorder_) {
        Json h = Json::object();
        h.set("schema", "gameplan.match/1");
        h.set("gameplanVersion", gameplanVersion());
        Json se = Json::object();
        se.set("id", season_.id); se.set("packageVersion", season_.packageVersion); se.set("rulesEngine", season_.rulesEngineVersion);
        se.set("manual", season_.manualTU + " " + season_.manualDate); se.set("sha256", season_.contentHash); se.set("path", setup_.seasonPath);
        h.set("season", se);
        h.set("componentsSha256", comps_.sourceHash);
        h.set("world", setup_.world.toJson());
        h.set("mode", setup_.world.competitionMode ? "competition" : "practice (driver control in AUTO allowed; NOT competition-accurate)");
        Json rs = Json::array();
        for (size_t i = 0; i < n; ++i) {
            const Robot& r = *world_->robots()[i].robot;
            Json o = Json::object();
            o.set("label", r.label); o.set("alliance", toString(r.alliance)); o.set("controller", setup_.robots[i].controller);
            o.set("robotConfig", setup_.robots[i].robotPath); o.set("robotConfigSha256", r.cfg.contentHash); o.set("name", r.cfg.name);
            if (links_[i]) {
                o.set("runtimePackage", setup_.robots[i].packageDir);
                o.set("runtimeReady", links_[i]->ready());
                o.set("autoOpMode", setup_.robots[i].autoOpMode);
                o.set("teleOpMode", setup_.robots[i].teleOpMode);
            }
            if (world_->robots()[i].ai) o.set("ai", world_->robots()[i].ai->describe());
            rs.push(o);
        }
        h.set("robots", rs);
        h.set("sampledParameters", world_->sampler().logJson());
#if defined(_WIN32)
        h.set("platform", "windows");
#elif defined(__APPLE__)
        h.set("platform", "macos");
#else
        h.set("platform", "linux");
#endif
        h.set("numericNote", "Deterministic given identical binaries; cross-platform bitwise equality is not guaranteed (libm transcendental differences). See DEEP_ANALYSIS.md.");
        recorder_->writeHeader(h);
        writeTextFile(setup_.recordDir + "/setup.json", setup_.raw.dump(2));
    }
    started_ = true;
    return true;
}

void MatchRunner::processSchedule() {
    double t = world_->time();
    double ms = setup_.preMatchS;
    if (world_->rules().phase() == Phase::PreMatch && t + 1e-9 >= ms) world_->rules().startMatch(ms);
    for (auto& s : sched_) {
        if (s.done || s.t > t + 1e-9) continue;
        s.done = true;
        RuntimeLink* l = links_[s.robot].get();
        if (!l) continue;
        if (s.action == 1) l->initOpMode(s.name);
        else if (s.action == 2) l->startOpMode();
        else if (s.action == 3) l->stopOpMode();
        if (recorder_) {
            Json d = Json::object();
            d.set("action", s.action == 1 ? "INIT" : s.action == 2 ? "START" : "STOP");
            d.set("opmode", s.name);
            recorder_->event(t, "driver_station", s.robot, d);
        }
    }
    if (branchT_ >= 0 && !branched_ && world_->rules().phase() != Phase::PreMatch && world_->rules().matchTime(t) + 1e-9 >= branchT_) {
        branched_ = true;
        for (auto& c : branch_) {
            std::string err;
            if (!applyParam(*world_, c, err)) warnings.push_back("branch parameter " + c.key + ": " + err);
        }
        if (recorder_) { Json d = Json::object(); d.set("changes", int(branch_.size())); recorder_->event(t, "counterfactual_branch", -1, d); }
    }
}

void MatchRunner::perStep() {
    for (auto& l : links_) if (l) l->applyDueCommands(world_->time());
    if (recorder_) recorder_->sample(*world_, world_->time() - lastEventT_ < 0.5);
    if (recorder_ && world_->time() + 1e-9 >= nextHash_) {
        recorder_->hash(world_->time(), world_->stateHash());
        nextHash_ += 1.0;
    }
}

bool MatchRunner::advance(double dt, std::string& err) {
    if (done_) return false;
    double until = world_->time() + dt;
    while (world_->time() + 1e-9 < until) {
        processSchedule();
        // gamepads for runtime robots
        Phase ph = world_->rules().phase();
        for (size_t i = 0; i < links_.size(); ++i) {
            if (!links_[i]) continue;
            GamepadState g1, g2;
            double mt = world_->rules().matchTime(world_->time());
            if (hasLive_[i]) { g1 = live_[i].first; g2 = live_[i].second; }
            else if (hasScript_[i]) {
                // recorded inputs are stamped in world time; authored scripts use match time
                if (scripts_[i].fromRecordingTimeline) scripts_[i].stateAt(world_->time(), g1, g2);
                else scripts_[i].stateAt(ph == Phase::PreMatch ? -1 : mt, g1, g2);
            }
            bool allowed = world_->rules().driverControlAllowed(world_->time(), setup_.world.competitionMode) ||
                           (ph == Phase::Transition);  // DS still sends gamepads during transition; robot must stay still (G403)
            links_[i]->setGamepads(g1, g2, allowed);
        }
        // next lockstep target
        // absolute 50 ms grid: end-of-match detection is independent of the caller's advance() chunking
        double tNext = std::min(until, (std::floor((world_->time() + setup_.world.dt * 0.5) / 0.05) + 1) * 0.05);
        for (auto& s : sched_) if (!s.done) { tNext = std::min(tNext, std::max(s.t, world_->time() + setup_.world.dt)); break; }
        if (world_->rules().phase() == Phase::PreMatch) tNext = std::min(tNext, std::max(setup_.preMatchS, world_->time() + setup_.world.dt));
        bool anyLink = false;
        for (auto& l : links_) if (l && l->alive()) { anyLink = true; tNext = std::min(tNext, l->target()); }
        if (tNext < world_->time() + setup_.world.dt * 0.5) tNext = world_->time();
        world_->advanceTo(tNext, [this]() { perStep(); });
        processSchedule();
        // service runtimes whose clock the world has reached
        bool exchanged = false;
        for (auto& l : links_) {
            if (!l || !l->alive()) continue;
            if (l->target() <= world_->time() + setup_.world.dt * 0.5) {
                if (!l->exchange(world_->time(), err)) {
                    warnings.push_back("runtime stopped: " + err);
                    if (recorder_) { Json d = Json::object(); d.set("error", err); recorder_->event(world_->time(), "runtime_failure", -1, d); }
                    err.clear();
                    continue;
                }
                exchanged = true;
            }
        }
        if (exchanged && tNext == world_->time()) {
            if (++sameTimeExchanges_ > 200000) { err = "runtime livelock: virtual clock not advancing"; return false; }
        } else sameTimeExchanges_ = 0;
        if (!anyLink && tNext <= world_->time() + 1e-12) world_->step();
        if (world_->rules().isFinal() && world_->time() > world_->rules().matchEnd() + 0.5) { done_ = true; break; }
        if (world_->time() > setup_.preMatchS + season_.matchLength() + 15.0) { done_ = true; break; }
    }
    return !done_;
}

Json MatchRunner::finish() {
    Json extra = Json::object();
    Json tel = Json::array();
    Json rt = Json::array();
    for (size_t i = 0; i < links_.size(); ++i) {
        if (!links_[i]) continue;
        links_[i]->shutdown();
        Json o = Json::object();
        o.set("robot", int(i));
        o.set("deterministic", links_[i]->deterministic());
        o.set("hwStats", links_[i]->hwStats());
        Json st = Json::array();
        for (auto& s : links_[i]->statusLog()) st.push(s);
        o.set("status", st);
        Json lastTel = Json::array();
        const auto& t = links_[i]->telemetry();
        if (!t.empty()) for (auto& l : t.back().lines) lastTel.push(l);
        o.set("lastTelemetry", lastTel);
        rt.push(o);
    }
    extra.set("runtimes", rt);
    Json w = Json::array();
    for (auto& s : warnings) w.push(s);
    extra.set("warnings", w);
    extra.set("finalStateHash", world_->stateHash());
    extra.set("mode", setup_.world.competitionMode ? "competition" : "practice");
    Json launches = Json::array();
    for (auto& l : world_->launches) {
        Json o = Json::object();
        o.set("t", l.t); o.set("robot", l.robot); o.set("speed", l.speed); o.set("elevation", l.elevation);
        o.set("flyBefore", l.flyBefore); o.set("flyAfter", l.flyAfter);
        launches.push(o);
    }
    extra.set("launches", launches);
    if (recorder_) {
        recorder_->hash(world_->time(), world_->stateHash());
        recorder_->finish(*world_, extra);
        return recorder_->summary();
    }
    Json r = world_->rules().resultJson();
    for (auto& kv : extra.members()) r.set(kv.first, kv.second);
    return r;
}

Json MatchRunner::run(std::string& err) {
    if (!begin(err)) return Json();
    while (advance(1.0, err)) {
        if (!err.empty()) return Json();
    }
    if (!err.empty()) return Json();
    return finish();
}

// ------------------------------------------------------------------ counterfactual
Json runCounterfactual(const std::string& dir, double branchT, const std::vector<ParamChange>& changes, const std::string& outDir, std::string& err) {
    Json setupJ = Json::parseFile(dir + "/setup.json");
    std::string base = Json::parseFile(dir + "/header.json")["season"]["path"].str("");
    (void)base;
    // recorded hashes
    std::vector<std::pair<double, std::string>> hashes;
    {
        std::istringstream ss(readTextFile(dir + "/hashes.csv"));
        std::string line;
        std::getline(ss, line);
        while (std::getline(ss, line)) {
            size_t c = line.find(',');
            if (c != std::string::npos) hashes.push_back({std::stod(line.substr(0, c)), line.substr(c + 1)});
        }
    }
    auto runOne = [&](bool branch, const std::string& rec, Json& result, std::string& divergence) -> bool {
        Json sj = setupJ;
        sj.set("record", rec);
        MatchSetup s = MatchSetup::fromJson(sj, GP_SOURCE_ROOT);
        MatchRunner m(s);
        for (size_t i = 0; i < s.robots.size(); ++i) {
            std::string inp = dir + "/inputs_" + std::to_string(i) + ".csv";
            if (s.robots[i].controller == "runtime") {
                std::ifstream f1(inp + ".gz"), f2(inp);
                if (f1 || f2) m.setReplayInputs(int(i), ScriptedGamepad::fromRecording(inp));
            }
        }
        if (!m.begin(err)) return false;
        if (branch) m.schedule(branchT, changes);
        double matchStart = s.preMatchS;
        while (m.advance(0.25, err)) {
            if (!err.empty()) return false;
        }
        result = m.finish();
        // compare the re-execution's hash stream with the recorded one (both written at the same point of each step)
        std::istringstream ss(readTextFile(rec + "/hashes.csv"));
        std::string line;
        std::getline(ss, line);
        size_t k = 0;
        while (std::getline(ss, line) && k < hashes.size()) {
            size_t c = line.find(',');
            double tt = std::stod(line.substr(0, c));
            std::string h = line.substr(c + 1);
            bool beforeBranch = !branch || tt < matchStart + branchT - 1e-6;
            if (beforeBranch && divergence.empty() && (std::fabs(tt - hashes[k].first) > 1e-6 || h != hashes[k].second))
                divergence = "state hash differs at t=" + std::to_string(hashes[k].first);
            ++k;
        }
        for (auto& wv : m.warnings) { (void)wv; }
        return true;
    };
    Json baseRes, branchRes;
    std::string divBase, divBranch;
    makeDirs(outDir);
    if (!runOne(false, outDir + "/baseline", baseRes, divBase)) return Json();
    if (!runOne(true, outDir + "/branch", branchRes, divBranch)) return Json();
    Json orig = Json::parseFile(dir + "/result.json");
    Json out = Json::object();
    out.set("schema", "gameplan.counterfactual/1");
    out.set("source", dir);
    out.set("branchMatchTime", branchT);
    Json ch = Json::array();
    for (auto& c : changes) { Json o = Json::object(); o.set("key", c.key); o.set("value", c.value); ch.push(o); }
    out.set("changes", ch);
    bool exactBaseline = divBase.empty() && orig["finalStateHash"].str("") == baseRes["finalStateHash"].str("x");
    bool exactPrefix = divBranch.empty();
    out.set("reproduction", exactBaseline ? "EXACT: re-execution reproduced every recorded state hash and the final state"
                                          : "APPROXIMATE: " + (divBase.empty() ? std::string("final state differs") : divBase));
    out.set("branchPrefix", exactPrefix ? "EXACT up to the branch point" : "APPROXIMATE: " + divBranch);
    auto summary = [](const Json& r) {
        Json o = Json::object();
        o.set("red", r["red"]["total"]); o.set("blue", r["blue"]["total"]);
        o.set("redTips", r["red"]["tips"]); o.set("blueTips", r["blue"]["tips"]);
        o.set("violations", double(r["violations"].size()));
        Json robots = Json::array();
        for (auto& rb : r["robots"].items()) {
            Json x = Json::object();
            x.set("label", rb["label"]); x.set("distance_m", rb["distance_m"]); x.set("energy_J", rb["energy_J"]);
            x.set("peakCurrent_A", rb["peakCurrent_A"]); x.set("finalSoc", rb["finalSoc"]);
            robots.push(x);
        }
        o.set("robots", robots);
        o.set("launches", double(r["launches"].size()));
        return o;
    };
    out.set("original", summary(orig));
    out.set("baselineReexecution", summary(baseRes));
    out.set("counterfactual", summary(branchRes));
    Json d = Json::object();
    d.set("redScore", branchRes["red"]["total"].num() - baseRes["red"]["total"].num());
    d.set("blueScore", branchRes["blue"]["total"].num() - baseRes["blue"]["total"].num());
    d.set("launches", double(branchRes["launches"].size()) - double(baseRes["launches"].size()));
    out.set("delta", d);
    out.set("method", "numerical re-simulation with identical seed and per-subsystem random streams (common random numbers); no AI/LLM involvement");
    writeTextFile(outDir + "/counterfactual.json", out.dump(2));
    return out;
}

}  // namespace gp
