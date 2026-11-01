package com.qualcomm.hardware.gobilda;

import com.qualcomm.robotcore.hardware.HardwareDevice;

import org.firstinspires.ftc.robotcore.external.navigation.AngleUnit;
import org.firstinspires.ftc.robotcore.external.navigation.DistanceUnit;
import org.firstinspires.ftc.robotcore.external.navigation.Pose2D;
import org.firstinspires.ftc.robotcore.external.navigation.Quaternion;
import org.firstinspires.ftc.robotcore.external.navigation.UnnormalizedAngleUnit;
import org.gameplan.runtime.api.DeviceInfo;
import org.gameplan.runtime.api.Sim;

/**
 * GamePlan shadow of the goBILDA Pinpoint odometry computer driver (SDK 12.0 API). update() is one
 * I2C bulk read; pose comes from the host's dead-wheel model (pod slip, scale error, IMU drift).
 * Offsets/resolution set by robot code are recorded; the physical pod geometry comes from the
 * robot description.
 */
public class GoBildaPinpointDriver implements HardwareDevice {
    public enum EncoderDirection { FORWARD, REVERSED }
    public enum GoBildaOdometryPods { goBILDA_SWINGARM_POD, goBILDA_4_BAR_POD }
    public enum DeviceStatus { NOT_READY, READY, CALIBRATING, FAULT_X_POD_NOT_DETECTED, FAULT_Y_POD_NOT_DETECTED, FAULT_NO_PODS_DETECTED, FAULT_IMU_RUNAWAY, FAULT_BAD_READ }
    public enum Register { DEVICE_ID, DEVICE_VERSION, DEVICE_STATUS, DEVICE_CONTROL, LOOP_TIME, X_ENCODER_VALUE, Y_ENCODER_VALUE, X_POSITION, Y_POSITION,
        H_ORIENTATION, X_VELOCITY, Y_VELOCITY, H_VELOCITY, MM_PER_TICK, X_POD_OFFSET, Y_POD_OFFSET, YAW_SCALAR, BULK_READ, QUATERNION_W, QUATERNION_X,
        QUATERNION_Y, QUATERNION_Z, PITCH, ROLL, SET_BULK_READ }
    public enum ReadData { ONLY_UPDATE_HEADING }
    public enum ErrorDetectionType { NONE, CRC, LOCAL_TEST }

    private final DeviceInfo info;
    private double x, y, h, vx, vy, vh, xOff, yOff, offX, offY, offH;
    private int encX, encY;
    private float yawScalar = 1;
    private double lastUpdate = -1;

    public GoBildaPinpointDriver(DeviceInfo info) { this.info = info; }

    public void update() {
        x = Sim.readI2c(info.index, 1, "pinpoint.update") / 1000.0;
        y = Sim.peek(info.index, 2) / 1000.0;
        h = Sim.peek(info.index, 3);
        vx = Sim.peek(info.index, 4) / 1000.0;
        vy = Sim.peek(info.index, 5) / 1000.0;
        vh = Sim.peek(info.index, 6);
        encX = (int) Sim.peek(info.index, 7);
        encY = (int) Sim.peek(info.index, 8);
        lastUpdate = Sim.now();
    }
    public void update(ReadData d) { update(); }
    public void setBulkReadScope(Register... r) {}
    public void setErrorDetectionType(ErrorDetectionType t) {}
    public void setOffsets(double xo, double yo, DistanceUnit u) { xOff = u.toMm(xo); yOff = u.toMm(yo); Sim.charge("pinpoint.update"); }
    public void recalibrateIMU() { Sim.charge("pinpoint.update"); }
    public void resetPosAndIMU() { update(); offX = x; offY = y; offH = h; }
    public void setEncoderDirections(EncoderDirection xd, EncoderDirection yd) { Sim.charge("pinpoint.update"); }
    public void setEncoderResolution(GoBildaOdometryPods p) { Sim.charge("pinpoint.update"); }
    public void setEncoderResolution(double ticksPerUnit, DistanceUnit u) { Sim.charge("pinpoint.update"); }
    public void setYawScalar(double s) { yawScalar = (float) s; }
    public void setPosition(Pose2D p) {
        update();
        offX = x - p.getX(DistanceUnit.METER);
        offY = y - p.getY(DistanceUnit.METER);
        offH = h - p.getHeading(AngleUnit.RADIANS);
    }
    public void setPosX(double v, DistanceUnit u) { update(); offX = x - u.toMeters(v); }
    public void setPosY(double v, DistanceUnit u) { update(); offY = y - u.toMeters(v); }
    public void setHeading(double v, AngleUnit u) { update(); offH = h - u.toRadians(v); }
    public int getDeviceID() { return 1; }
    public int getDeviceVersion() { return 3; }
    public float getYawScalar() { return yawScalar; }
    public DeviceStatus getDeviceStatus() { return Sim.peek(info.index, 1) == 0 && lastUpdate < 0 ? DeviceStatus.NOT_READY : DeviceStatus.READY; }
    public int getLoopTime() { return 667; }
    public double getFrequency() { return 1500; }
    public int getEncoderX() { return encX; }
    public int getEncoderY() { return encY; }
    private double rx() { return x - offX; }
    private double ry() { return y - offY; }
    private double rh() { double a = h - offH; while (a > Math.PI) a -= 2 * Math.PI; while (a <= -Math.PI) a += 2 * Math.PI; return a; }
    public double getPosX(DistanceUnit u) { return u.fromMeters(rx()); }
    public double getPosY(DistanceUnit u) { return u.fromMeters(ry()); }
    public double getHeading(AngleUnit u) { return u.fromRadians(rh()); }
    public double getHeading(UnnormalizedAngleUnit u) { return u.fromRadians(h - offH); }
    public double getVelX(DistanceUnit u) { return u.fromMeters(vx); }
    public double getVelY(DistanceUnit u) { return u.fromMeters(vy); }
    public double getHeadingVelocity(UnnormalizedAngleUnit u) { return u.fromRadians(vh); }
    public float getXOffset(DistanceUnit u) { return (float) u.fromMm(xOff); }
    public float getYOffset(DistanceUnit u) { return (float) u.fromMm(yOff); }
    public Pose2D getPosition() { return new Pose2D(DistanceUnit.METER, rx(), ry(), AngleUnit.RADIANS, rh()); }
    public Quaternion getQuaternion() {
        double a = rh() / 2;
        return new Quaternion((float) Math.cos(a), 0f, 0f, (float) Math.sin(a), (long) (Sim.now() * 1e9));
    }
    public double getPitch(AngleUnit u) { return 0; }
    public double getRoll(AngleUnit u) { return 0; }
    @Override public Manufacturer getManufacturer() { return Manufacturer.Other; }
    @Override public String getDeviceName() { return "goBILDA Pinpoint Odometry Computer (GamePlan simulated)"; }
    @Override public String getConnectionInfo() { return info.hub + "; I2C bus " + info.port; }
    @Override public int getVersion() { return 1; }
    @Override public void resetDeviceConfigurationForOpMode() {}
    @Override public void close() {}
}
