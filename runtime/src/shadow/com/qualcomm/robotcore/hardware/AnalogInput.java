package com.qualcomm.robotcore.hardware;

import org.gameplan.runtime.api.DeviceInfo;
import org.gameplan.runtime.api.Sim;

/** GamePlan shadow of AnalogInput (bulk-readable analog port, 0-3.3 V). */
public class AnalogInput implements HardwareDevice {
    private final DeviceInfo info;
    public AnalogInput(DeviceInfo info) { this.info = info; }
    public AnalogInput(AnalogInputController c, int ch) { throw new UnsupportedOperationException("construct analog inputs via hardwareMap in the simulator"); }
    public Manufacturer getManufacturer() { return Manufacturer.Lynx; }
    public double getVoltage() { return Sim.readBulk(info.index, 1, "analog.getVoltage"); }
    public double getMaxVoltage() { return 3.3; }
    public String getDeviceName() { return "GamePlan simulated analog input"; }
    public String getConnectionInfo() { return info.hub + "; analog port " + info.port; }
    public int getVersion() { return 1; }
    public void resetDeviceConfigurationForOpMode() {}
    public void close() {}
}
