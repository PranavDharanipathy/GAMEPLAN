package android.os;

import org.gameplan.runtime.api.Sim;

/**
 * Minimal android.os.Process. getElapsedCpuTime() reports VIRTUAL elapsed time (as if the robot
 * program kept one core busy), so it is deterministic; it is not a measurement of Control Hub CPU load.
 */
public class Process {
    public static long getElapsedCpuTime() { return Sim.uptimeMillis(); }
    public static int myPid() { return 1000; }
    public static int myTid() { return (int) Thread.currentThread().getId(); }
    public static int myUid() { return 10000; }
    public static void setThreadPriority(int p) {}
    public static final int THREAD_PRIORITY_BACKGROUND = 10;
    public static final int THREAD_PRIORITY_DEFAULT = 0;
}
