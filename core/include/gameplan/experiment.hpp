// Local batch experimentation (never cloud). Runs many matches in parallel on this machine
// using the same World as interactive play.
#pragma once
#include <string>
#include <vector>

#include "gameplan/json.hpp"

namespace gp {

struct ExperimentSpec {
    std::string name, matchPath, mode = "stochastic", outDir = "out/experiments/run", objective = "red.total";
    int trials = 20;
    unsigned long long baseSeed = 1;
    int threads = 0;           // 0 = auto (hardware threads - 1)
    bool record = false;
    double maxWallS = 3600, maxStorageMB = 2000;
    int maxTrials = 5000;
    Json variants;             // [{name, patch:{...}}]
    Json adversarial;          // {params:[{key,min,max}], iterations, minimize}
    Json raw;
    static ExperimentSpec load(const std::string& path);
};

struct ResourceEstimate {
    int trials = 0, threads = 1;
    double perTrialWallS = 0, totalWallS = 0, memoryMB = 0, storageMB = 0;
    double cpuCoreSeconds = 0;
    std::string gpu = "none (headless batch does not render)";
    double externalApiCost = 0;
    bool withinLimits = true;
    std::string limitReason;
    Json toJson() const;
};

ResourceEstimate estimateResources(const ExperimentSpec& s);
Json runExperiment(const ExperimentSpec& s, const std::string& outDir);   // honours <out>/control: pause | cancel
Json runStressTests(const std::string& robotPath, const std::string& componentsPath, const std::string& outDir);

}  // namespace gp
