package org.gameplan.runtime;

import java.lang.management.ManagementFactory;
import java.lang.management.ThreadMXBean;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

import org.gameplan.runtime.api.DeviceInfo;
import org.gameplan.runtime.api.GamepadData;
import org.gameplan.runtime.api.OpModeKilledError;
import org.gameplan.runtime.api.Sim;

/**
 * Virtual Control Hub execution environment. Robot code runs on an OpMode thread; every SDK call
 * charges virtual time; the main thread performs the lockstep exchange with the host.
 */
final class VirtualRuntime implements Sim.Backend {
    static final double MAX_QUANTUM = 0.005;   // force an exchange after 5 ms of virtual time without one
    static final double MIN_RESYNC = 0.0002;   // data younger than 0.2 ms counts as fresh

    final Protocol io;
    final CostModel cost;
    final List<DeviceInfo> devices;
    final Map<String, DeviceInfo> byName = new HashMap<>();
    final String sdkVersion;

    // ---- shared state (guarded by this) ----
    volatile double t = 0;
    volatile double lastStateT = 0;
    final Map<Integer, Double> values = new HashMap<>();
    final GamepadData[] pads = {new GamepadData(), new GamepadData()};
    final long[] padSeq = {0, 0};
    final Map<Integer, String> vision = new HashMap<>();
    volatile boolean rcAlive = true, dsDisconnected = false, inputAllowed = false;
    volatile boolean startRequested = false, stopRequested = false, inInit = false, killed = false;
    double stopRequestedAt = -1;
    final List<double[]> pending = new ArrayList<>();
    final Map<Integer, Double> lastWritten = new HashMap<>();
    // bulk caching per hub device index
    final Map<Integer, Integer> bulkMode = new HashMap<>();
    final Map<Integer, Map<Integer, Double>> bulkCache = new HashMap<>();
    final Map<Integer, Set<Integer>> bulkReadSince = new HashMap<>();
    // statistics
    final Map<String, double[]> stats = new LinkedHashMap<>(); // op -> {calls, transactions, totalUs}
    double loopStart = -1, loopHw, loopI2c, loopCompute, loopOverhead, loopVision, loopSlowest;
    int loopTransactions;
    double lastTelemetry = -1;
    volatile boolean deterministic = true;
    Thread opThread;
    private long lastCpuNs = -1;
    private final ThreadMXBean mx = ManagementFactory.getThreadMXBean();

    // ---- handoff ----
    static final class Req {
        final double t;
        final List<double[]> cmds;
        final boolean finished;
        volatile boolean isDone;
        Req(double t, List<double[]> c, boolean f) { this.t = t; this.cmds = c; this.finished = f; }
    }
    final LinkedBlockingQueue<Req> requests = new LinkedBlockingQueue<>();

    VirtualRuntime(Protocol io, CostModel cost, List<DeviceInfo> devices, String sdkVersion) {
        this.io = io;
        this.cost = cost;
        this.devices = Collections.unmodifiableList(devices);
        this.sdkVersion = sdkVersion;
        for (DeviceInfo d : devices) {
            byName.put(d.name, d);
            if (d.kind.equals("hub")) bulkMode.put(d.index, 0);
        }
    }

    // ---------------------------------------------------------------- time & cost
    @Override public double now() { return t; }

    private void account(String op, double us, int transactions, String kind) {
        double[] s = stats.computeIfAbsent(op, k -> new double[3]);
        s[0]++;
        s[1] += transactions;
        s[2] += us;
        loopTransactions += transactions;
        switch (kind) {
            case "i2c": loopI2c += us; break;
            case "vision": loopVision += us; break;
            case "compute": loopCompute += us; break;
            case "framework": case "comm": case "local": loopOverhead += us; break;
            default: loopHw += us;
        }
        loopSlowest = Math.max(loopSlowest, us);
    }

    @Override public synchronized void charge(String op, double extraUs) {
        checkKilled();
        CostModel.Op o = cost.op(op);
        double us = (o.transaction ? cost.transactionUs(true) * (1 + o.extraTransactions) : cost.opUs(o)) + extraUs;
        t += us * 1e-6;
        account(op, us, o.transaction ? 1 + o.extraTransactions : 0, o.kind);
        if (t - lastStateT > MAX_QUANTUM) syncNow();
    }

    private DeviceInfo dev(int index) {
        for (DeviceInfo d : devices) if (d.index == index) return d;
        throw new IllegalArgumentException("no device index " + index);
    }

    private static int key(int dev, int field) { return dev * 64 + field; }

    private double transaction(DeviceInfo d, String op) {
        CostModel.Op o = cost.op(op);
        double us = cost.sdkOverheadUs;
        int tr = 1 + o.extraTransactions;
        for (int k = 0; k < tr; k++) us += cost.transactionUs(d.controlHub);
        account(op, us, tr, "lynx");
        return us;
    }

