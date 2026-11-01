#include "gameplan/experiment.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <functional>
#include <map>

#include "gameplan/match.hpp"

namespace gp {

ExperimentSpec ExperimentSpec::load(const std::string& path) {
    ExperimentSpec s;
    Json j = Json::parseFile(path);
    std::string base = path.find_last_of("/\\") == std::string::npos ? "." : path.substr(0, path.find_last_of("/\\"));
    s.raw = j;
    s.name = j["name"].str("experiment");
    s.matchPath = resolvePath(j["match"].str(), base);
    s.mode = j["mode"].str("stochastic");
    s.trials = int(j["trials"].num(20));
    s.baseSeed = (unsigned long long)j["baseSeed"].num(1);
    s.threads = int(j["threads"].num(0));
    s.record = j["record"].boolean(false);
    s.objective = j["objective"].str("red.total");
    s.outDir = j["out"].str("out/experiments/" + s.name);
    s.maxWallS = j["limits"]["maxWall_s"].num(3600);
    s.maxStorageMB = j["limits"]["maxStorage_MB"].num(2000);
    s.maxTrials = int(j["limits"]["maxTrials"].num(5000));
    s.variants = j["variants"];
    s.adversarial = j["adversarial"];
    return s;
}

Json ResourceEstimate::toJson() const {
    Json j = Json::object();
    j.set("trials", trials); j.set("threads", threads); j.set("perTrialWall_s", perTrialWallS); j.set("totalWall_s", totalWallS);
    j.set("cpuCoreSeconds", cpuCoreSeconds); j.set("memory_MB", memoryMB); j.set("storage_MB", storageMB); j.set("gpu", gpu);
    j.set("externalApiCost_USD", externalApiCost); j.set("withinLimits", withinLimits); j.set("limitReason", limitReason);
    j.set("compute", "local only");
    return j;
}

namespace {
// JSON patch: keys are dotted paths, array indices allowed ("robots.1.ai.personality")
void patch(Json& doc, const std::string& path, const Json& value) {
    // "a.b.c" walks objects/arrays; "a.b:x.y.z" uses the literal key "x.y.z" as the last step
    std::vector<std::string> parts;
    std::string head = path, literal;
    size_t colon = path.find(':');
    if (colon != std::string::npos) { head = path.substr(0, colon); literal = path.substr(colon + 1); }
    std::stringstream ss(head);
    std::string p;
    while (std::getline(ss, p, '.')) parts.push_back(p);
    if (!literal.empty()) parts.push_back(literal);
    std::function<void(Json&, size_t)> rec = [&](Json& node, size_t i) {
        if (i == parts.size() - 1) {
            if (node.isArray()) node[size_t(std::stoul(parts[i]))] = value;
            else node.set(parts[i], value);
            return;
        }
        if (node.isArray()) { rec(node[size_t(std::stoul(parts[i]))], i + 1); return; }
        if (!node.has(parts[i])) node.set(parts[i], Json::object());
        Json copy = node[parts[i]];
        rec(copy, i + 1);
        node.set(parts[i], copy);
    };
    rec(doc, 0);
}
double objectiveValue(const Json& r, const std::string& obj) {
    if (obj == "margin.red") return r["red"]["total"].num() - r["blue"]["total"].num();
    if (obj == "margin.blue") return r["blue"]["total"].num() - r["red"]["total"].num();
    size_t d = obj.find('.');
    return r[obj.substr(0, d)][obj.substr(d + 1)].num(0);
}
double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    double k = p * (v.size() - 1);
    size_t lo = size_t(k), hi = std::min(v.size() - 1, lo + 1);
    return v[lo] + (v[hi] - v[lo]) * (k - lo);
}
int autoThreads(int req) {
    unsigned hw = std::thread::hardware_concurrency();
    int n = req > 0 ? req : std::max(1, int(hw) - 1);
    return n;
}
}  // namespace

