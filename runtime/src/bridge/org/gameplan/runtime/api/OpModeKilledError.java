package org.gameplan.runtime.api;

/** Thrown into robot code that keeps running after STOP + the stuck-detection limit (mirrors the RC force-stop). */
public final class OpModeKilledError extends Error {
    public OpModeKilledError(String m) { super(m); }
}
