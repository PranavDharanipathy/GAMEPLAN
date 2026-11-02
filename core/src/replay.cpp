#include "gameplan/replay.hpp"

#include <zlib.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include "gameplan/match.hpp"
#include "gameplan/view.hpp"

namespace gp {
namespace {

bool littleEndianHost() {
    uint16_t x = 1;
    unsigned char c = 0;
    std::memcpy(&c, &x, 1);
    return c == 1;
}

// events worth marking on a replay timeline (contacts are too frequent to be useful there)
bool keepEvent(const std::string& kind) {
    return kind == "launch" || kind == "cell_entry" || kind == "tip_start" || kind == "flower_entry" || kind == "flower_retrieve" ||
           kind == "robot_collision" || kind == "intake" || kind == "fault" || kind == "runtime_failure";
}

bool fileExists(const std::string& p) { std::ifstream f(p); return bool(f); }

}  // namespace

bool exportReplay(const std::string& dir, const std::string& outPath, double hz, ReplayInfo& info, std::string& err) {
    if (!littleEndianHost()) { err = "replay export needs a little-endian host"; return false; }
    // the runner advances in 50 ms chunks, as verify does: chunks off that grid can change where the
    // match loop stops (see MatchRunner::advance), so sampling rates are divisors of 20 Hz
    if (!(hz == 5 || hz == 10 || hz == 20)) { err = "--hz must be 5, 10 or 20"; return false; }
    const int every = int(20 / hz);
    if (!fileExists(dir + "/setup.json") || !fileExists(dir + "/result.json")) {
        err = dir + " is not a GamePlan recording (setup.json / result.json missing; record with gp-sim run --record DIR)";
        return false;
    }
    Json setupJ = Json::parseFile(dir + "/setup.json");
    Json orig = Json::parseFile(dir + "/result.json");
    Json header = fileExists(dir + "/header.json") ? Json::parseFile(dir + "/header.json") : Json::object();

    // re-execute without writing a second recording; recorded gamepad inputs drive runtime robots
    Json sj = setupJ;
    sj.set("record", "");
    MatchSetup s = MatchSetup::fromJson(sj, GP_SOURCE_ROOT);
    MatchRunner m(s);
    for (size_t i = 0; i < s.robots.size(); ++i) {
        std::string inp = dir + "/inputs_" + std::to_string(i) + ".csv";
        if (s.robots[i].controller == "runtime" && (fileExists(inp + ".gz") || fileExists(inp)))
            m.setReplayInputs(int(i), ScriptedGamepad::fromRecording(inp));
    }
    if (!m.begin(err)) return false;
    std::vector<float> frames;
    std::vector<int> status(m.world().robots().size(), 0);
    const size_t frameLen = viewFrameLength(m.world());
    frames.reserve(frameLen * size_t(hz * 170));
    appendViewFrame(m.world(), status, frames);
    for (int k = 1;; ++k) {
        bool more = m.advance(0.05, err);
        if (!err.empty()) return false;
        if (k % every == 0 || !more) appendViewFrame(m.world(), status, frames);
        if (frames.size() % frameLen != 0) { err = "view frame length changed during the match"; return false; }
        if (!more) break;
    }
    Json res = m.finish();
    const World& w = m.world();

    Json h = Json::object();
    h.set("schema", "gameplan.replay/1");
    h.set("gameplanVersion", gameplanVersion());
    Json season = Json::object();
    const Json& hs = header["season"];
    season.set("id", hs["id"].str(w.season().id));
    season.set("packageVersion", hs["packageVersion"].str(""));
    season.set("rulesEngine", hs["rulesEngine"].str(""));
    season.set("manual", hs["manual"].str(""));
    h.set("season", season);
    h.set("seed", double(s.world.seed));
    h.set("mode", s.world.competitionMode ? "competition" : "practice");
    h.set("matchStart", s.preMatchS);
    Json timing = Json::object();
    timing.set("auto_s", w.season().autoS); timing.set("transition_s", w.season().transitionS); timing.set("teleop_s", w.season().teleopS);
    h.set("timing", timing);
    h.set("hz", hz);
    h.set("frameLength", double(frameLen));
    h.set("frames", double(frames.size() / frameLen));
    Json robots = Json::array();
    for (size_t i = 0; i < w.robots().size(); ++i) {
        Json r = robotViewInfo(w, int(i));
        r.set("controller", i < s.robots.size() ? s.robots[i].controller : std::string("ai"));
        robots.push(r);
    }
    h.set("robots", robots);
    Json events = Json::array();
    {
        std::ifstream f(dir + "/events.jsonl");
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            Json e = Json::parse(line);
            if (!keepEvent(e["kind"].str(""))) continue;
            Json o = Json::object();
            o.set("t", e["t"]); o.set("kind", e["kind"]); o.set("robot", e["robot"]);
            const Json& d = e["detail"];
            if (d.has("hive")) o.set("hive", d["hive"]);
            if (d.has("other")) o.set("other", d["other"]);
            if (d.has("speed")) o.set("speed", d["speed"]);
            if (d.has("type")) o.set("type", d["type"]);
            events.push(o);
        }
    }
    h.set("events", events);
    h.set("violations", orig["violations"]);
    Json result = Json::object();
    for (const char* a : {"red", "blue"}) {
        Json o = Json::object();
        o.set("total", res[a]["total"]); o.set("rp", res[a]["rp"]); o.set("foulCredit", res[a]["foulCredit"]); o.set("breakdown", res[a]["breakdown"]);
        result.set(a, o);
    }
    h.set("result", result);
    bool exact = orig["finalStateHash"].str("") != "" && orig["finalStateHash"].str("") == res["finalStateHash"].str("x");
    info.reproduction = exact ? "EXACT" : "APPROXIMATE: the re-execution's final state differs from the recording";
    h.set("reproduction", info.reproduction);
    h.set("source", dir);

    std::string hj = h.dump();
    while ((10 + 4 + hj.size()) % 4) hj.push_back(' ');
    gzFile f = gzopen(outPath.c_str(), "wb6");
    if (!f) { err = "cannot write " + outPath; return false; }
    uint32_t n = uint32_t(hj.size());
    unsigned char len[4] = {uint8_t(n), uint8_t(n >> 8), uint8_t(n >> 16), uint8_t(n >> 24)};
    bool ok = gzwrite(f, "GPREPLAY1\n", 10) == 10 && gzwrite(f, len, 4) == 4 && gzwrite(f, hj.data(), unsigned(hj.size())) == int(hj.size());
    const char* p = reinterpret_cast<const char*>(frames.data());
    size_t left = frames.size() * sizeof(float);
    while (ok && left) {
        unsigned chunk = unsigned(std::min<size_t>(left, 1u << 20));
        ok = gzwrite(f, p, chunk) == int(chunk);
        p += chunk; left -= chunk;
    }
    if (gzclose(f) != Z_OK) ok = false;
    if (!ok) { err = "writing " + outPath + " failed"; return false; }
    info.frames = frames.size() / frameLen;
    info.seconds = w.time();
    return true;
}

}  // namespace gp
