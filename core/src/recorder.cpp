#include "gameplan/recorder.hpp"

#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#ifdef GP_HAVE_ZLIB
#include <zlib.h>
#endif

#include "gameplan/world.hpp"

namespace gp {

void makeDirs(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur += path[i];
        if (path[i] == '/' || path[i] == '\\' || i + 1 == path.size()) {
#ifdef _WIN32
            _mkdir(cur.c_str());
#else
            mkdir(cur.c_str(), 0755);
#endif
        }
    }
}

GzTable::~GzTable() { close(); }

bool GzTable::open(const std::string& path, const std::vector<std::string>& columns) {
    close();
    cols_ = columns.size();
    std::string head;
    for (size_t i = 0; i < columns.size(); ++i) head += (i ? "," : "") + columns[i];
    head += "\n";
#ifdef GP_HAVE_ZLIB
    gzFile f = gzopen((path + ".gz").c_str(), "wb6");
    if (!f) return false;
    gzwrite(f, head.data(), unsigned(head.size()));
    h_ = f;
    gz_ = true;
#else
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(head.data(), 1, head.size(), f);
    h_ = f;
    gz_ = false;
#endif
    return true;
}

void GzTable::row(const std::vector<double>& v) {
    if (!h_) return;
    std::string line;
    line.reserve(v.size() * 10);
    char b[32];
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) line += ',';
        if (v[i] == std::floor(v[i]) && std::fabs(v[i]) < 1e12) std::snprintf(b, sizeof b, "%.0f", v[i]);
        else std::snprintf(b, sizeof b, "%.6g", v[i]);
        line += b;
    }
    line += '\n';
#ifdef GP_HAVE_ZLIB
    if (gz_) { gzwrite((gzFile)h_, line.data(), unsigned(line.size())); return; }
#endif
    std::fwrite(line.data(), 1, line.size(), (std::FILE*)h_);
}

void GzTable::close() {
    if (!h_) return;
#ifdef GP_HAVE_ZLIB
    if (gz_) gzclose((gzFile)h_);
    else std::fclose((std::FILE*)h_);
#else
    std::fclose((std::FILE*)h_);
#endif
    h_ = nullptr;
}

std::vector<std::vector<double>> readGzTable(const std::string& path, std::vector<std::string>* header) {
    std::string text;
#ifdef GP_HAVE_ZLIB
    gzFile f = gzopen((path + ".gz").c_str(), "rb");
    if (f) {
        char buf[1 << 16];
        int n;
        while ((n = gzread(f, buf, sizeof buf)) > 0) text.append(buf, size_t(n));
        gzclose(f);
    } else {
        text = readTextFile(path);
    }
#else
    text = readTextFile(path);
#endif
    std::vector<std::vector<double>> rows;
    std::istringstream ss(text);
    std::string line;
    bool first = true;
    while (std::getline(ss, line)) {
        if (first) {
            first = false;
            if (header) {
                std::istringstream hs(line);
                std::string c;
                while (std::getline(hs, c, ',')) header->push_back(c);
            }
            continue;
        }
        std::vector<double> r;
        const char* p = line.c_str();
        char* end;
        while (*p) {
            r.push_back(std::strtod(p, &end));
            p = end;
            if (*p == ',') ++p;
            else break;
        }
        rows.push_back(r);
    }
    return rows;
}

Recorder::Recorder(const std::string& dir) : dir_(dir) {
    makeDirs(dir_);
    events_ = std::fopen((dir_ + "/events.jsonl").c_str(), "wb");
    hashes_ = std::fopen((dir_ + "/hashes.csv").c_str(), "wb");
    if (hashes_) std::fprintf(hashes_, "t,hash\n");
}

Recorder::~Recorder() {
    if (events_) std::fclose(events_);
    if (hashes_) std::fclose(hashes_);
}

void Recorder::writeHeader(const Json& header) { writeTextFile(dir_ + "/header.json", header.dump(2)); }

void Recorder::event(double t, const std::string& kind, int robot, const Json& detail) {
    if (!events_) return;
    Json e = Json::object();
    e.set("t", t);
    e.set("kind", kind);
    e.set("robot", robot);
    e.set("detail", detail);
    std::string s = e.dump() + "\n";
    std::fwrite(s.data(), 1, s.size(), events_);
}

void Recorder::hash(double t, const std::string& h) {
    if (hashes_) std::fprintf(hashes_, "%.4f,%s\n", t, h.c_str());
}

