package org.gameplan.runtime.api;

/** One configured device from the robot description (host INIT device table). */
public final class DeviceInfo {
    public final int index;
    public final String name, kind, hub, component;
    public final int port;
    public final double ticksPerRev, maxRpm;
    public final int hubIndex;          // device index of the owning hub (-1 for hubs themselves)
    public final boolean controlHub;    // owning hub is the Control Hub (internal UART) vs Expansion Hub (RS485/USB)

    public DeviceInfo(int index, String name, String kind, String hub, String component, int port, double ticksPerRev, double maxRpm,
                      int hubIndex, boolean controlHub) {
        this.index = index; this.name = name; this.kind = kind; this.hub = hub; this.component = component; this.port = port;
        this.ticksPerRev = ticksPerRev; this.maxRpm = maxRpm; this.hubIndex = hubIndex; this.controlHub = controlHub;
    }

    @Override public String toString() { return kind + " '" + name + "' (" + hub + " port " + port + ")"; }
}
