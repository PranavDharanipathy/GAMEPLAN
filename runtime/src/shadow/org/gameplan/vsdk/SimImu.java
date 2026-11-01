package org.gameplan.vsdk;

import com.qualcomm.robotcore.hardware.IMU;

import org.firstinspires.ftc.robotcore.external.navigation.AngleUnit;
import org.firstinspires.ftc.robotcore.external.navigation.AngularVelocity;
import org.firstinspires.ftc.robotcore.external.navigation.AxesOrder;
import org.firstinspires.ftc.robotcore.external.navigation.AxesReference;
import org.firstinspires.ftc.robotcore.external.navigation.Orientation;
import org.firstinspires.ftc.robotcore.external.navigation.Quaternion;
import org.firstinspires.ftc.robotcore.external.navigation.YawPitchRollAngles;
import org.gameplan.runtime.api.DeviceInfo;
import org.gameplan.runtime.api.Sim;

/**
 * Simulated Control Hub IMU (BHI260AP). Values come from the host sensor model (bias, bias walk,
 * scale error, noise, 100 Hz fused output). Assumes the RevHubOrientationOnRobot passed to
 * initialize() matches the physical mounting in the robot description.
 */
public class SimImu extends SimDevice implements IMU {
    private double yawOffset = 0;
    public SimImu(DeviceInfo i) { super(i); }

    @Override public boolean initialize(Parameters p) { Sim.charge("imu.getRobotYawPitchRollAngles"); return true; }
    @Override public void resetYaw() { yawOffset = Sim.readI2c(dev, 1, "imu.getRobotYawPitchRollAngles"); }
    private double yaw() { return wrap(Sim.readI2c(dev, 1, "imu.getRobotYawPitchRollAngles") - yawOffset); }
    private static double wrap(double a) { while (a > Math.PI) a -= 2 * Math.PI; while (a <= -Math.PI) a += 2 * Math.PI; return a; }

    @Override public YawPitchRollAngles getRobotYawPitchRollAngles() {
        double y = yaw(), p = Sim.peek(dev, 2), r = Sim.peek(dev, 3);
        return new YawPitchRollAngles(AngleUnit.DEGREES, Math.toDegrees(y), Math.toDegrees(p), Math.toDegrees(r), (long) (Sim.now() * 1e9));
    }
    @Override public Orientation getRobotOrientation(AxesReference ref, AxesOrder order, AngleUnit unit) {
        double y = yaw(), p = Sim.peek(dev, 2), r = Sim.peek(dev, 3);
        Orientation o = new Orientation(AxesReference.INTRINSIC, AxesOrder.ZYX, AngleUnit.RADIANS, (float) y, (float) p, (float) r, (long) (Sim.now() * 1e9));
        return o.toAxesReference(ref).toAxesOrder(order).toAngleUnit(unit);
    }
    @Override public Quaternion getRobotOrientationAsQuaternion() {
        double y = yaw() / 2, p = Sim.peek(dev, 2) / 2, r = Sim.peek(dev, 3) / 2;
        double cy = Math.cos(y), sy = Math.sin(y), cp = Math.cos(p), sp = Math.sin(p), cr = Math.cos(r), sr = Math.sin(r);
        return new Quaternion((float) (cr * cp * cy + sr * sp * sy), (float) (sr * cp * cy - cr * sp * sy), (float) (cr * sp * cy + sr * cp * sy),
                              (float) (cr * cp * sy - sr * sp * cy), (long) (Sim.now() * 1e9));
    }
    @Override public AngularVelocity getRobotAngularVelocity(AngleUnit unit) {
        double z = Sim.readI2c(dev, 4, "imu.getRobotAngularVelocity"), y = Sim.peek(dev, 5), x = Sim.peek(dev, 6);
        double k = unit == AngleUnit.DEGREES ? 180.0 / Math.PI : 1.0;
        return new AngularVelocity(unit, (float) (x * k), (float) (y * k), (float) (z * k), (long) (Sim.now() * 1e9));
    }
    @Override public Manufacturer getManufacturer() { return Manufacturer.Lynx; }
}
