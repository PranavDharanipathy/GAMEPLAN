package com.qualcomm.robotcore.hardware;

import com.qualcomm.robotcore.hardware.configuration.typecontainers.ServoConfigurationType;

import org.gameplan.runtime.api.DeviceInfo;
import org.gameplan.runtime.api.Sim;

public class ServoImplEx extends ServoImpl implements PwmControl {
    public ServoImplEx(DeviceInfo info) { super(info); }
    public ServoImplEx(ServoControllerEx c, int port, ServoConfigurationType t) { super(null, port); }
    public ServoImplEx(ServoControllerEx c, int port, Direction d, ServoConfigurationType t) { super(null, port); }
    @Override public synchronized void setPwmRange(PwmRange r) { range = r; if (!Double.isNaN(position)) setPosition(position); }
    @Override public PwmRange getPwmRange() { return range; }
    @Override public synchronized void setPwmEnable() { pwmEnabled = true; Sim.write(dev, 2, 1, "servo.setPwmEnable", false); }
    @Override public synchronized void setPwmDisable() { pwmEnabled = false; Sim.write(dev, 2, 0, "servo.setPwmEnable", false); }
    @Override public boolean isPwmEnabled() { Sim.chargeDevice(dev, "servo.isPwmEnabled"); return pwmEnabled; }
    @Override public synchronized void setPulseWidth(double us) {
        Sim.write(dev, 1, us, "servo.setPosition", true);
        if (!pwmEnabled) { pwmEnabled = true; Sim.write(dev, 2, 1, "servo.setPwmEnable", false); }
    }
    @Override public double getPulseWidth() {
        return Double.isNaN(position) ? 0 : range.usPulseLower + position * (range.usPulseUpper - range.usPulseLower);
    }
}
