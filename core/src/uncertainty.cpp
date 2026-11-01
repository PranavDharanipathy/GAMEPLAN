#include "gameplan/uncertainty.hpp"

#include <cmath>
#include <stdexcept>

#include "gameplan/math.hpp"

namespace gp {

double normalQuantile(double p) {
    static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
                               1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00};
    static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
                               6.680131188771972e+01, -1.328068155288572e+01};
    static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
                               -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00};
    static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
                               3.754408661907416e+00};
    p = clampd(p, 1e-12, 1 - 1e-12);
    double q, r;
    if (p < 0.02425) {
        q = std::sqrt(-2 * std::log(p));
        return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
               ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
    }
    if (p > 1 - 0.02425) {
        q = std::sqrt(-2 * std::log(1 - p));
        return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
               ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
    }
    q = p - 0.5;
    r = q * q;
    return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
           (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1);
}

const char* toString(ULevel l) {
    switch (l) {
        case ULevel::Disabled: return "disabled";
        case ULevel::Nominal: return "nominal";
        case ULevel::Realistic: return "realistic";
        case ULevel::Extreme: return "extreme";
    }
    return "?";
}
ULevel parseULevel(const std::string& s) {
    if (s == "disabled") return ULevel::Disabled;
    if (s == "nominal") return ULevel::Nominal;
    if (s == "realistic") return ULevel::Realistic;
    if (s == "extreme") return ULevel::Extreme;
    throw std::runtime_error("unknown uncertainty level: " + s);
}

const std::vector<std::string>& UncertaintyConfig::subsystems() {
    static const std::vector<std::string> s = {"battery", "electrical", "motors", "servos", "drivetrain", "mechanical",
                                               "sensors", "timing", "vision", "field", "elements", "esd"};
    return s;
}
ULevel UncertaintyConfig::level(const std::string& s) const {
    auto it = levels_.find(s);
    return it == levels_.end() ? ULevel::Nominal : it->second;
}
Json UncertaintyConfig::toJson() const {
    Json j = Json::object();
    Json lv = Json::object();
    for (auto& s : subsystems()) lv.set(s, toString(level(s)));
    j.set("levels", lv);
    Json ov = Json::object();
    for (auto& kv : overrides_) ov.set(kv.first, kv.second);
    j.set("overrides", ov);
    return j;
}
UncertaintyConfig UncertaintyConfig::fromJson(const Json& j) {
    UncertaintyConfig c;
    if (j["all"].isString()) c.setAll(parseULevel(j["all"].str()));
    for (auto& kv : j["levels"].members()) c.setLevel(kv.first, parseULevel(kv.second.str()));
    for (auto& kv : j["overrides"].members()) c.setOverride(kv.first, kv.second.num());
    return c;
}

double UncertaintySampler::sample(const ParamSpec& p) {
    ULevel lvl = cfg_.level(p.subsystem);
    double v;
    bool ov = cfg_.hasOverride(p.name);
    if (ov) {
        v = cfg_.override_(p.name);
    } else if (lvl == ULevel::Disabled) {
        v = p.ideal;
    } else if (lvl == ULevel::Nominal || p.spread <= 0) {
        v = p.nominal;
    } else {
        Rng r = Rng::derive(seed_, "param:" + p.name);
        double z;
        if (lvl == ULevel::Realistic) {
            z = (p.dist == Dist::StudentT) ? r.studentT(p.nu) : r.normal();
            if (p.dist == Dist::Uniform) z = r.uniform(-1, 1);
        } else {
            // Extreme: adverse tail band [90%, 99.5%] of the same distribution.
            double u = 0.90 + 0.095 * r.uniform();
            double zq = normalQuantile(u);
            int dir = p.adverse == 0 ? (r.bernoulli(0.5) ? 1 : -1) : p.adverse;
            z = dir * zq;
            if (p.dist == Dist::Uniform) z = dir * (0.8 + 0.2 * r.uniform());
        }
        switch (p.dist) {
            case Dist::Normal:
            case Dist::StudentT: v = p.nominal + p.spread * z; break;
            case Dist::LogNormal: v = p.nominal * std::exp(p.spread * z); break;
            case Dist::Uniform: v = p.nominal + p.spread * z; break;
        }
    }
    v = clampd(v, p.minV, p.maxV);
    log_.push_back({p.name, v, p.nominal, lvl, ov});
    return v;
}

Json UncertaintySampler::logJson() const {
    Json a = Json::array();
    for (auto& s : log_) {
        Json o = Json::object();
        o.set("name", s.name);
        o.set("value", s.value);
        o.set("nominal", s.nominal);
        o.set("level", toString(s.level));
        o.set("overridden", s.overridden);
        a.push(o);
    }
    return a;
}

}  // namespace gp
