package org.gameplan.runtime.api;

public final class GamepadData {
    public float lx, ly, rx, ry, lt, rt;
    public int buttons;
    public boolean btn(int bit) { return (buttons & (1 << bit)) != 0; }
}