void Recorder::sample(const World& w, bool eventActive) {
    double t = w.time();
    const auto& rs = w.robots();
    if (!frames_.isOpen()) {
        std::vector<std::string> cols = {"t", "phase"};
        for (size_t i = 0; i < rs.size(); ++i)
            for (const char* c : {"x", "y", "h", "vx", "vy", "w", "pitch", "roll", "held"}) cols.push_back("r" + std::to_string(i) + "_" + c);
        for (size_t e = 0; e < w.elements().size(); ++e)
            for (const char* c : {"x", "y", "z", "s"}) cols.push_back("e" + std::to_string(e) + "_" + c);
        for (const char* c : {"hiveR_up", "hiveR_tip", "hiveB_up", "hiveB_tip", "scoreR", "scoreB"}) cols.push_back(c);
        frames_.open(dir_ + "/frames.csv", cols);
        for (size_t i = 0; i < rs.size(); ++i) {
            std::vector<std::string> rc = {"t", "battV", "battI", "soc", "battTemp", "ocv", "r0", "vrc", "mainR"};
            const Robot& r = *rs[i].robot;
            for (auto& h : r.elec.hubs) { rc.push_back(h.id + "_busV"); rc.push_back(h.id + "_servoRailA"); rc.push_back(h.id + "_reset"); }
            for (auto& m : r.elec.motors)
                for (const char* c : {"duty", "I", "V", "torque", "rpm", "tw", "limited"}) rc.push_back(m.name + "_" + c);
            for (auto& s : r.elec.servos) for (const char* c : {"cmd", "angle", "I", "V"}) rc.push_back(s.name + "_" + c);
            for (auto& wh : r.wheels) for (const char* c : {"N", "slip"}) rc.push_back("wheel_" + wh.name + "_" + c);
            rc.push_back("pinpointErr");
            rc.push_back("imuYawErr");
            robotTables_.emplace_back(new GzTable());
            robotTables_.back()->open(dir_ + "/robot_" + std::to_string(i) + ".csv", rc);
        }
    }
    // adaptive sampling: 50 Hz frames, 500 Hz around events; engineering channels 200 Hz
    double framePeriod = eventActive ? 0.002 : 0.02;
    if (lastFrame_ < 0 || t - lastFrame_ >= framePeriod - 1e-9) {
        lastFrame_ = t;
        std::vector<double> v = {t, double(int(w.rules().phase()))};
        for (auto& s : rs) {
            const Robot& r = *s.robot;
            for (double x : {r.pos.x, r.pos.y, r.heading, r.vel.x, r.vel.y, r.omega, r.pitch, r.roll, double(r.held.size())}) v.push_back(x);
        }
        for (auto& e : w.elements()) for (double x : {e.p.x, e.p.y, e.p.z, double(int(e.state))}) v.push_back(x);
        v.push_back(w.hive(Alliance::Red).upCell); v.push_back(w.hive(Alliance::Red).tipping);
        v.push_back(w.hive(Alliance::Blue).upCell); v.push_back(w.hive(Alliance::Blue).tipping);
        v.push_back(w.rules().score(Alliance::Red).total()); v.push_back(w.rules().score(Alliance::Blue).total());
        frames_.row(v);
    }
    if (lastRobotSample_ < 0 || t - lastRobotSample_ >= 0.005 - 1e-9) {
        lastRobotSample_ = t;
        for (size_t i = 0; i < rs.size(); ++i) {
            const Robot& r = *rs[i].robot;
            const auto& b = r.elec.battery.st;
            std::vector<double> v = {t, b.vTerm, b.current, b.soc, b.temp, b.ocv, b.r0Now, b.vrc, r.elec.mainPath.r()};
            for (auto& h : r.elec.hubs) { v.push_back(h.busV); v.push_back(h.servoRailCurrent); v.push_back(h.inReset); }
            for (size_t m = 0; m < r.elec.motors.size(); ++m) {
                const auto& mu = r.elec.motors[m];
                double rpm = r.joints[r.motorJoint[m]].motorOmega() * 60 / (2 * kPi);
                for (double x : {mu.duty, mu.current, mu.terminalV, mu.torque, rpm, mu.tWinding, mu.currentLimited ? 1.0 : 0.0}) v.push_back(x);
            }
            for (size_t s = 0; s < r.elec.servos.size(); ++s) {
                const auto& sv = r.elec.servos[s];
                v.push_back(sv.pulseUs); v.push_back(sv.angle); v.push_back(sv.current); v.push_back(sv.railV);
            }
            for (auto& wh : r.wheels) { v.push_back(wh.normal); v.push_back(wh.slip); }
            v.push_back(rs[i].sensors->pinpointError());
            v.push_back(wrapAngle(rs[i].sensors->imuYawObserved() - wrapAngle(r.heading - 0)));
            robotTables_[i]->row(v);
        }
    }
}

