package com.qualcomm.robotcore.eventloop.opmode;

import com.qualcomm.robotcore.hardware.Gamepad;
import com.qualcomm.robotcore.hardware.HardwareMap;

import org.firstinspires.ftc.robotcore.external.Telemetry;
import org.gameplan.runtime.api.GamepadData;
import org.gameplan.runtime.api.Lifecycle;
import org.gameplan.runtime.api.Sim;

/**
 * GamePlan shadow of the SDK's OpModeInternal (Android-bound in the real SDK). Same public
 * members; lifecycle is driven by the simulator's virtual Robot Controller event loop.
 */
abstract class OpModeInternal implements Lifecycle {
    public static final int MS_BEFORE_FORCE_STOP_AFTER_STOP_REQUESTED = 1000;
    public volatile Gamepad gamepad1 = new Gamepad();
    public volatile Gamepad gamepad2 = new Gamepad();
    public Telemetry telemetry;
    public volatile HardwareMap hardwareMap;
    public int msStuckDetectStop = 900;

    private final long[] seq1 = {-1}, seq2 = {-1};
    private final Gamepad scratch = new Gamepad();

    public final void requestOpModeStop() {
        Sim.log("INFO", "OpMode requested its own stop (requestOpModeStop)");
        gpSelfStop = true;
    }

    volatile boolean gpSelfStop;

    @Override public void gpAttach(Object hw, Object tel) {
        hardwareMap = (HardwareMap) hw;
        telemetry = (Telemetry) tel;
    }

    /** Copy Driver Station gamepad state into the real SDK Gamepad objects (copy() keeps edge detection). */
    void gpUpdateGamepads() {
        update(gamepad1, 1, seq1);
        update(gamepad2, 2, seq2);
    }

    private void update(Gamepad g, int n, long[] seq) {
        if (!Sim.backend().gamepadChanged(n, seq)) return;
        GamepadData d = Sim.gamepad(n);
        Gamepad s = scratch;
        s.copy(g);
        s.left_stick_x = d.lx; s.left_stick_y = d.ly; s.right_stick_x = d.rx; s.right_stick_y = d.ry;
        s.left_trigger = d.lt; s.right_trigger = d.rt;
        s.a = d.btn(0); s.b = d.btn(1); s.x = d.btn(2); s.y = d.btn(3);
        s.left_bumper = d.btn(4); s.right_bumper = d.btn(5);
        s.dpad_up = d.btn(6); s.dpad_down = d.btn(7); s.dpad_left = d.btn(8); s.dpad_right = d.btn(9);
        s.back = d.btn(10); s.start = d.btn(11); s.guide = d.btn(12);
        s.left_stick_button = d.btn(13); s.right_stick_button = d.btn(14); s.touchpad = d.btn(15);
        // PlayStation aliases
        s.cross = s.a; s.circle = s.b; s.square = s.x; s.triangle = s.y; s.share = s.back; s.options = s.start; s.ps = s.guide;
        s.left_trigger_pressed = s.left_trigger > s.getTriggerThreshold();
        s.right_trigger_pressed = s.right_trigger > s.getTriggerThreshold();
        g.copy(s);
    }
}
