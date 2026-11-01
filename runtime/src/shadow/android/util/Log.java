package android.util;

import org.gameplan.runtime.api.Sim;

/** android.util.Log routed to the simulator log (robot code commonly logs through it). */
public final class Log {
    private Log() {}
    public static int v(String t, String m) { Sim.log("VERBOSE", t + ": " + m); return 0; }
    public static int d(String t, String m) { Sim.log("DEBUG", t + ": " + m); return 0; }
    public static int i(String t, String m) { Sim.log("INFO", t + ": " + m); return 0; }
    public static int w(String t, String m) { Sim.log("WARN", t + ": " + m); return 0; }
    public static int e(String t, String m) { Sim.log("ERROR", t + ": " + m); return 0; }
    public static int e(String t, String m, Throwable e) { Sim.log("ERROR", t + ": " + m + " " + e); return 0; }
}
