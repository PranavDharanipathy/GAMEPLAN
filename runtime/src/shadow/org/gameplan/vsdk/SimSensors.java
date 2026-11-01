package org.gameplan.vsdk;

import com.qualcomm.robotcore.hardware.ColorSensor;
import com.qualcomm.robotcore.hardware.DigitalChannel;
import com.qualcomm.robotcore.hardware.DigitalChannelController;
import com.qualcomm.robotcore.hardware.DistanceSensor;
import com.qualcomm.robotcore.hardware.I2cAddr;
import com.qualcomm.robotcore.hardware.NormalizedColorSensor;
import com.qualcomm.robotcore.hardware.NormalizedRGBA;
import com.qualcomm.robotcore.hardware.TouchSensor;
import com.qualcomm.robotcore.hardware.VoltageSensor;

import org.firstinspires.ftc.robotcore.external.navigation.DistanceUnit;
import org.gameplan.runtime.api.DeviceInfo;
import org.gameplan.runtime.api.Sim;

/** Simulated FTC sensors; every read pays its modelled cost and returns host sensor-model values. */
public final class SimSensors {
    private SimSensors() {}

    public static class Voltage extends SimDevice implements VoltageSensor {
        private final int field;
        public Voltage(DeviceInfo i, int field) { super(i); this.field = field; }
        @Override public double getVoltage() { return Sim.readDirect(dev, field, "voltage.getVoltage"); }
        @Override public Manufacturer getManufacturer() { return Manufacturer.Lynx; }
    }

    public static class Distance extends SimDevice implements DistanceSensor {
        public Distance(DeviceInfo i) { super(i); }
        @Override public double getDistance(DistanceUnit unit) {
            double m = Sim.readI2c(dev, 1, "distance.getDistance");
            if (m >= 8.0) return DistanceSensor.distanceOutOfRange;
            return unit.fromMeters(m);
        }
    }

    public static class Color extends SimDevice implements ColorSensor, NormalizedColorSensor, DistanceSensor {
        private float gain = 1;
        public Color(DeviceInfo i) { super(i); }
        private int ch(int f) { return (int) Math.max(0, Math.min(65535, Sim.readI2c(dev, f, "color.channel"))); }
        @Override public int red() { return ch(1); }
        @Override public int green() { return ch(2); }
        @Override public int blue() { return ch(3); }
        @Override public int alpha() { return ch(4); }
        @Override public int argb() {
            int r = (int) Sim.readI2c(dev, 1, "color.argb"), g = (int) Sim.peek(dev, 2), b = (int) Sim.peek(dev, 3), a = (int) Sim.peek(dev, 4);
            return (clip8(a) << 24) | (clip8(r) << 16) | (clip8(g) << 8) | clip8(b);
        }
        private static int clip8(int v) { return Math.max(0, Math.min(255, v >> 2)); }
        @Override public void enableLed(boolean on) {}
        @Override public void setI2cAddress(I2cAddr a) {}
        @Override public I2cAddr getI2cAddress() { return I2cAddr.create7bit(0x52); }
        @Override public NormalizedRGBA getNormalizedColors() {
            NormalizedRGBA n = new NormalizedRGBA();
            double r = Sim.readI2c(dev, 1, "color.argb");
            n.red = (float) Math.min(1, r / 1024.0 * gain);
            n.green = (float) Math.min(1, Sim.peek(dev, 2) / 1024.0 * gain);
            n.blue = (float) Math.min(1, Sim.peek(dev, 3) / 1024.0 * gain);
            n.alpha = (float) Math.min(1, Sim.peek(dev, 4) / 1024.0 * gain);
            return n;
        }
        @Override public float getGain() { return gain; }
        @Override public void setGain(float g) { gain = g; }
        @Override public double getDistance(DistanceUnit unit) { return unit.fromMeters(Sim.readI2c(dev, 5, "distance.getDistance")); }
    }

    public static class Touch extends SimDevice implements TouchSensor {
        public Touch(DeviceInfo i) { super(i); }
        @Override public double getValue() { return isPressed() ? 1 : 0; }
        @Override public boolean isPressed() { return Sim.readBulk(dev, 1, "touch.isPressed") != 0; }
    }

    public static class Digital extends SimDevice implements DigitalChannel {
        private Mode mode = Mode.INPUT;
        private boolean out;
        public Digital(DeviceInfo i) { super(i); }
        @Override public Mode getMode() { return mode; }
        @Override public void setMode(Mode m) { mode = m; Sim.chargeDevice(dev, "digital.setState"); }
        @Override public boolean getState() { return mode == Mode.OUTPUT ? out : Sim.readBulk(dev, 1, "digital.getState") != 0; }
        @Override public void setState(boolean s) { out = s; Sim.chargeDevice(dev, "digital.setState"); }
        @Override @Deprecated public void setMode(DigitalChannelController.Mode m) { setMode(m == DigitalChannelController.Mode.INPUT ? Mode.INPUT : Mode.OUTPUT); }
    }
}
