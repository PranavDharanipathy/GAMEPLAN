package com.qualcomm.robotcore.eventloop.opmode;

import org.gameplan.runtime.api.Sim;

/**
 * GamePlan shadow of LinearOpMode (SDK 12.0 public API). runOpMode() runs on its own thread; STOP
 * sets isStopRequested(), interrupts sleeps, and force-stops after msStuckDetectStop.
 */
public abstract class LinearOpMode extends OpMode {
    public LinearOpMode() {}

    public abstract void runOpMode() throws InterruptedException;

    public void waitForStart() {
        while (!isStarted() && !isStopRequested()) {
            gpUpdateGamepads();
            Sim.cycleEnd();
        }
        resetRuntime();
    }

    public final void idle() {
        gpUpdateGamepads();
        Sim.charge("time.read");
    }

    public final void sleep(long milliseconds) {
        try {
            Sim.sleep(milliseconds);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        gpUpdateGamepads();
    }

    public final boolean opModeIsActive() {
        boolean active = !isStopRequested() && isStarted();
        if (active) {
            // one iteration of the user's control loop: record loop timing and the modelled user compute
            Sim.loopBoundary();
            Sim.userCompute();
            idle();
        }
        return active;
    }

    public final boolean opModeInInit() { return !isStarted() && !isStopRequested(); }
    public final boolean isStarted() { return Sim.startRequested(); }
    public final boolean isStopRequested() { return Sim.stopRequested() || gpSelfStop || Thread.currentThread().isInterrupted(); }

    public final void init() {}
    public final void init_loop() {}
    public final void start() {}
    public final void loop() {}
    public final void stop() {}

    @Override public boolean gpIsLinear() { return true; }

    @Override public void gpRun() throws Throwable {
        resetRuntime();
        gpUpdateGamepads();
        Sim.loopBoundary();
        try {
            runOpMode();
        } catch (InterruptedException e) {
            Sim.log("INFO", "runOpMode() exited via InterruptedException after STOP");
        } catch (TerminateOpModeNow e) {
            Sim.log("INFO", "terminateOpModeNow() called");
        }
        if (!isStopRequested()) Sim.log("INFO", "runOpMode() returned before STOP; the OpMode is finished and outputs are stopped");
    }
}