void Recorder::loop(int robot, const LoopRecord& r) {
    while (int(loopTables_.size()) <= robot) {
        loopTables_.emplace_back(new GzTable());
        loopTables_.back()->open(dir_ + "/loops_" + std::to_string(loopTables_.size() - 1) + ".csv",
                                 {"tStart", "tEnd", "transactions", "hwUs", "i2cUs", "computeUs", "overheadUs", "visionUs", "slowestUs"});
    }
    loopTables_[robot]->row({r.tStart, r.tEnd, double(r.transactions), r.hwUs, r.i2cUs, r.computeUs, r.overheadUs, r.visionUs, r.slowestUs});
}

void Recorder::observed(int robot, double t, const std::vector<std::pair<std::string, double>>& values) {
    while (int(obsTables_.size()) <= robot) obsTables_.emplace_back(new GzTable());
    if (!obsTables_[robot]->isOpen()) {
        std::vector<std::string> cols = {"t"};
        for (auto& kv : values) cols.push_back(kv.first);
        obsColumns_[robot] = cols;
        obsTables_[robot]->open(dir_ + "/observed_" + std::to_string(robot) + ".csv", cols);
    }
    std::vector<double> row = {t};
    for (auto& kv : values) row.push_back(kv.second);
    row.resize(obsColumns_[robot].size(), 0.0);
    obsTables_[robot]->row(row);
}

void Recorder::input(int robot, double t, const std::vector<double>& g1, const std::vector<double>& g2) {
    while (int(inputTables_.size()) <= robot) inputTables_.emplace_back(new GzTable());
    if (!inputTables_[robot]->isOpen()) {
        std::vector<std::string> cols = {"t"};
        for (int g = 1; g <= 2; ++g)
            for (const char* c : {"lx", "ly", "rx", "ry", "lt", "rt", "buttons"}) cols.push_back("g" + std::to_string(g) + "_" + c);
        inputTables_[robot]->open(dir_ + "/inputs_" + std::to_string(robot) + ".csv", cols);
    }
    std::vector<double> row = {t};
    row.insert(row.end(), g1.begin(), g1.end());
    row.insert(row.end(), g2.begin(), g2.end());
    inputTables_[robot]->row(row);
}

void Recorder::finish(const World& w, const Json& extra) {
    frames_.close();
    if (hashes_) { std::fclose(hashes_); hashes_ = nullptr; }
    if (events_) { std::fflush(events_); }
    for (auto& t : robotTables_) t->close();
    for (auto& t : loopTables_) t->close();
    for (auto& t : obsTables_) t->close();
    for (auto& t : inputTables_) t->close();
    Json r = w.rules().resultJson();
    Json faults = Json::array();
    for (auto& f : const_cast<World&>(w).faults()) {
        Json o = Json::object();
        o.set("t", f.t); o.set("robot", f.robot); o.set("kind", f.kind); o.set("target", f.target);
        o.set("duration", f.duration); o.set("energy_mJ", f.energyMJ); o.set("cause", f.cause);
        faults.push(o);
    }
    r.set("faults", faults);
    Json robots = Json::array();
    for (auto& s : w.robots()) {
        const Robot& rb = *s.robot;
        Json o = Json::object();
        o.set("label", rb.label); o.set("alliance", toString(rb.alliance)); o.set("distance_m", rb.distance);
        o.set("maxSpeed_mps", rb.maxSpeed); o.set("collisions", rb.collisions); o.set("energy_J", rb.elec.battery.st.energyJ);
        o.set("minBatteryV", 0.0); o.set("peakCurrent_A", rb.elec.battery.st.peakCurrent);
        o.set("finalSoc", rb.elec.battery.st.soc); o.set("tipped", rb.tipped); o.set("rcAlive", rb.rcAlive);
        Json hubs = Json::array();
        for (auto& h : rb.elec.hubs) { Json hj = Json::object(); hj.set("id", h.id); hj.set("minBusV", h.minBusV); hj.set("brownouts", h.brownouts); hubs.push(hj); }
        o.set("hubs", hubs);
        Json ev = Json::array();
        for (auto& e : rb.elec.events) {
            Json ej = Json::object();
            ej.set("t", e.t); ej.set("kind", e.kind); ej.set("device", e.device); ej.set("value", e.value);
            ev.push(ej);
        }
        o.set("electricalEvents", ev);
        robots.push(o);
    }
    r.set("robots", robots);
    for (auto& kv : extra.members()) r.set(kv.first, kv.second);
    summary_ = r;
    writeTextFile(dir_ + "/result.json", r.dump(2));
}

}  // namespace gp
