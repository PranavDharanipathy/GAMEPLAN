package org.firstinspires.ftc.teamcode;

import com.qualcomm.hardware.lynx.LynxModule;
import com.qualcomm.robotcore.eventloop.opmode.OpMode;
import com.qualcomm.robotcore.eventloop.opmode.TeleOp;
import com.qualcomm.robotcore.hardware.DcMotor;
import com.qualcomm.robotcore.hardware.DcMotorEx;
import com.qualcomm.robotcore.hardware.DcMotorSimple;
import com.qualcomm.robotcore.hardware.Servo;
import com.qualcomm.robotcore.hardware.VoltageSensor;
import com.qualcomm.robotcore.util.ElapsedTime;

/** Ordinary iterative TeleOp: field-unaware mecanum drive, intake, flywheel, gate servo. */
@TeleOp(name = "Mecanum TeleOp", group = "fixture")
public class MecanumTeleOp extends OpMode {
    private DcMotor lf, rf, lb, rb, intake;
    private DcMotorEx shooter;
    private Servo gate, hood;
    private VoltageSensor battery;
    private final ElapsedTime loopTimer = new ElapsedTime();

    @Override public void init() {
        for (LynxModule hub : hardwareMap.getAll(LynxModule.class)) hub.setBulkCachingMode(LynxModule.BulkCachingMode.AUTO);
        lf = hardwareMap.get(DcMotor.class, "lf");
        rf = hardwareMap.get(DcMotor.class, "rf");
        lb = hardwareMap.dcMotor.get("lb");
        rb = hardwareMap.dcMotor.get("rb");
        intake = hardwareMap.get(DcMotor.class, "intake");
        shooter = hardwareMap.get(DcMotorEx.class, "shooter");
        gate = hardwareMap.get(Servo.class, "gate");
        hood = hardwareMap.get(Servo.class, "hood");
        battery = hardwareMap.voltageSensor.iterator().next();
        rf.setDirection(DcMotorSimple.Direction.REVERSE);
        rb.setDirection(DcMotorSimple.Direction.REVERSE);
        for (DcMotor m : new DcMotor[]{lf, rf, lb, rb}) m.setZeroPowerBehavior(DcMotor.ZeroPowerBehavior.BRAKE);
        shooter.setMode(DcMotor.RunMode.RUN_USING_ENCODER);
        telemetry.addData("status", "initialised");
    }

    @Override public void loop() {
        double y = -gamepad1.left_stick_y, x = gamepad1.left_stick_x, r = gamepad1.right_stick_x;
        double d = Math.max(1.0, Math.abs(y) + Math.abs(x) + Math.abs(r));
        // our wiring: rf and rb reversed, so all motors use the standard mecanum mixing
        lf.setPower((y + x + r) / d);
        rf.setPower((y - x - r) / d);
        lb.setPower((y - x + r) / d);
        rb.setPower((y + x - r) / d);
        intake.setPower(gamepad1.right_trigger > 0.1 ? 1.0 : (gamepad1.left_trigger > 0.1 ? -1.0 : 0.0));
        shooter.setVelocity(gamepad1.right_bumper ? 2000 : 0);
        gate.setPosition(gamepad1.a ? 1.0 : 0.0);
        hood.setPosition(0.5);
        telemetry.addData("loop ms", "%.1f", loopTimer.milliseconds());
        telemetry.addData("lf ticks", lf.getCurrentPosition());
        telemetry.addData("shooter tps", "%.0f", shooter.getVelocity());
        telemetry.addData("battery V", "%.2f", battery.getVoltage());
        loopTimer.reset();
    }
}