    @Override public synchronized double readDirect(int devIdx, int field, String op) {
        checkKilled();
        DeviceInfo d = dev(devIdx);
        double us = transaction(d, op);
        t += us * 0.5e-6;          // response sampled mid-transaction
        syncIfStale();
        t += us * 0.5e-6;
        return value(devIdx, field);
    }

    @Override public synchronized double readI2c(int devIdx, int field, String op) {
        checkKilled();
        CostModel.Op o = cost.op(op);
        double us = cost.sdkOverheadUs + cost.opUs(o);
        account(op, us, 1, "i2c");
        t += us * 0.5e-6;
        syncIfStale();
        t += us * 0.5e-6;
        return value(devIdx, field);
    }

    @Override public synchronized double readBulk(int devIdx, int field, String op) {
        checkKilled();
        DeviceInfo d = dev(devIdx);
        int hub = d.hubIndex;
        int mode = bulkMode.getOrDefault(hub, 0);
        int k = key(devIdx, field);
        if (mode == 0) return readDirect(devIdx, field, op);   // OFF: every read is a Lynx transaction
        Map<Integer, Double> cache = bulkCache.get(hub);
        Set<Integer> since = bulkReadSince.computeIfAbsent(hub, x -> new HashSet<>());
        boolean refresh = cache == null || (mode == 2 && since.contains(k)) || (mode == 1 && false);
        if (mode == 2 && cache != null && since.contains(k)) refresh = true;    // AUTO: repeat read clears the cache
        if (cache == null) refresh = true;
        if (refresh) {
            double us = transaction(d, "lynx.getBulkData");
            t += us * 0.5e-6;
            syncIfStale();
            t += us * 0.5e-6;
            Map<Integer, Double> snap = new HashMap<>();
            for (Map.Entry<Integer, Double> e : values.entrySet()) snap.put(e.getKey(), e.getValue());
            bulkCache.put(hub, snap);
            since.clear();
            cache = snap;
        } else {
            double us = cost.sdkOverheadUs * 0.3;
            t += us * 1e-6;
            account(op + " (cached)", us, 0, "local");
        }
        since.add(k);
        Double v = cache.get(k);
        return v == null ? 0.0 : v;
    }

    @Override public synchronized double peek(int devIdx, int field) { return value(devIdx, field); }

    private double value(int devIdx, int field) {
        Double v = values.get(key(devIdx, field));
        return v == null ? 0.0 : v;
    }

    @Override public synchronized void write(int devIdx, int field, double v, String op, boolean skipIfUnchanged) {
        checkKilled();
        int k = key(devIdx, field);
        Double last = lastWritten.get(k);
        if (skipIfUnchanged && last != null && last == v) {
            double us = cost.sdkOverheadUs;
            t += us * 1e-6;
            account(op + " (unchanged, no command)", us, 0, "local");
            return;
        }
        DeviceInfo d = dev(devIdx);
        double us = transaction(d, op);
        double tCmd = t + us * 0.5e-6;   // hub receives the command mid-transaction
        t += us * 1e-6;
        pending.add(new double[]{devIdx, field, v, tCmd});
        lastWritten.put(k, v);
        if (t - lastStateT > MAX_QUANTUM) syncNow();
    }

    @Override public void sleep(double seconds) {
        synchronized (this) {
            checkKilled();
            if (stopRequested && Thread.currentThread() == opThread) { Thread.currentThread().interrupt(); return; }
            t += Math.max(0, seconds);
            account("sleep", seconds * 1e6, 0, "idle");
            syncNow();
        }
    }

    private void syncIfStale() { if (t - lastStateT > MIN_RESYNC) syncNow(); }

    /**
     * Hand our clock and buffered writes to the main thread; block (releasing the monitor) until the
     * host has answered and the main thread has applied the new state. Callers hold the monitor.
     */
    void syncNow() {
        if (Thread.currentThread().getName().equals("gp-main")) return;
        if (opThread != null && Thread.currentThread() != opThread && deterministic) {
            deterministic = false;
            status("running", null, "robot code uses additional threads: lockstep stays correct, bit-reproducibility is no longer guaranteed");
        }
        userComputeMeasured();
        List<double[]> cmds = new ArrayList<>(pending);
        pending.clear();
        Req r = new Req(t, cmds, false);
        requests.add(r);
        boolean intr = false;
        while (!r.isDone) {
            try { wait(); } catch (InterruptedException e) { intr = true; }
        }
        if (intr) Thread.currentThread().interrupt();
        checkKilled();
    }

    void postFinished() { requests.add(new Req(t, new ArrayList<>(pending), true)); pending.clear(); }

