package com.qualcomm.robotcore.hardware;

import com.qualcomm.robotcore.hardware.configuration.typecontainers.MotorConfigurationType;

import org.gameplan.runtime.api.DeviceInfo;
import org.gameplan.runtime.api.Sim;
import org.gameplan.vsdk.Stubs;

/**
 * GamePlan shadow of DcMotorImpl. Mirrors SDK semantics: direction is applied host-side by the SDK
 * (power, target position, encoder position and velocity are sign-flipped for REVERSE);
 * setPower() is skipped when unchanged; encoder/velocity/busy are bulk-readable.
 */
public class DcMotorImpl implements DcMotor {
    protected final DeviceInfo info;
    protected final int dev;
    protected Direction direction = Direction.FORWARD;
    protected RunMode mode = RunMode.RUN_WITHOUT_ENCODER;
    protected ZeroPowerBehavior zpb = ZeroPowerBehavior.BRAKE;
    protected double lastPower = 0;
    protected int targetPosition = 0;
    protected MotorConfigurationType motorType;

    public DcMotorImpl(DeviceInfo info) {
        this.info = info;
        this.dev = info.index;
        this.motorType = Stubs.motorType(info);
    }
    public DcMotorImpl(DcMotorController c, int port) { throw new UnsupportedOperationException("construct motors via hardwareMap in the simulator"); }
    public DcMotorImpl(DcMotorController c, int port, Direction d) { this(c, port); }
    public DcMotorImpl(DcMotorController c, int port, Direction d, MotorConfigurationType t) { this(c, port); }

    protected int sign() { return direction == Direction.FORWARD ? 1 : -1; }

    @Override public MotorConfigurationType getMotorType() { return motorType; }
    @Override public void setMotorType(MotorConfigurationType t) { motorType = t; }
    @Override public DcMotorController getController() { return Stubs.proxy(DcMotorController.class, info); }
    @Override public int getPortNumber() { return info.port; }
    @Override public synchronized void setZeroPowerBehavior(ZeroPowerBehavior b) {
        if (b == ZeroPowerBehavior.UNKNOWN) return;
        zpb = b;
        Sim.write(dev, 5, b == ZeroPowerBehavior.BRAKE ? 1 : 0, "motor.setZeroPowerBehavior", true);
    }
    @Override public ZeroPowerBehavior getZeroPowerBehavior() { return zpb; }
    @Override @Deprecated public synchronized void setPowerFloat() { setZeroPowerBehavior(ZeroPowerBehavior.FLOAT); setPower(0); }
    @Override public boolean getPowerFloat() { return zpb == ZeroPowerBehavior.FLOAT && lastPower == 0; }
    @Override public synchronized void setTargetPosition(int position) {
        targetPosition = position;
        Sim.write(dev, 3, position * sign(), "motor.setTargetPosition", true);
    }
    @Override public int getTargetPosition() { return targetPosition; }
    @Override public boolean isBusy() { return Sim.readBulk(dev, 4, "motor.isBusy") != 0; }
    @Override public int getCurrentPosition() { return (int) Math.round(Sim.readBulk(dev, 1, "motor.getCurrentPosition")) * sign(); }
    @Override public synchronized void setMode(RunMode m) {
        m = m.migrate();
        mode = m;
        int code;
        switch (m) {
            case RUN_USING_ENCODER: code = 1; break;
            case RUN_TO_POSITION: code = 2; break;
            case STOP_AND_RESET_ENCODER: code = 3; break;
            default: code = 0;
        }
        Sim.write(dev, 2, code, "motor.setMode", false);
        if (m == RunMode.STOP_AND_RESET_ENCODER) { lastPower = 0; Sim.write(dev, 1, 0, "motor.setPower", true); }
    }
    @Override public RunMode getMode() { return mode; }
    @Override public synchronized void setDirection(Direction d) { direction = d; Sim.charge("motor.setDirection"); }
    @Override public Direction getDirection() { return direction; }
    @Override public synchronized void setPower(double p) {
        if (Double.isNaN(p)) throw new IllegalArgumentException("power is NaN");
        p = Math.max(-1, Math.min(1, p));
        lastPower = p;
        double sent = mode == RunMode.RUN_TO_POSITION ? Math.abs(p) : p * sign();
        Sim.write(dev, 1, sent, "motor.setPower", true);
    }
    @Override public double getPower() {
        Sim.chargeDevice(dev, "motor.getPower");   // a real Lynx transaction even though the value is the last command
        return lastPower;
    }
    @Override public Manufacturer getManufacturer() { return Manufacturer.Lynx; }
    @Override public String getDeviceName() { return "GamePlan simulated motor (" + info.component + ")"; }
    @Override public String getConnectionInfo() { return info.hub + "; port " + info.port; }
    @Override public int getVersion() { return 1; }
    @Override public void resetDeviceConfigurationForOpMode() { direction = Direction.FORWARD; }
    @Override public void close() {}
}
