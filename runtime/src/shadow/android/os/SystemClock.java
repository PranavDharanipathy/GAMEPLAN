package android.os;

import org.gameplan.runtime.api.Sim;

/** android.os.SystemClock on the simulator's virtual clock (calls are also redirected at load time). */
public final class SystemClock {
    private SystemClock() {}
    public static void sleep(long ms) {
        try { Sim.sleep(ms); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
    }
    public static long uptimeMillis() { return Sim.uptimeMillis(); }
    public static long elapsedRealtime() { return Sim.uptimeMillis(); }
    public static long elapsedRealtimeNanos() { return Sim.nanoTime(); }
    public static long currentThreadTimeMillis() { return Sim.uptimeMillis(); }
}
