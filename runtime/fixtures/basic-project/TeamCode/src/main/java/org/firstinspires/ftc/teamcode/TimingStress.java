package org.firstinspires.ftc.teamcode;

import com.qualcomm.robotcore.eventloop.opmode.OpMode;
import com.qualcomm.robotcore.eventloop.opmode.TeleOp;
import com.qualcomm.robotcore.hardware.DcMotorEx;
import com.qualcomm.robotcore.hardware.VoltageSensor;

import org.firstinspires.ftc.robotcore.external.navigation.CurrentUnit;

/** Deliberately expensive loop (no bulk caching, getCurrent, getPower, voltage) to show loop-time cost. */
@TeleOp(name = "Timing Stress", group = "fixture")
public class TimingStress extends OpMode {
    private DcMotorEx[] m;
    private VoltageSensor v;
    @Override public void init() {
        m = new DcMotorEx[]{hardwareMap.get(DcMotorEx.class, "lf"), hardwareMap.get(DcMotorEx.class, "rf"), hardwareMap.get(DcMotorEx.class, "lb"), hardwareMap.get(DcMotorEx.class, "rb")};
        v = hardwareMap.voltageSensor.iterator().next();
    }
    @Override public void loop() {
        double sum = 0;
        for (DcMotorEx x : m) { sum += x.getCurrentPosition() + x.getVelocity() + x.getCurrent(CurrentUnit.AMPS) + x.getPower(); x.setPower(0.3); }
        telemetry.addData("sum", sum + v.getVoltage());
    }
}
