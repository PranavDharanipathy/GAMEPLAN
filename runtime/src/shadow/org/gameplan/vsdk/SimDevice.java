package org.gameplan.vsdk;

import com.qualcomm.robotcore.hardware.HardwareDevice;

import org.gameplan.runtime.api.DeviceInfo;

abstract class SimDevice implements HardwareDevice {
    protected final DeviceInfo info;
    protected final int dev;
    SimDevice(DeviceInfo info) { this.info = info; this.dev = info.index; }
    @Override public Manufacturer getManufacturer() { return Manufacturer.Other; }
    @Override public String getDeviceName() { return "GamePlan simulated " + info.kind + " (" + info.component + ")"; }
    @Override public String getConnectionInfo() { return info.hub + "; port " + info.port; }
    @Override public int getVersion() { return 1; }
    @Override public void resetDeviceConfigurationForOpMode() {}
    @Override public void close() {}
}
