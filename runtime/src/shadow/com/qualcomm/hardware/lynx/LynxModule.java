package com.qualcomm.hardware.lynx;

import com.qualcomm.robotcore.hardware.HardwareDevice;

import org.firstinspires.ftc.robotcore.external.navigation.CurrentUnit;
import org.firstinspires.ftc.robotcore.external.navigation.VoltageUnit;
import org.gameplan.runtime.api.DeviceInfo;
import org.gameplan.runtime.api.Sim;

/**
 * GamePlan shadow of LynxModule exposing the parts robot code uses: bulk caching (OFF / MANUAL /
 * AUTO with the SDK's cache semantics, implemented in the runtime's cost model), input voltage,
 * LED colour and identity.
 */
public class LynxModule implements HardwareDevice {
    public static final String TAG = "LynxModule";
    public enum BulkCachingMode { OFF, MANUAL, AUTO }

    private final DeviceInfo info;
    private BulkCachingMode mode = BulkCachingMode.OFF;
    private int ledColor = 0xFF00FF00;

    public LynxModule(DeviceInfo info) { this.info = info; }

    public void setBulkCachingMode(BulkCachingMode m) {
        mode = m;
        Sim.setBulkMode(info.index, m == BulkCachingMode.OFF ? 0 : m == BulkCachingMode.MANUAL ? 1 : 2);
    }
    public BulkCachingMode getBulkCachingMode() { return mode; }
    public void clearBulkCache() { Sim.clearBulkCache(info.index); }
    public double getInputVoltage(VoltageUnit unit) {
        double v = Sim.readDirect(info.index, 1, "voltage.getVoltage");
        return unit == VoltageUnit.MILLIVOLTS ? v * 1000 : v;
    }
    public double getAuxiliaryVoltage(VoltageUnit unit) { Sim.chargeDevice(info.index, "voltage.getVoltage"); return unit == VoltageUnit.MILLIVOLTS ? 5000 : 5.0; }
    public double getCurrent(CurrentUnit unit) { Sim.chargeDevice(info.index, "lynx.getCurrent"); return 0; }
    public double getGpioBusCurrent(CurrentUnit unit) { return 0; }
    public double getI2cBusCurrent(CurrentUnit unit) { return 0; }
    public boolean isParent() { return info.controlHub; }
    public int getModuleAddress() { return info.controlHub ? 173 : 2; }
    public void setConstant(int color) { ledColor = color; Sim.chargeDevice(info.index, "lynx.setConstant"); }
    public int getLedColor() { return ledColor; }
    public boolean isNotResponding() { return Sim.peek(info.index, 2) != 0; }
    @Override public Manufacturer getManufacturer() { return Manufacturer.Lynx; }
    @Override public String getDeviceName() { return info.name; }
    @Override public String getConnectionInfo() { return "GamePlan virtual " + info.name + " (" + info.component + ")"; }
    @Override public int getVersion() { return 1; }
    @Override public void resetDeviceConfigurationForOpMode() {}
    @Override public void close() {}
}
