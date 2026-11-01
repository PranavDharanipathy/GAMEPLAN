package com.qualcomm.robotcore.eventloop.opmode;

import java.util.HashMap;

import org.firstinspires.ftc.robotcore.external.Telemetry;
import org.gameplan.runtime.api.Sim;

/**
 * GamePlan shadow of com.qualcomm.robotcore.eventloop.opmode.OpMode (SDK 12.0 public API).
 * Iterative OpModes run init / init_loop... / start / loop... / stop exactly as the SDK event
 * loop calls them; telemetry is auto-updated after each call as in the SDK.
 */
public abstract class OpMode extends OpModeInternal {
    public static final HashMap<String, Object> blackboard = new HashMap<>();
    public volatile double time = 0;
    public int msStuckDetectInit = 5000;
    public int msStuckDetectInitLoop = 5000;
    public int msStuckDetectStart = 5000;
    public int msStuckDetectLoop = 5000;
    private double startTime = 0;

    public OpMode() {}

    public abstract void init();
    public void init_loop() {}
    public void start() {}
    public abstract void loop();
    public void stop() {}

    public final void terminateOpModeNow() { throw new TerminateOpModeNow(); }
    static final class TerminateOpModeNow extends RuntimeException {
        TerminateOpModeNow() { super("terminateOpModeNow()"); }
    }

    public double getRuntime() { return Sim.now() - startTime; }
    public void resetRuntime() { startTime = Sim.now(); }
    public void updateTelemetry(Telemetry t) { t.update(); }
    public final void internalUpdateTelemetryNow(com.qualcomm.robotcore.robocol.TelemetryMessage m) {}
    public void internalPreInit() {}
    public void internalPostInitLoop() { if (telemetry != null) telemetry.update(); }
    public void internalPostLoop() { if (telemetry != null) telemetry.update(); }

    @Override public boolean gpIsLinear() { return false; }

    private void tick() { time = getRuntime(); }

    @Override public void gpRun() throws Throwable {
        resetRuntime();
        gpUpdateGamepads();
        try {
            internalPreInit();
            Sim.loopBoundary();
            init();
            Sim.userCompute();
            internalPostInitLoop();
            Sim.cycleEnd();
            while (!Sim.startRequested() && !Sim.stopRequested() && !gpSelfStop) {
                Sim.loopBoundary();
                gpUpdateGamepads();
                tick();
                init_loop();
                Sim.userCompute();
                internalPostInitLoop();
                Sim.cycleEnd();
            }
            if (!Sim.stopRequested() && !gpSelfStop) {
                resetRuntime();
                gpUpdateGamepads();
                start();
                Sim.cycleEnd();
                while (!Sim.stopRequested() && !gpSelfStop) {
                    Sim.loopBoundary();
                    gpUpdateGamepads();
                    tick();
                    loop();
                    Sim.userCompute();
                    internalPostLoop();
                    Sim.cycleEnd();
                }
            }
        } catch (TerminateOpModeNow e) {
            Sim.log("INFO", "terminateOpModeNow() called");
        }
        stop();
        if (telemetry != null) telemetry.update();
    }
}
