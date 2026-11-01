package org.gameplan.runtime.api;

import java.util.List;

/**
 * Static facade used by shadow SDK classes and by rewritten robot code
 * (System.nanoTime/currentTimeMillis/Thread.sleep are redirected here).
 * All time is VIRTUAL simulation time.
 */
public final class Sim {
    private Sim() {}
    public interface Backend {
        double now();
        void charge(String op, double extraUs);
        double readBulk(int dev, int field, String op);
        double readDirect(int dev, int field, String op);
        double readI2c(int dev, int field, String op);
        double peek(int dev, int field);
        void write(int dev, int field, double v, String op, boolean skipIfUnchanged);
        void sleep(double seconds);
        List<DeviceInfo> devices();
        void setBulkMode(int hubDevice, int mode);
        int getBulkMode(int hubDevice);
        void clearBulkCache(int hubDevice);
        GamepadData gamepad(int n);
        boolean gamepadChanged(int n, long[] seq);
        void telemetry(List<String> lines, int intervalMs);
        String vision(int dev);
        void log(String level, String msg);
        boolean startRequested();
        boolean stopRequested();
        boolean inInit();
        void loopBoundary();
        void userCompute();
        void checkKilled();
        String sdkVersion();
        void cycleEnd(double overheadUs);
        void chargeDevice(int dev, String op);
    }
    private static volatile Backend b;
    public static void install(Backend backend) { b = backend; }
    public static Backend backend() { return b; }

    // --- time (rewritten JDK calls land here) ---
    public static long nanoTime() { b.charge("time.read", 0); return (long) (b.now() * 1e9); }
    public static long currentTimeMillis() { b.charge("time.read", 0); return 1_700_000_000_000L + (long) (b.now() * 1e3); }
    public static void sleep(long ms) throws InterruptedException { sleepImpl(ms / 1000.0); }
    public static void sleep(long ms, int nanos) throws InterruptedException { sleepImpl(ms / 1000.0 + nanos / 1e9); }
    public static void sleep(java.time.Duration d) throws InterruptedException { sleepImpl(d.toNanos() / 1e9); }
    private static void sleepImpl(double s) throws InterruptedException {
        b.sleep(s);
        if (Thread.currentThread().isInterrupted()) { Thread.interrupted(); throw new InterruptedException("sleep interrupted (OpMode stop)"); }
    }
    public static long uptimeMillis() { b.charge("time.read", 0); return (long) (b.now() * 1e3); }
    public static void onSpinWait() { b.charge("time.read", 1.0); }
    public static void yieldThread() { b.charge("time.read", 1.0); }
    public static double now() { return b.now(); }
    public static void exit(int code) { b.log("ERROR", "robot code called System.exit(" + code + ") - ignored in simulation (it would kill the Robot Controller app)"); throw new OpModeKilledError("System.exit(" + code + ") called by robot code"); }

    // --- hardware ---
    public static void charge(String op) { b.charge(op, 0); }
    public static double readBulk(int dev, int field, String op) { return b.readBulk(dev, field, op); }
    public static double readDirect(int dev, int field, String op) { return b.readDirect(dev, field, op); }
    public static double readI2c(int dev, int field, String op) { return b.readI2c(dev, field, op); }
    public static double peek(int dev, int field) { return b.peek(dev, field); }
    public static void write(int dev, int field, double v, String op, boolean skipIfUnchanged) { b.write(dev, field, v, op, skipIfUnchanged); }
    public static List<DeviceInfo> devices() { return b.devices(); }
    public static DeviceInfo device(String name) {
        for (DeviceInfo d : b.devices()) if (d.name.equals(name)) return d;
        return null;
    }
    public static void setBulkMode(int hub, int mode) { b.setBulkMode(hub, mode); }
    public static int getBulkMode(int hub) { return b.getBulkMode(hub); }
    public static void clearBulkCache(int hub) { b.clearBulkCache(hub); }
    public static GamepadData gamepad(int n) { return b.gamepad(n); }
    public static void telemetry(List<String> lines, int intervalMs) { b.telemetry(lines, intervalMs); }
    public static String vision(int dev) { return b.vision(dev); }
    public static void log(String level, String msg) { b.log(level, msg); }
    public static boolean startRequested() { return b.startRequested(); }
    public static boolean stopRequested() { return b.stopRequested(); }
    public static boolean inInit() { return b.inInit(); }
    public static void loopBoundary() { b.loopBoundary(); }
    public static void userCompute() { b.userCompute(); }
    public static void checkKilled() { b.checkKilled(); }
    public static String sdkVersion() { return b.sdkVersion(); }
    /** End of one event-loop cycle: charge framework overhead and exchange with the host. */
    public static void cycleEnd() { b.cycleEnd(-1); }
    public static void cycleEnd(double overheadUs) { b.cycleEnd(overheadUs); }
    /** A Lynx transaction on the device's hub whose result is not a sensor value (e.g. DcMotor.getPower()). */
    public static void chargeDevice(int dev, String op) { b.chargeDevice(dev, op); }
}
