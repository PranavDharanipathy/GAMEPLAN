package org.gameplan.runtime.api;

/** Targets of rewritten process-creation calls in robot code: always denied inside the simulator sandbox. */
public final class Deny {
    private Deny() {}
    public static Process start(ProcessBuilder pb) { throw new SecurityException("process creation is not permitted for robot code in the simulator: " + pb.command()); }
    public static Process exec(Runtime r, String cmd) { throw new SecurityException("process creation is not permitted for robot code in the simulator: " + cmd); }
    public static Process exec(Runtime r, String[] cmd) { throw new SecurityException("process creation is not permitted for robot code in the simulator"); }
    public static void halt(Runtime r, int code) { throw new OpModeKilledError("Runtime.halt(" + code + ") called by robot code"); }
}
