package com.qualcomm.robotcore.hardware;

import org.gameplan.runtime.api.DeviceInfo;
import org.gameplan.runtime.api.Sim;
import org.gameplan.vsdk.Stubs;

/** GamePlan shadow of CRServoImpl: power [-1,1] -> servo position [0,1] -> PWM pulse, as the SDK maps it. */
public class CRServoImpl implements CRServo {
    protected final DeviceInfo info;
    protected final int dev;
    protected Direction direction = Direction.FORWARD;
    protected double power = 0;
    protected PwmControl.PwmRange range = PwmControl.PwmRange.defaultRange;
    protected boolean pwmEnabled = false;

    public CRServoImpl(DeviceInfo info) { this.info = info; this.dev = info.index; }
    public CRServoImpl(ServoController c, int port) { throw new UnsupportedOperationException("construct CR servos via hardwareMap in the simulator"); }
    public CRServoImpl(ServoController c, int port, Direction d) { this(c, port); }

    @Override public ServoController getController() { return Stubs.proxy(ServoController.class, info); }
    @Override public int getPortNumber() { return info.port; }
    @Override public synchronized void setDirection(Direction d) { direction = d; }
    @Override public Direction getDirection() { return direction; }
    @Override public synchronized void setPower(double p) {
        p = Math.max(-1, Math.min(1, p));
        power = p;
        double signed = direction == Direction.REVERSE ? -p : p;
        double pos = signed * 0.5 + 0.5;
        double pulse = range.usPulseLower + pos * (range.usPulseUpper - range.usPulseLower);
        Sim.write(dev, 1, pulse, "crservo.setPower", true);
        if (!pwmEnabled) { pwmEnabled = true; Sim.write(dev, 2, 1, "servo.setPwmEnable", false); }
    }
    @Override public double getPower() { Sim.charge("crservo.getPower"); return power; }
    @Override public Manufacturer getManufacturer() { return Manufacturer.Lynx; }
    @Override public String getDeviceName() { return "GamePlan simulated CR servo (" + info.component + ")"; }
    @Override public String getConnectionInfo() { return info.hub + "; servo port " + info.port; }
    @Override public int getVersion() { return 1; }
    @Override public void resetDeviceConfigurationForOpMode() { direction = Direction.FORWARD; }
    @Override public void close() {}
}
