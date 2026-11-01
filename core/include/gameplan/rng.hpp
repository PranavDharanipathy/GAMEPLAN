// Deterministic, serialisable random streams.
// Every subsystem draws from its own stream derived from (match seed, stream name) so that
// changing one subsystem's usage never shifts another's sequence. This is what makes
// common-random-number counterfactual comparisons valid.
#pragma once
#include <cmath>
#include <cstdint>
#include <string>

namespace gp {

class Rng {
public:
    struct State { uint64_t s[4]; double spare; bool hasSpare; };

    Rng() { seed(0x9E3779B97F4A7C15ULL); }
    explicit Rng(uint64_t seedValue) { seed(seedValue); }
    static Rng derive(uint64_t matchSeed, const std::string& stream) {
        uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : stream) { h ^= c; h *= 1099511628211ULL; }
        return Rng(matchSeed ^ (h * 0x9E3779B97F4A7C15ULL));
    }

    void seed(uint64_t v) {
        for (auto& x : st_.s) {
            v += 0x9E3779B97F4A7C15ULL;
            uint64_t z = v;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            x = z ^ (z >> 31);
        }
        st_.hasSpare = false;
        st_.spare = 0;
    }
    uint64_t next() {
        uint64_t* s = st_.s;
        uint64_t r = rotl(s[1] * 5, 7) * 9;
        uint64_t t = s[1] << 17;
        s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
        s[2] ^= t; s[3] = rotl(s[3], 45);
        return r;
    }
    double uniform() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }
    double uniform(double a, double b) { return a + (b - a) * uniform(); }
    bool bernoulli(double p) { return uniform() < p; }
    double normal() {
        if (st_.hasSpare) { st_.hasSpare = false; return st_.spare; }
        double u, v, s;
        do { u = uniform(-1, 1); v = uniform(-1, 1); s = u * u + v * v; } while (s >= 1.0 || s == 0.0);
        double m = std::sqrt(-2.0 * std::log(s) / s);
        st_.spare = v * m;
        st_.hasSpare = true;
        return u * m;
    }
    double normal(double mean, double sigma) { return mean + sigma * normal(); }
    double lognormal(double median, double sigmaLog) { return median * std::exp(sigmaLog * normal()); }
    // Student-t with nu dof (heavy tails); scale applies to the standard t variate.
    double studentT(double nu) {
        double z = normal();
        double chi2 = 0;
        int k = int(nu);
        for (int i = 0; i < k; ++i) { double g = normal(); chi2 += g * g; }
        return z / std::sqrt(chi2 / k);
    }
    double exponential(double mean) { return -mean * std::log(1.0 - uniform()); }

    State state() const { return st_; }
    void setState(const State& s) { st_ = s; }

private:
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    State st_{};
};

// Inverse standard normal CDF (Acklam), used for quantile-based "Extreme" sampling.
double normalQuantile(double p);

}  // namespace gp
