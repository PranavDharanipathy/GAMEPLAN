package org.gameplan.runtime.api;

/**
 * Implemented by the shadow OpModeInternal so the bridge (parent class loader) can drive OpModes
 * that live in the sandboxed child class loader.
 */
public interface Lifecycle {
    void gpAttach(Object hardwareMap, Object telemetry);
    boolean gpIsLinear();
    /** Run the whole OpMode lifecycle on the calling (OpMode) thread until STOP completes. */
    void gpRun() throws Throwable;
}
