package com.qualcomm.robotcore.hardware;

import com.qualcomm.robotcore.hardware.configuration.typecontainers.ServoConfigurationType;

import org.gameplan.runtime.api.DeviceInfo;
import org.gameplan.runtime.api.Sim;
import org.gameplan.vsdk.Stubs;

/** GamePlan shadow of ServoImpl: position -> scaled range -> PWM pulse (default 600-2400 us), as the SDK computes. */
public class ServoImpl implements Servo {
    protected final DeviceInfo info;
    protected final int dev;
    protected Direction direction = Direction.FORWARD;
    protected double position = Double.NaN, min = 0, max = 1;
    protected PwmControl.PwmRange range = PwmControl.PwmRange.defaultRange;
    protected boolean pwmEnabled = false;

    public ServoImpl(DeviceInfo info) { this.info = info; this.dev = info.index; }
    public ServoImpl(ServoController c, int port) { throw new UnsupportedOperationException("construct servos via hardwareMap in the simulator"); }
    public ServoImpl(ServoController c, int port, Direction d) { this(c, port); }

    @Override public ServoController getController() { return Stubs.proxy(ServoController.class, info); }
    @Override public int getPortNumber() { return info.port; }
    @Override public synchronized void setDirection(Direction d) { direction = d; }
    @Override public Direction getDirection() { return direction; }
    @Override public synchronized void setPosition(double p) {
        p = Math.max(0, Math.min(1, p));
        position = p;
        double scaled = min + (max - min) * (direction == Direction.REVERSE ? 1 - p : p);
        double pulse = range.usPulseLower + scaled * (range.usPulseUpper - range.usPulseLower);
        Sim.write(dev, 1, pulse, "servo.setPosition", true);
        if (!pwmEnabled) { pwmEnabled = true; Sim.write(dev, 2, 1, "servo.setPwmEnable", false); }
    }
    @Override public double getPosition() { Sim.charge("servo.getPosition"); return position; }
    @Override public synchronized void scaleRange(double lo, double hi) {
        if (lo < 0 || hi > 1 || lo >= hi) throw new IllegalArgumentException("scaleRange: invalid range");
        min = lo; max = hi;
    }
    @Override public Manufacturer getManufacturer() { return Manufacturer.Lynx; }
    @Override public String getDeviceName() { return "GamePlan simulated servo (" + info.component + ")"; }
    @Override public String getConnectionInfo() { return info.hub + "; servo port " + info.port; }
    @Override public int getVersion() { return 1; }
    @Override public void resetDeviceConfigurationForOpMode() { direction = Direction.FORWARD; min = 0; max = 1; }
    @Override public void close() {}
}