ResourceEstimate estimateResources(const ExperimentSpec& s) {
    ResourceEstimate e;
    Json mj = Json::parseFile(s.matchPath);
    std::string base = s.matchPath.substr(0, s.matchPath.find_last_of("/\\"));
    // calibration: simulate the first 6 s of the actual match setup and scale
    mj.set("record", "");
    MatchSetup ms = MatchSetup::fromJson(mj, base);
    int runtimes = 0;
    for (auto& r : ms.robots) if (r.controller == "runtime") ++runtimes;
    auto t0 = std::chrono::steady_clock::now();
    double simulated = 0;
    {
        MatchRunner m(ms);
        std::string err;
        if (m.begin(err)) { m.advance(6.0, err); simulated = m.world().time(); }
    }
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    double matchLen = ms.preMatchS + 158.0 + 3.0;
    e.perTrialWallS = simulated > 0 ? wall / simulated * matchLen : 30.0;
    int variants = std::max(1, int(s.variants.size()));
    e.trials = s.mode == "adversarial" ? int(s.adversarial["iterations"].num(30)) * std::max(1, int(s.adversarial["repeats"].num(1)))
                                       : s.trials * variants;
    e.threads = std::min(autoThreads(s.threads), std::max(1, e.trials));
    e.cpuCoreSeconds = e.perTrialWallS * e.trials;
    e.totalWallS = e.cpuCoreSeconds / e.threads;
    e.memoryMB = e.threads * (40.0 + 180.0 * runtimes);
    e.storageMB = s.record ? e.trials * 9.0 : e.trials * 0.01;
    e.externalApiCost = 0.0;
    if (e.trials > s.maxTrials) { e.withinLimits = false; e.limitReason = "trials > maxTrials"; }
    if (e.totalWallS > s.maxWallS) { e.withinLimits = false; e.limitReason += (e.limitReason.empty() ? "" : "; ") + std::string("estimated wall time > maxWall_s"); }
    if (e.storageMB > s.maxStorageMB) { e.withinLimits = false; e.limitReason += (e.limitReason.empty() ? "" : "; ") + std::string("estimated storage > maxStorage_MB"); }
    return e;
}

