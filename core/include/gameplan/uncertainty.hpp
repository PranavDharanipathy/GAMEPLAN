// Uncertainty and Reality Modeling Engine.
//
// Four levels per subsystem (left to right in the UI): Disabled - Nominal - Realistic - Extreme.
//   Disabled : the effect is removed (ideal model: no noise, no backlash, nominal constants).
//   Nominal  : best-estimate constants and imperfections at their nominal magnitude, no random draws.
//   Realistic: parameters drawn from evidence-based distributions (can land above OR below nominal);
//              stochastic processes (drift, bias walk, jitter, heavy-tailed spikes) at calibrated size.
//   Extreme  : draws restricted to the adverse tail (quantile band) of the SAME distribution, i.e. a
//              plausible bad day, not "multiply every sigma by k".
// Each parameter carries its kind (aleatoric / epistemic / hidden-state) and provenance.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "gameplan/json.hpp"
#include "gameplan/rng.hpp"

namespace gp {

enum class ULevel { Disabled = 0, Nominal = 1, Realistic = 2, Extreme = 3 };
const char* toString(ULevel l);
ULevel parseULevel(const std::string& s);

enum class UKind { Aleatoric, Epistemic, HiddenState, ModelDiscrepancy };

enum class Dist { Normal, LogNormal, Uniform, StudentT };

struct ParamSpec {
    std::string name;          // e.g. "battery.r0_scale"
    std::string subsystem;     // e.g. "battery"
    double nominal = 1.0;      // value used at Nominal
    double ideal = 1.0;        // value used at Disabled
    Dist dist = Dist::Normal;
    double spread = 0.0;       // sigma (Normal/StudentT), sigmaLog (LogNormal), half-width (Uniform)
    double nu = 4.0;           // StudentT dof
    int adverse = +1;          // +1: larger is worse, -1: smaller is worse, 0: both tails adverse
    double minV = -1e300, maxV = 1e300;
    UKind kind = UKind::Epistemic;
    std::string provenance;
};

class UncertaintyConfig {
public:
    static const std::vector<std::string>& subsystems();
    ULevel level(const std::string& subsystem) const;
    void setLevel(const std::string& subsystem, ULevel l) { levels_[subsystem] = l; }
    void setAll(ULevel l) { for (auto& s : subsystems()) levels_[s] = l; }
    void setOverride(const std::string& param, double v) { overrides_[param] = v; }
    bool hasOverride(const std::string& p) const { return overrides_.count(p) != 0; }
    double override_(const std::string& p) const { return overrides_.at(p); }
    Json toJson() const;
    static UncertaintyConfig fromJson(const Json& j);

private:
    std::map<std::string, ULevel> levels_;
    std::map<std::string, double> overrides_;
};

struct SampledParam {
    std::string name;
    double value;
    double nominal;
    ULevel level;
    bool overridden;
};

// Samples hidden parameters for one match. All draws for a parameter come from a stream
// keyed by the parameter name, so the sampled value of X does not depend on which other
// parameters exist (stable under model evolution and counterfactual edits).
class UncertaintySampler {
public:
    UncertaintySampler(uint64_t seed, const UncertaintyConfig& cfg) : seed_(seed), cfg_(cfg) {}
    double sample(const ParamSpec& p);
    const std::vector<SampledParam>& log() const { return log_; }
    Json logJson() const;
    const UncertaintyConfig& config() const { return cfg_; }
    uint64_t seed() const { return seed_; }

private:
    uint64_t seed_;
    UncertaintyConfig cfg_;
    std::vector<SampledParam> log_;
};

// Stochastic processes with memory (not i.i.d. Gaussian).
struct OUProcess {  // mean-reverting drift, e.g. sensor bias vs temperature
    double x = 0, mean = 0, theta = 0.1, sigma = 0;
    void step(Rng& r, double dt) { x += theta * (mean - x) * dt + sigma * std::sqrt(dt) * r.normal(); }
};
struct RandomWalk {  // e.g. gyro bias instability
    double x = 0, sigma = 0;
    void step(Rng& r, double dt) { x += sigma * std::sqrt(dt) * r.normal(); }
};
struct SpikeProcess {  // rare heavy-tailed events (loop spikes, EMI bursts)
    double ratePerS = 0, meanMag = 0, sigmaLog = 0.5;
    double sample(Rng& r, double dt) { return r.bernoulli(ratePerS * dt) ? r.lognormal(meanMag, sigmaLog) : 0.0; }
};

}  // namespace gp
