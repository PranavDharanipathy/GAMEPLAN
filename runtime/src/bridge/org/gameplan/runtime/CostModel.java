package org.gameplan.runtime;

import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Per-SDK-call virtual execution cost (data/hardware/timing_profiles.json). */
final class CostModel {
    static final class Op {
        String kind = "local";
        boolean transaction, cachedIfUnchanged, bulk;
        int extraTransactions;
        double costUs, sigmaLog = 0.2;
    }

    double ctrlUs = 1250, expUs = 2300, sigma = 0.18, spikeP = 0.004, spikeMean = 9000, spikeSigma = 0.6, sdkOverheadUs = 18;
    double computePerLoopUs = 300, measuredScale = 3.2;
    boolean deterministicCompute = true;
    final Map<String, Op> ops = new HashMap<>();
    private long s0, s1;
    private double spare;
    private boolean hasSpare;

    CostModel(String path, long seed, boolean deterministic) {
        s0 = seed * 0x9E3779B97F4A7C15L + 1;
        s1 = (seed ^ 0xD1B54A32D192ED03L) | 1;
        deterministicCompute = deterministic;
        if (path == null || path.isEmpty()) return;
        try {
            Map<String, Object> j = Json.obj(Json.parse(new String(Files.readAllBytes(Paths.get(path)), "UTF-8")));
            Map<String, Object> tr = Json.obj(j.get("transaction"));
            ctrlUs = Json.num(tr.get("controlHubInternal_us"), ctrlUs);
            expUs = Json.num(tr.get("expansionHubRs485_us"), expUs);
            sigma = Json.num(tr.get("sigmaLog"), sigma);
            Map<String, Object> sp = Json.obj(tr.get("spike"));
            if (sp != null) {
                spikeP = Json.num(sp.get("probability"), spikeP);
                spikeMean = Json.num(sp.get("extra_us_mean"), spikeMean);
                spikeSigma = Json.num(sp.get("extra_us_sigmaLog"), spikeSigma);
            }
            sdkOverheadUs = Json.num(j.get("sdkOverhead_us"), sdkOverheadUs);
            Map<String, Object> o = Json.obj(j.get("operations"));
            for (Map.Entry<String, Object> e : o.entrySet()) {
                Map<String, Object> m = Json.obj(e.getValue());
                Op op = new Op();
                op.kind = Json.str(m.get("kind"), "local");
                op.transaction = Json.bool(m.get("transaction"), false);
                op.cachedIfUnchanged = Json.bool(m.get("cachedIfUnchanged"), false);
                op.bulk = Json.bool(m.get("bulk"), false);
                op.extraTransactions = (int) Json.num(m.get("extraTransactions"), 0);
                op.costUs = Json.num(m.get("cost_us"), 0);
                op.sigmaLog = Json.num(m.get("sigmaLog"), 0.2);
                ops.put(e.getKey(), op);
            }
            Map<String, Object> c = Json.obj(j.get("compute"));
            if (c != null) {
                computePerLoopUs = Json.num(c.get("deterministicPerUserLoop_us"), computePerLoopUs);
                measuredScale = Json.num(c.get("measuredScale"), measuredScale);
            }
        } catch (Exception e) {
            throw new RuntimeException("cannot read timing profile " + path + ": " + e.getMessage(), e);
        }
    }

    Op op(String name) {
        Op o = ops.get(name);
        if (o == null) {
            o = new Op();
            o.kind = "local";
            o.costUs = 2;
            ops.put(name, o);
        }
        return o;
    }

    private double uniform() {
        long x = s0, y = s1;
        s0 = y;
        x ^= x << 23;
        s1 = x ^ y ^ (x >>> 17) ^ (y >>> 26);
        return ((s1 + y) >>> 11) * (1.0 / 9007199254740992.0);
    }

    private double normal() {
        if (hasSpare) { hasSpare = false; return spare; }
        double u, v, s;
        do { u = uniform() * 2 - 1; v = uniform() * 2 - 1; s = u * u + v * v; } while (s >= 1 || s == 0);
        double m = Math.sqrt(-2 * Math.log(s) / s);
        spare = v * m;
        hasSpare = true;
        return u * m;
    }

    double lognormal(double median, double sl) { return median * Math.exp(sl * normal()); }

    /** One Lynx command round trip on the given hub. */
    double transactionUs(boolean controlHub) {
        double base = lognormal(controlHub ? ctrlUs : expUs, sigma);
        if (uniform() < spikeP) base += lognormal(spikeMean, spikeSigma);
        return base;
    }

    double opUs(Op o) { return o.costUs <= 0 ? 0 : lognormal(o.costUs, o.sigmaLog); }

    static List<String> describe() { return java.util.Collections.emptyList(); }
}
