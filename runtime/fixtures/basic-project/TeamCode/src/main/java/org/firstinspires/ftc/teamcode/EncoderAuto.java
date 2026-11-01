package org.firstinspires.ftc.teamcode;

import com.qualcomm.hardware.rev.RevHubOrientationOnRobot;
import com.qualcomm.robotcore.eventloop.opmode.Autonomous;
import com.qualcomm.robotcore.eventloop.opmode.LinearOpMode;
import com.qualcomm.robotcore.hardware.DcMotor;
import com.qualcomm.robotcore.hardware.DcMotorSimple;
import com.qualcomm.robotcore.hardware.IMU;
import com.qualcomm.robotcore.util.ElapsedTime;

import org.firstinspires.ftc.robotcore.external.navigation.AngleUnit;

/** Linear autonomous: RUN_TO_POSITION drive, IMU turn, then park in the LOADING ZONE. */
@Autonomous(name = "Encoder Auto", group = "fixture")
public class EncoderAuto extends LinearOpMode {
    private DcMotor lf, rf, lb, rb;
    private IMU imu;
    static final double TICKS_PER_M = 537.7 / (Math.PI * 0.104);

    @Override public void runOpMode() throws InterruptedException {
        lf = hardwareMap.get(DcMotor.class, "lf");
        rf = hardwareMap.get(DcMotor.class, "rf");
        lb = hardwareMap.get(DcMotor.class, "lb");
        rb = hardwareMap.get(DcMotor.class, "rb");
        rf.setDirection(DcMotorSimple.Direction.REVERSE);
        rb.setDirection(DcMotorSimple.Direction.REVERSE);
        imu = hardwareMap.get(IMU.class, "imu");
        imu.initialize(new IMU.Parameters(new RevHubOrientationOnRobot(RevHubOrientationOnRobot.LogoFacingDirection.UP,
                                                                       RevHubOrientationOnRobot.UsbFacingDirection.FORWARD)));
        imu.resetYaw();
        telemetry.addLine("ready");
        telemetry.update();
        waitForStart();
        drive(0.80, 0.6);
        turnTo(90);
        drive(0.60, 0.5);
        telemetry.addData("yaw", imu.getRobotYawPitchRollAngles().getYaw(AngleUnit.DEGREES));
        telemetry.update();
        while (opModeIsActive()) idle();
    }

    private void drive(double meters, double power) {
        int t = (int) Math.round(meters * TICKS_PER_M);
        for (DcMotor m : new DcMotor[]{lf, rf, lb, rb}) {
            m.setMode(DcMotor.RunMode.STOP_AND_RESET_ENCODER);
            m.setTargetPosition(t);
            m.setMode(DcMotor.RunMode.RUN_TO_POSITION);
            m.setPower(power);
        }
        ElapsedTime timeout = new ElapsedTime();
        while (opModeIsActive() && (lf.isBusy() || rf.isBusy()) && timeout.seconds() < 4) {
            telemetry.addData("lf", lf.getCurrentPosition());
            telemetry.update();
        }
        for (DcMotor m : new DcMotor[]{lf, rf, lb, rb}) { m.setPower(0); m.setMode(DcMotor.RunMode.RUN_USING_ENCODER); }
    }

    private void turnTo(double deg) {
        ElapsedTime timeout = new ElapsedTime();
        while (opModeIsActive() && timeout.seconds() < 3) {
            double err = deg - imu.getRobotYawPitchRollAngles().getYaw(AngleUnit.DEGREES);
            if (Math.abs(err) < 2) break;
            double p = Math.max(-0.5, Math.min(0.5, err * 0.02));
            lf.setPower(-p); lb.setPower(-p); rf.setPower(p); rb.setPower(p);
        }
        for (DcMotor m : new DcMotor[]{lf, rf, lb, rb}) m.setPower(0);
        sleep(200);
    }
}