Json runExperiment(const ExperimentSpec& s, const std::string& outDir) {
    makeDirs(outDir);
    Json base = Json::parseFile(s.matchPath);
    std::string baseDir = s.matchPath.substr(0, s.matchPath.find_last_of("/\\"));
    struct Trial { int idx; std::string variant; unsigned long long seed; Json patch; };
    std::vector<Trial> trials;
    Json variants = s.variants.isArray() && s.variants.size() ? s.variants : [] { Json a = Json::array(); Json v = Json::object(); v.set("name", "base"); a.push(v); return a; }();
    std::vector<Json> advParams;
    if (s.mode == "adversarial") {
        for (auto& p : s.adversarial["params"].items()) advParams.push_back(p);
    } else {
        int k = 0;
        for (auto& v : variants.items())
            for (int i = 0; i < s.trials; ++i) {
                unsigned long long seed = s.mode == "deterministic" ? s.baseSeed : s.baseSeed + i;
                trials.push_back({k++, v["name"].str("base"), seed, v["patch"]});
            }
    }
    std::mutex mu;
    std::vector<Json> results;
    std::atomic<int> next{0}, done{0};
    std::atomic<bool> cancel{false};
    auto t0 = std::chrono::steady_clock::now();
    auto progress = [&](int total) {
        Json p = Json::object();
        p.set("done", done.load()); p.set("total", total);
        p.set("elapsed_s", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        p.set("state", cancel ? "cancelled" : (done.load() == total ? "finished" : "running"));
        writeTextFile(outDir + "/progress.json", p.dump(2));
    };
    auto controlState = [&]() {
        std::ifstream f(outDir + "/control");
        std::string c;
        f >> c;
        return c;
    };
    auto runTrial = [&](const Trial& tr, const std::vector<ParamChange>& setupOverrides) -> Json {
        Json mj = base;
        mj.set("seed", double(tr.seed));
        if (tr.patch.isObject()) for (auto& kv : tr.patch.members()) patch(mj, kv.first, kv.second);
        for (auto& o : setupOverrides) patch(mj, o.key, Json(o.value));
        mj.set("record", s.record ? outDir + "/matches/" + std::to_string(tr.idx) : std::string());
        MatchSetup ms = MatchSetup::fromJson(mj, baseDir);
        MatchRunner m(ms);
        std::string err;
        Json r = m.run(err);
        Json row = Json::object();
        row.set("trial", tr.idx); row.set("variant", tr.variant); row.set("seed", double(tr.seed));
        if (r.isNull()) { row.set("error", err); return row; }
        row.set("red", r["red"]["total"]); row.set("blue", r["blue"]["total"]); row.set("redTips", r["red"]["tips"]); row.set("blueTips", r["blue"]["tips"]);
        row.set("violations", double(r["violations"].size())); row.set("faults", double(r["faults"].size()));
        row.set("objective", objectiveValue(r, s.objective));
        row.set("finalStateHash", r["finalStateHash"]);
        Json sp = Json::object();
        for (auto& p : m.world().sampler().log()) if (p.value != p.nominal || p.overridden) sp.set(p.name, p.value);
        row.set("sampled", sp);
        return row;
    };
    int nthreads = autoThreads(s.threads);
    if (s.mode != "adversarial") {
        int total = int(trials.size());
        std::vector<std::thread> pool;
        for (int k = 0; k < std::min(nthreads, total); ++k)
            pool.emplace_back([&]() {
                for (;;) {
                    std::string c = controlState();
                    while (c == "pause" && !cancel) { std::this_thread::sleep_for(std::chrono::milliseconds(200)); c = controlState(); }
                    if (c == "cancel") cancel = true;
                    if (cancel) return;
                    int i = next++;
                    if (i >= total) return;
                    Json row = runTrial(trials[i], {});
                    std::lock_guard<std::mutex> g(mu);
                    results.push_back(row);
                    ++done;
                    progress(total);
                }
            });
        for (auto& th : pool) th.join();
        progress(total);
    } else {
        // adversarial search: random exploration then coordinate refinement toward the worst objective
        int iters = int(s.adversarial["iterations"].num(30));
        bool minimize = s.adversarial["minimize"].boolean(true);
        Rng r(s.baseSeed ^ 0xA5A5);
        std::vector<double> best(advParams.size());
        double bestObj = minimize ? 1e18 : -1e18;
        for (int it = 0; it < iters && !cancel; ++it) {
            if (controlState() == "cancel") { cancel = true; break; }
            std::vector<ParamChange> ov;
            std::vector<double> x(advParams.size());
            for (size_t k = 0; k < advParams.size(); ++k) {
                double lo = advParams[k]["min"].num(), hi = advParams[k]["max"].num();
                if (it < iters / 2 || bestObj == 1e18 || bestObj == -1e18) x[k] = r.uniform(lo, hi);
                else x[k] = clampd(best[k] + r.normal(0, 0.15 * (hi - lo)), lo, hi);
                ov.push_back({advParams[k]["key"].str(), x[k]});
            }
            Trial tr{it, "adversarial", s.baseSeed, Json()};
            Json row = runTrial(tr, ov);
            Json px = Json::object();
            for (size_t k = 0; k < x.size(); ++k) px.set(advParams[k]["key"].str(), x[k]);
            row.set("params", px);
            double o = row["objective"].num(0);
            if ((minimize && o < bestObj) || (!minimize && o > bestObj)) { bestObj = o; best = x; }
            results.push_back(row);
            ++done;
            progress(iters);
        }
    }
    std::sort(results.begin(), results.end(), [](const Json& a, const Json& b) { return a["trial"].num() < b["trial"].num(); });
    // aggregate
    Json summary = Json::object();
    summary.set("mode", s.mode);
    summary.set("objective", s.objective);
    summary.set("trialsCompleted", int(results.size()));
    summary.set("cancelled", cancel.load());
    std::map<std::string, std::vector<const Json*>> byVar;
    for (auto& r : results) byVar[r["variant"].str()].push_back(&r);
    Json vs = Json::array();
    for (auto& kv : byVar) {
        std::vector<double> obj, margin;
        for (auto* r : kv.second) if (!r->has("error")) { obj.push_back((*r)["objective"].num()); margin.push_back((*r)["red"].num() - (*r)["blue"].num()); }
        Json o = Json::object();
        o.set("variant", kv.first);
        o.set("n", int(obj.size()));
        double mean = 0; for (double v : obj) mean += v; mean = obj.empty() ? 0 : mean / obj.size();
        double var = 0; for (double v : obj) var += (v - mean) * (v - mean); var = obj.size() > 1 ? var / (obj.size() - 1) : 0;
        o.set("mean", mean); o.set("std", std::sqrt(var));
        for (double p : {0.01, 0.05, 0.25, 0.5, 0.75, 0.95, 0.99}) o.set("p" + std::to_string(int(p * 100 + 0.5)), pct(obj, p));
        std::vector<double> sorted = obj;
        std::sort(sorted.begin(), sorted.end());
        size_t tail = std::max<size_t>(1, sorted.size() / 20);
        double worst = 0; for (size_t i = 0; i < tail && i < sorted.size(); ++i) worst += sorted[i];
        o.set("worst5pctMean", sorted.empty() ? 0 : worst / std::min(tail, sorted.size()));
        int wins = 0; for (double m : margin) if (m > 0) ++wins;
        o.set("redWinRate", margin.empty() ? 0 : double(wins) / margin.size());
        // correlations between sampled hidden parameters and the objective
        std::map<std::string, std::vector<std::pair<double, double>>> pairs;
        for (auto* r : kv.second) for (auto& p : (*r)["sampled"].members()) pairs[p.first].push_back({p.second.num(), (*r)["objective"].num()});
        std::vector<std::pair<double, std::string>> corr;
        for (auto& pp : pairs) {
            if (pp.second.size() < 5) continue;
            double mx = 0, my = 0;
            for (auto& q : pp.second) { mx += q.first; my += q.second; }
            mx /= pp.second.size(); my /= pp.second.size();
            double sxy = 0, sxx = 0, syy = 0;
            for (auto& q : pp.second) { sxy += (q.first - mx) * (q.second - my); sxx += (q.first - mx) * (q.first - mx); syy += (q.second - my) * (q.second - my); }
            if (sxx > 0 && syy > 0) corr.push_back({sxy / std::sqrt(sxx * syy), pp.first});
        }
        std::sort(corr.begin(), corr.end(), [](auto& a, auto& b) { return std::fabs(a.first) > std::fabs(b.first); });
        Json cj = Json::array();
        for (size_t i = 0; i < corr.size() && i < 10; ++i) { Json c = Json::object(); c.set("parameter", corr[i].second); c.set("pearson", corr[i].first); cj.push(c); }
        o.set("topCorrelations", cj);
        if (s.mode == "deterministic") {
            std::string h;
            bool same = true;
            for (auto* r : kv.second) { if (h.empty()) h = (*r)["finalStateHash"].str(""); else if ((*r)["finalStateHash"].str("") != h) same = false; }
            o.set("determinism", same ? "all repeats produced the identical final state hash" : "REPEATS DIVERGED (nondeterminism detected)");
        }
        vs.push(o);
    }
    summary.set("variants", vs);
    Json out = Json::object();
    out.set("summary", summary);
    Json rows = Json::array();
    for (auto& r : results) rows.push(r);
    out.set("trials", rows);
    writeTextFile(outDir + "/experiment.json", out.dump(2));
    // CSV export
    std::ostringstream csv;
    csv << "trial,variant,seed,red,blue,redTips,blueTips,violations,faults,objective\n";
    for (auto& r : results)
        csv << r["trial"].integer() << "," << r["variant"].str("") << "," << (unsigned long long)r["seed"].num() << "," << r["red"].num(0) << "," << r["blue"].num(0) << ","
            << r["redTips"].num(0) << "," << r["blueTips"].num(0) << "," << r["violations"].num(0) << "," << r["faults"].num(0) << "," << r["objective"].num(0) << "\n";
    writeTextFile(outDir + "/trials.csv", csv.str());
    return out;
}

// ------------------------------------------------------------------ stress tests
Json runStressTests(const std::string& robotPath, const std::string& compPath, const std::string& outDir) {
    makeDirs(outDir);
    Components comps = Components::load(compPath);
    RobotConfig rc = RobotConfig::load(robotPath);
    struct Cond { std::string name; double soc; double cycles; double mainScale; };
    std::vector<Cond> conds = {{"fresh battery (95% SoC)", 0.95, 20, 1.0}, {"half charge (50%)", 0.50, 20, 1.0},
                               {"low charge (25%)", 0.25, 20, 1.0}, {"aged pack (300 cycles)", 0.95, 300, 1.0},
                               {"poor main wiring (3x resistance)", 0.95, 20, 3.0}};
    auto make = [&](const Cond& c) {
        std::unique_ptr<Robot> r(new Robot());
        r->cfg = rc;
        UncertaintyConfig uc;
        uc.setAll(ULevel::Nominal);
        uc.setOverride("s.battery.soc", c.soc);
        UncertaintySampler us(7, uc);
        r->build(comps, us, "s.");
        r->elec.battery.st.cycles = c.cycles;
        r->elec.mainPath.scale *= c.mainScale;
        return r;
    };
    std::vector<int> drive;
    {
        auto r = make(conds[0]);
        for (auto& w : r->wheels) drive.push_back(r->motorIndex(w.name));
    }
    struct Scenario { std::string name; double dur; std::function<void(Robot&, double)> act; };
    auto mechMotor = [&](Robot& r, MechType t) { for (auto& m : r.mechs) if (m.type == t && m.joint >= 0) return r.joints[m.joint].motor; return -1; };
    std::vector<Scenario> sc = {
        {"all drivetrain motors full from rest", 1.5, [&](Robot& r, double) { for (int m : drive) r.ports[m].power = r.motorSign[m]; }},
        {"accelerate while running intake", 1.5, [&](Robot& r, double) { for (int m : drive) r.ports[m].power = 1; int i = mechMotor(r, MechType::Intake); if (i >= 0) r.ports[i].power = 1; }},
        {"accelerate + intake stalled + flywheel spin-up", 1.5, [&](Robot& r, double) {
             for (int m : drive) r.ports[m].power = 1;
             int i = mechMotor(r, MechType::Intake); if (i >= 0) { r.ports[i].power = 1; r.joints[r.motorJoint[i]].loadTorque = 50.0; r.joints[r.motorJoint[i]].loadDamping = 50.0; }
             int f = mechMotor(r, MechType::Flywheel); if (f >= 0) r.ports[f].power = 1; }},
        {"all servos move simultaneously under load", 1.0, [&](Robot& r, double t) {
             for (auto& s : r.elec.servos) { s.pwmEnabled = true; s.pulseUs = t < 0.5 ? 2300 : 700; s.gravityNm = std::max(s.gravityNm, 0.6); } }},
        {"pushing against a wall (stalled drivetrain 50%) for 60 s", 60.0, [&](Robot& r, double) {
             for (int m : drive) { r.ports[m].power = 0.5; r.joints[r.motorJoint[m]].loadTorque = 0; } r.vel = {0, 0}; r.pos = {0, 0}; }},
    };
    Json out = Json::object();
    out.set("robot", rc.name);
    out.set("method", "single-robot coupled electrical/mechanical simulation, nominal uncertainty, dt=0.5 ms");
    Json rows = Json::array();
    std::vector<std::string> recs;
    for (auto& s : sc) {
        for (auto& c : conds) {
            if (s.dur > 10 && c.name != conds[0].name) continue;
            auto r = make(c);
            double dt = 0.0005, minBus = 99, peakI = 0, maxTw = 0, t90 = -1, maxRail = 0;
            double vFree = 0;
            int brown = 0;
            for (double t = 0; t < s.dur; t += dt) {
                s.act(*r, t);
                r->step(dt, t, 1.0);
                if (s.dur > 10) { r->vel = {0, 0}; r->omega = 0; for (int m : drive) { auto& j = r->joints[r->motorJoint[m]]; j.w = j.wm = 0; } }
                for (auto& h : r->elec.hubs) { minBus = std::min(minBus, h.busV); maxRail = std::max(maxRail, h.servoRailCurrent); }
                peakI = std::max(peakI, r->elec.battery.st.current);
                for (auto& m : r->elec.motors) maxTw = std::max(maxTw, m.tWinding);
                vFree = std::max(vFree, r->vel.norm());
            }
            for (auto& h : r->elec.hubs) brown += h.brownouts;
            Json o = Json::object();
            o.set("scenario", s.name); o.set("condition", c.name); o.set("minBusV", minBus); o.set("peakBatteryA", peakI);
            o.set("maxWinding_C", maxTw); o.set("finalSpeed_mps", r->vel.norm()); o.set("brownouts", brown); o.set("maxServoRailA", maxRail);
            int limited = 0;
            for (auto& e : r->elec.events) if (e.kind == "current_limit") ++limited;
            o.set("currentLimitEvents", limited);
            o.set("servoFoldbacks", [&] { int n = 0; for (auto& e : r->elec.events) if (e.kind == "servo_foldback") ++n; return n; }());
            rows.push(o);
            double margin = minBus - r->elec.hubs[0].spec.brownoutV;
            if (brown > 0) recs.push_back(s.name + " / " + c.name + ": hub brownout occurred (bus fell below " + std::to_string(r->elec.hubs[0].spec.brownoutV) + " V). Reduce simultaneous load or improve battery/wiring.");
            else if (margin < 1.0) recs.push_back(s.name + " / " + c.name + ": only " + std::to_string(margin).substr(0, 4) + " V of margin above the brownout threshold.");
            if (maxTw > 100) recs.push_back(s.name + " / " + c.name + ": winding reached " + std::to_string(int(maxTw)) + " C; sustained stalls risk thermal damage.");
            (void)t90;
        }
    }
    out.set("results", rows);
    Json rj = Json::array();
    for (auto& r : recs) rj.push(r);
    if (recs.empty()) rj.push("No brownouts, thermal or margin issues found in the tested scenarios.");
    out.set("recommendations", rj);
    out.set("recommendationBasis", "thresholds: hub brownout voltage from the component library; 1.0 V margin rule; 100 C winding warning (estimated thermal model)");
    writeTextFile(outDir + "/stress.json", out.dump(2));
    return out;
}

}  // namespace gp
