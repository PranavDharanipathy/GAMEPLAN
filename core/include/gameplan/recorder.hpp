// Match recording. Every match writes a self-describing directory:
//   header.json     versions, hashes, seed, settings, robots, sampled hidden parameters
//   frames.csv.gz   ground truth replay frames (adaptive: 50 Hz, 500 Hz around events)
//   robot_<i>.csv.gz engineering channels per robot (electrical, mechanical, timing)
//   observed_<i>.csv.gz what the robot program saw (sensor values) - kept separate from truth
//   loops_<i>.csv.gz robot-program loop timing records (from the FTC runtime)
//   inputs_<i>.csv.gz gamepad inputs delivered to the runtime (for deterministic re-execution)
//   events.jsonl    every game / electrical / fault / rule event
//   hashes.csv      world state hash every second (re-execution verification)
//   result.json     final score, violations, RP
#pragma once
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gameplan/json.hpp"

namespace gp {

class World;

class GzTable {
public:
    GzTable() = default;
    ~GzTable();
    bool open(const std::string& path, const std::vector<std::string>& columns);
    void row(const std::vector<double>& values);
    void close();
    bool isOpen() const { return h_ != nullptr; }

private:
    void* h_ = nullptr;
    bool gz_ = false;
    size_t cols_ = 0;
};

struct LoopRecord {
    double tStart, tEnd;
    int transactions;
    double hwUs, i2cUs, computeUs, overheadUs, visionUs;
    std::string slowest;
    double slowestUs;
};

class Recorder {
public:
    explicit Recorder(const std::string& dir);
    ~Recorder();
    const std::string& dir() const { return dir_; }
    void writeHeader(const Json& header);
    void sample(const World& w, bool eventActive);
    void event(double t, const std::string& kind, int robot, const Json& detail);
    void loop(int robot, const LoopRecord& r);
    void observed(int robot, double t, const std::vector<std::pair<std::string, double>>& values);
    void input(int robot, double t, const std::vector<double>& gp1, const std::vector<double>& gp2);
    void hash(double t, const std::string& h);
    void finish(const World& w, const Json& extra);
    Json summary() const { return summary_; }

private:
    std::string dir_;
    GzTable frames_;
    std::vector<std::unique_ptr<GzTable>> robotTables_, loopTables_, obsTables_, inputTables_;
    std::FILE* events_ = nullptr;
    std::FILE* hashes_ = nullptr;
    double lastFrame_ = -1, lastRobotSample_ = -1;
    Json summary_;
    std::map<int, std::vector<std::string>> obsColumns_;
};

std::vector<std::vector<double>> readGzTable(const std::string& path, std::vector<std::string>* header = nullptr);
void makeDirs(const std::string& path);

}  // namespace gp