    // ---------------------------------------------------------------- devices / misc
    @Override public List<DeviceInfo> devices() { return devices; }
    @Override public synchronized void setBulkMode(int hub, int mode) { bulkMode.put(hub, mode); bulkCache.remove(hub); }
    @Override public synchronized int getBulkMode(int hub) { return bulkMode.getOrDefault(hub, 0); }
    @Override public synchronized void clearBulkCache(int hub) {
        bulkCache.remove(hub);
        charge("lynx.clearBulkCache", 0);
    }
    @Override public synchronized GamepadData gamepad(int n) { return pads[n == 2 ? 1 : 0]; }
    @Override public synchronized boolean gamepadChanged(int n, long[] seq) {
        int i = n == 2 ? 1 : 0;
        if (seq[0] != padSeq[i]) { seq[0] = padSeq[i]; return true; }
        return false;
    }
    @Override public synchronized void telemetry(List<String> lines, int intervalMs) {
        charge("telemetry.update", 0);
        if (lastTelemetry >= 0 && (t - lastTelemetry) * 1000 < intervalMs) return;
        lastTelemetry = t;
        io.sendText(Protocol.TELEMETRY, Json.write(Json.map("t", t, "lines", lines)));
    }
    @Override public synchronized String vision(int dev) {
        charge("vision.aprilTagDetections", 0);
        return vision.get(dev);
    }
    @Override public void log(String level, String msg) {
        io.sendText(Protocol.LOG, Json.write(Json.map("level", level, "msg", msg, "t", t)));
    }
    @Override public boolean startRequested() { return startRequested; }
    @Override public boolean stopRequested() { return stopRequested; }
    @Override public boolean inInit() { return inInit; }
    @Override public String sdkVersion() { return sdkVersion; }

    @Override public synchronized void userCompute() {
        if (cost.deterministicCompute) {
            double us = cost.computePerLoopUs;
            t += us * 1e-6;
            account("user code (modelled)", us, 0, "compute");
        } else userComputeMeasured();
    }

    private void userComputeMeasured() {
        if (cost.deterministicCompute || Thread.currentThread() != opThread) return;
        long cpu = mx.getCurrentThreadCpuTime();
        if (lastCpuNs >= 0) {
            double us = (cpu - lastCpuNs) / 1000.0 * cost.measuredScale;
            t += us * 1e-6;
            account("user code (measured x" + cost.measuredScale + ")", us, 0, "compute");
        }
        lastCpuNs = cpu;
    }

    @Override public synchronized void loopBoundary() {
        if (loopStart >= 0) {
            ByteBuffer b = ByteBuffer.allocate(8 * 8 + 4).order(ByteOrder.LITTLE_ENDIAN);
            b.putDouble(loopStart).putDouble(t).putInt(loopTransactions).putDouble(loopHw).putDouble(loopI2c).putDouble(loopCompute)
             .putDouble(loopOverhead).putDouble(loopVision).putDouble(loopSlowest);
            io.send(Protocol.LOOP, b.array());
        }
        loopStart = t;
        loopHw = loopI2c = loopCompute = loopOverhead = loopVision = loopSlowest = 0;
        loopTransactions = 0;
    }

    @Override public synchronized void cycleEnd(double overheadUs) {
        checkKilled();
        CostModel.Op o = cost.op("opmode.loopOverhead");
        double us = overheadUs >= 0 ? overheadUs : cost.opUs(o);
        t += us * 1e-6;
        account("opmode.loopOverhead", us, 0, "framework");
        syncNow();
    }

    @Override public synchronized void chargeDevice(int devIdx, String op) {
        checkKilled();
        double us = transaction(dev(devIdx), op);
        t += us * 1e-6;
        if (t - lastStateT > MAX_QUANTUM) syncNow();
    }

    @Override public void checkKilled() {
        if (killed && !Thread.currentThread().getName().equals("gp-main"))
            throw new OpModeKilledError("OpMode force-stopped: it did not exit within msStuckDetectStop after STOP");
    }

    void status(String state, String opMode, String detail) {
        Map<String, Object> m = Json.map("opModeState", state, "opMode", opMode, "deterministic", deterministic, "t", t);
        if (detail != null) m.put("detail", detail);
        io.sendText(Protocol.STATUS, Json.write(m));
    }

    Map<String, Object> hwStats() {
        List<Object> ops = new ArrayList<>();
        for (Map.Entry<String, double[]> e : stats.entrySet()) {
            double[] s = e.getValue();
            ops.add(Json.map("op", e.getKey(), "calls", s[0], "transactions", s[1], "totalUs", s[2], "meanUs", s[0] > 0 ? s[2] / s[0] : 0));
        }
        ops.sort((a, b) -> Double.compare(Json.num(Json.obj(b).get("totalUs"), 0), Json.num(Json.obj(a).get("totalUs"), 0)));
        return Json.map("ops", ops, "deterministic", deterministic);
    }
}
