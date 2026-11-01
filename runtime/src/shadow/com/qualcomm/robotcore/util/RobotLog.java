package com.qualcomm.robotcore.util;

import org.gameplan.runtime.api.Sim;

/** GamePlan shadow of RobotLog (Android logcat in the real SDK) routed to the simulator log. */
public class RobotLog {
    public static final String TAG = "RobotCore";
    public static void v(String m) { Sim.log("VERBOSE", m); }
    public static void v(String f, Object... a) { Sim.log("VERBOSE", String.format(f, a)); }
    public static void vv(String tag, String m) { Sim.log("VERBOSE", tag + ": " + m); }
    public static void vv(String tag, String f, Object... a) { Sim.log("VERBOSE", tag + ": " + String.format(f, a)); }
    public static void d(String m) { Sim.log("DEBUG", m); }
    public static void d(String f, Object... a) { Sim.log("DEBUG", String.format(f, a)); }
    public static void dd(String tag, String m) { Sim.log("DEBUG", tag + ": " + m); }
    public static void dd(String tag, String f, Object... a) { Sim.log("DEBUG", tag + ": " + String.format(f, a)); }
    public static void i(String m) { Sim.log("INFO", m); }
    public static void i(String f, Object... a) { Sim.log("INFO", String.format(f, a)); }
    public static void ii(String tag, String m) { Sim.log("INFO", tag + ": " + m); }
    public static void ii(String tag, String f, Object... a) { Sim.log("INFO", tag + ": " + String.format(f, a)); }
    public static void w(String m) { Sim.log("WARN", m); }
    public static void w(String f, Object... a) { Sim.log("WARN", String.format(f, a)); }
    public static void ww(String tag, String m) { Sim.log("WARN", tag + ": " + m); }
    public static void ww(String tag, String f, Object... a) { Sim.log("WARN", tag + ": " + String.format(f, a)); }
    public static void ww(String tag, Throwable t, String m) { Sim.log("WARN", tag + ": " + m + " " + t); }
    public static void e(String m) { Sim.log("ERROR", m); }
    public static void e(String f, Object... a) { Sim.log("ERROR", String.format(f, a)); }
    public static void ee(String tag, String m) { Sim.log("ERROR", tag + ": " + m); }
    public static void ee(String tag, String f, Object... a) { Sim.log("ERROR", tag + ": " + String.format(f, a)); }
    public static void ee(String tag, Throwable t, String m) { Sim.log("ERROR", tag + ": " + m + " " + t); }
    public static void logStackTrace(Throwable t) { Sim.log("ERROR", String.valueOf(t)); }
    public static void addGlobalWarningMessage(String m) { Sim.log("WARN", "global warning: " + m); }
    public static void setGlobalErrorMsg(String m) { Sim.log("ERROR", "global error: " + m); }
}
