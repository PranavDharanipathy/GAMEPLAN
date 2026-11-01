// Post-match engineering analysis. Everything here is numerical and traceable to recorded
// channels; no AI/LLM is used to produce conclusions. Each figure states whether it is
// measured (simulated sensor), calculated (from the model state) or estimated (model inputs
// with uncertainty).
#pragma once
#include <string>
#include <vector>

#include "gameplan/json.hpp"

namespace gp {

struct VoltageAttribution {
    double t = 0;
    double busV = 0, refV = 0;           // hub bus voltage and the no-load reference at match start
    double socDrop = 0, polarisation = 0, internalR = 0, wiring = 0;
    std::vector<std::pair<std::string, double>> bySubsystem;   // share of the I*R0 drop
};

struct SlowdownExplanation {
    double tStart = 0, tEnd = 0;
    double commanded = 0, achievedSpeed = 0, referenceSpeed = 0;
    std::vector<std::pair<std::string, double>> causes;  // cause -> fraction of lost speed
    std::string text;
};

Json analyseRobotElectrical(const std::string& recordDir, int robot);
Json analyseLoopTiming(const std::string& recordDir, int robot);
Json analyseDriverVsRobot(const std::string& recordDir, int robot);
std::vector<SlowdownExplanation> explainDrivetrainSlowdowns(const std::string& recordDir, int robot, int maxResults = 3);
void writeMatchReport(const std::string& recordDir, const std::string& outHtml);

// SVG line chart (inline, theme-aware via CSS variables)
std::string svgLineChart(const std::string& title, const std::string& yLabel, const std::vector<double>& x,
                         const std::vector<std::pair<std::string, std::vector<double>>>& series, double yMin = 0, double yMax = 0,
                         const std::vector<std::pair<double, std::string>>& markers = {});

}  // namespace gp
