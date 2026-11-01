// gp-sim: headless GamePlan simulator CLI. Uses exactly the same World/rules/physics as the
// Unreal application; there is no separate batch physics.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "gameplan/analysis.hpp"
#include "gameplan/cad.hpp"
#include "gameplan/experiment.hpp"
#include "gameplan/legality.hpp"
#include "gameplan/match.hpp"

using namespace gp;

static void usage() {
    std::puts(
        "gp-sim <command> [options]\n"
        "  run <match.json> [--record DIR] [--seed N]       play a full match headless (faster than real time)\n"
        "  verify <recordDir>                              re-execute a recorded match and check every state hash\n"
        "  counterfactual <recordDir> --at <matchTime> --set key=value [--set ...] [--out DIR]\n"
        "  report <recordDir> [--out report.html]          post-match engineering report (HTML)\n"
        "  experiment <experiment.json> [--out DIR]        batch: deterministic | stochastic | adversarial\n"
        "  estimate <experiment.json>                      resource estimate only (no simulation started)\n"
        "  stress <robot.json> [--out DIR]                 automated electrical stress tests\n"
        "  legality <robot.json>                           FTC legality check (BIOBUZZ R1xx/R5xx/R6xx/R7xx)\n"
        "  season [season.json]                            print the season package summary\n"
        "  cad-props <mesh.stl|obj> [--density kg/m3]      mass properties from a closed mesh\n"
        "  step-tree <file.step>                           assembly tree + placements from a STEP file\n");
}

static std::map<std::string, std::vector<std::string>> parseOpts(int argc, char** argv, int from, std::vector<std::string>& pos) {
    std::map<std::string, std::vector<std::string>> o;
    for (int i = from; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--", 0) == 0) {
            std::string v = (i + 1 < argc && std::strncmp(argv[i + 1], "--", 2) != 0) ? argv[++i] : "1";
            o[a.substr(2)].push_back(v);
        } else pos.push_back(a);
    }
    return o;
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }
    std::string cmd = argv[1];
    std::vector<std::string> pos;
    auto opt = parseOpts(argc, argv, 2, pos);
    auto get = [&](const char* k, const std::string& d) { return opt.count(k) ? opt[k].back() : d; };
    try {
        if (cmd == "run") {
            if (pos.empty()) { usage(); return 2; }
            MatchSetup s = MatchSetup::load(pos[0]);
            if (opt.count("record")) s.recordDir = get("record", "");
            if (opt.count("seed")) { s.world.seed = std::stoull(get("seed", "1")); s.raw.set("seed", double(s.world.seed)); }
            if (opt.count("record")) s.raw.set("record", s.recordDir);
            auto t0 = std::chrono::steady_clock::now();
            MatchRunner m(s);
            std::string err;
            Json r = m.run(err);
            double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (!err.empty() || r.isNull()) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
            std::printf("RED %d  -  BLUE %d   (tips %d/%d, violations %zu)  simulated %.1f s in %.1f s wall (%.1fx)\n",
                        r["red"]["total"].integer(), r["blue"]["total"].integer(), r["red"]["tips"].integer(), r["blue"]["tips"].integer(),
                        r["violations"].size(), m.world().time(), wall, m.world().time() / std::max(wall, 1e-6));
            for (auto& w : m.warnings) std::printf("warning: %s\n", w.c_str());
            if (!s.recordDir.empty()) std::printf("recorded: %s\n", s.recordDir.c_str());
            if (opt.count("json")) std::puts(r.dump(2).c_str());
            return 0;
        }
        if (cmd == "verify") {
            if (pos.empty()) { usage(); return 2; }
            std::string err;
            Json r = runCounterfactual(pos[0], 1e9, {}, get("out", pos[0] + "/verify"), err);
            if (r.isNull()) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
            std::printf("%s\n", r["reproduction"].str().c_str());
            return r["reproduction"].str().rfind("EXACT", 0) == 0 ? 0 : 3;
        }
        if (cmd == "counterfactual") {
            if (pos.empty() || !opt.count("at")) { usage(); return 2; }
            std::vector<ParamChange> ch;
            for (auto& kv : opt["set"]) {
                size_t e = kv.find('=');
                if (e == std::string::npos) { std::fprintf(stderr, "bad --set %s\n", kv.c_str()); return 2; }
                ch.push_back({kv.substr(0, e), std::stod(kv.substr(e + 1))});
            }
            std::string err;
            Json r = runCounterfactual(pos[0], std::stod(get("at", "0")), ch, get("out", pos[0] + "/counterfactual"), err);
            if (r.isNull()) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
            std::puts(r.dump(2).c_str());
            return 0;
        }
        if (cmd == "report") {
            if (pos.empty()) { usage(); return 2; }
            std::string out = get("out", pos[0] + "/report.html");
            writeMatchReport(pos[0], out);
            std::printf("report: %s\n", out.c_str());
            return 0;
        }
        if (cmd == "experiment" || cmd == "estimate") {
            if (pos.empty()) { usage(); return 2; }
            ExperimentSpec spec = ExperimentSpec::load(pos[0]);
            ResourceEstimate est = estimateResources(spec);
            std::puts(est.toJson().dump(2).c_str());
            if (cmd == "estimate") return 0;
            if (!est.withinLimits && !opt.count("force")) {
                std::fprintf(stderr, "estimate exceeds configured limits (%s); pass --force to run anyway\n", est.limitReason.c_str());
                return 4;
            }
            Json r = runExperiment(spec, get("out", spec.outDir));
            std::puts(r["summary"].dump(2).c_str());
            return 0;
        }
        if (cmd == "stress") {
            if (pos.empty()) { usage(); return 2; }
            Json r = runStressTests(pos[0], resolvePath("data/hardware/components.json", "."), get("out", "out/stress"));
            std::puts(r.dump(2).c_str());
            return 0;
        }
        if (cmd == "legality") {
            if (pos.empty()) { usage(); return 2; }
            SeasonDef s = SeasonDef::load(resolvePath(get("season", "seasons/biobuzz-2027/season.json"), "."));
            Components c = Components::load(resolvePath("data/hardware/components.json", "."));
            Json r = checkLegality(RobotConfig::load(pos[0]), s, c).toJson();
            std::puts(r.dump(2).c_str());
            return r["status"].str() == "competition-legal" ? 0 : 5;
        }
        if (cmd == "season") {
            SeasonDef s = SeasonDef::load(resolvePath(pos.empty() ? "seasons/biobuzz-2027/season.json" : pos[0], "."));
            std::printf("%s (%s) package %s, rules %s, manual %s %s\nmatch: AUTO %.0f s, transition %.0f s, TELEOP %.0f s\nsha256 %s\n",
                        s.name.c_str(), s.id.c_str(), s.packageVersion.c_str(), s.rulesEngineVersion.c_str(), s.manualTU.c_str(),
                        s.manualDate.c_str(), s.autoS, s.transitionS, s.teleopS, s.contentHash.c_str());
            return 0;
        }
        if (cmd == "cad-props") {
            if (pos.empty()) { usage(); return 2; }
            MeshProps p = meshMassProperties(loadMesh(pos[0]), std::stod(get("density", "1200")));
            std::puts(p.toJson().dump(2).c_str());
            return 0;
        }
        if (cmd == "step-tree") {
            if (pos.empty()) { usage(); return 2; }
            std::puts(readStepAssembly(pos[0]).toJson().dump(2).c_str());
            return 0;
        }
    } catch (std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    usage();
    return 2;
}
