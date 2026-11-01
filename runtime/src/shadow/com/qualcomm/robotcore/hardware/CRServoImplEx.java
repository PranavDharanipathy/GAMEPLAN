package com.qualcomm.robotcore.hardware;

import com.qualcomm.robotcore.hardware.configuration.typecontainers.ServoConfigurationType;

import org.gameplan.runtime.api.DeviceInfo;
import org.gameplan.runtime.api.Sim;

public class CRServoImplEx extends CRServoImpl implements PwmControl {
    public CRServoImplEx(DeviceInfo info) { super(info); }
    public CRServoImplEx(ServoControllerEx c, int port, ServoConfigurationType t) { super(null, port); }
    public CRServoImplEx(ServoControllerEx c, int port, Direction d, ServoConfigurationType t) { super(null, port); }
    @Override public void setPwmRange(PwmRange r) { range = r; }
    @Override public PwmRange getPwmRange() { return range; }
    @Override public synchronized void setPwmEnable() { pwmEnabled = true; Sim.write(dev, 2, 1, "servo.setPwmEnable", false); }
    @Override public synchronized void setPwmDisable() { pwmEnabled = false; Sim.write(dev, 2, 0, "servo.setPwmEnable", false); }
    @Override public boolean isPwmEnabled() { return pwmEnabled; }
    @Override public double getPulseWidth() { return range.usPulseLower + (power * 0.5 + 0.5) * (range.usPulseUpper - range.usPulseLower); }
    @Override public synchronized void setPulseWidth(double us) { Sim.write(dev, 1, us, "crservo.setPower", true); }
}
