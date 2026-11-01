package com.qualcomm.robotcore.hardware;

import com.qualcomm.robotcore.hardware.configuration.typecontainers.MotorConfigurationType;

import org.firstinspires.ftc.robotcore.external.navigation.AngleUnit;
import org.firstinspires.ftc.robotcore.external.navigation.CurrentUnit;
import org.gameplan.runtime.api.DeviceInfo;
import org.gameplan.runtime.api.Sim;

/** GamePlan shadow of DcMotorImplEx (all motors in hardwareMap are this type, as on a REV hub). */
public class DcMotorImplEx extends DcMotorImpl implements DcMotorEx {
    private boolean enabled = true;
    private int tolerance = 5;
    private double currentAlertA = 0;
    private PIDFCoefficients velPidf = new PIDFCoefficients(10, 3, 0, 0);
    private PIDFCoefficients posPidf = new PIDFCoefficients(10, 0, 0, 0);

    public DcMotorImplEx(DeviceInfo info) { super(info); }
    public DcMotorImplEx(DcMotorController c, int port) { super(c, port); }
    public DcMotorImplEx(DcMotorController c, int port, Direction d) { super(c, port); }
    public DcMotorImplEx(DcMotorController c, int port, Direction d, MotorConfigurationType t) { super(c, port); }

    @Override public synchronized void setMotorEnable() { enabled = true; Sim.write(dev, 6, 1, "motor.setMotorEnable", false); }
    @Override public synchronized void setMotorDisable() { enabled = false; Sim.write(dev, 6, 0, "motor.setMotorEnable", false); }
    @Override public boolean isMotorEnabled() { Sim.chargeDevice(dev, "motor.isMotorEnabled"); return enabled; }
    @Override public synchronized void setVelocity(double ticksPerSecond) {
        Sim.write(dev, 4, ticksPerSecond * sign(), "motor.setVelocity", true);
    }
    @Override public synchronized void setVelocity(double v, AngleUnit unit) {
        double revPerSec = unit == AngleUnit.DEGREES ? v / 360.0 : v / (2 * Math.PI);
        setVelocity(revPerSec * info.ticksPerRev);
    }
    @Override public synchronized double getVelocity() { return Sim.readBulk(dev, 2, "motor.getVelocity") * sign(); }
    @Override public synchronized double getVelocity(AngleUnit unit) {
        double rev = getVelocity() / info.ticksPerRev;
        return unit == AngleUnit.DEGREES ? rev * 360.0 : rev * 2 * Math.PI;
    }
    @Override @Deprecated public void setPIDCoefficients(RunMode m, PIDCoefficients c) { setPIDFCoefficients(m, new PIDFCoefficients(c.p, c.i, c.d, 0)); }
    @Override public synchronized void setPIDFCoefficients(RunMode m, PIDFCoefficients c) {
        if (m == RunMode.RUN_TO_POSITION) { posPidf = new PIDFCoefficients(c); Sim.write(dev, 12, c.p, "motor.setPIDFCoefficients", false); return; }
        velPidf = new PIDFCoefficients(c);
        Sim.write(dev, 7, c.p, "motor.setPIDFCoefficients", false);
        Sim.write(dev, 8, c.i, "motor.setPIDFCoefficients", false);
        Sim.write(dev, 9, c.d, "motor.setPIDFCoefficients", false);
        Sim.write(dev, 10, c.f, "motor.setPIDFCoefficients", false);
    }
    @Override public void setVelocityPIDFCoefficients(double p, double i, double d, double f) { setPIDFCoefficients(RunMode.RUN_USING_ENCODER, new PIDFCoefficients(p, i, d, f)); }
    @Override public void setPositionPIDFCoefficients(double p) { setPIDFCoefficients(RunMode.RUN_TO_POSITION, new PIDFCoefficients(p, 0, 0, 0)); }
    @Override @Deprecated public PIDCoefficients getPIDCoefficients(RunMode m) { PIDFCoefficients c = getPIDFCoefficients(m); return new PIDCoefficients(c.p, c.i, c.d); }
    @Override public PIDFCoefficients getPIDFCoefficients(RunMode m) {
        Sim.chargeDevice(dev, "motor.getPIDFCoefficients");
        return new PIDFCoefficients(m == RunMode.RUN_TO_POSITION ? posPidf : velPidf);
    }
    @Override public int getTargetPositionTolerance() { return tolerance; }
    @Override public synchronized void setTargetPositionTolerance(int t) { tolerance = t; Sim.write(dev, 11, t, "motor.setTargetPositionTolerance", true); }
    @Override public double getCurrent(CurrentUnit unit) {
        double a = Sim.readDirect(dev, 3, "motor.getCurrent");
        return unit == CurrentUnit.MILLIAMPS ? a * 1000.0 : a;
    }
    @Override public double getCurrentAlert(CurrentUnit unit) { return unit == CurrentUnit.MILLIAMPS ? currentAlertA * 1000 : currentAlertA; }
    @Override public void setCurrentAlert(double c, CurrentUnit unit) { currentAlertA = unit == CurrentUnit.MILLIAMPS ? c / 1000.0 : c; Sim.chargeDevice(dev, "motor.setCurrentAlert"); }
    @Override public boolean isOverCurrent() {
        double a = Sim.readDirect(dev, 3, "motor.isOverCurrent");
        return currentAlertA > 0 ? a > currentAlertA : Sim.peek(dev, 5) != 0;
    }
}
