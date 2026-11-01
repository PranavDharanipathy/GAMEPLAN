package com.qualcomm.robotcore.hardware;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.SortedSet;
import java.util.TreeSet;

import com.qualcomm.robotcore.util.SerialNumber;

import org.gameplan.runtime.api.DeviceInfo;
import org.gameplan.runtime.api.Sim;
import org.gameplan.vsdk.Devices;

/**
 * GamePlan shadow of HardwareMap (SDK 12.0 public API). Populated from the robot description
 * (the simulator's equivalent of the active Robot Controller configuration file).
 */
public class HardwareMap implements Iterable<HardwareDevice> {
    public DeviceMapping<DcMotorController> dcMotorController = new DeviceMapping<>(DcMotorController.class);
    public DeviceMapping<DcMotor> dcMotor = new DeviceMapping<>(DcMotor.class);
    public DeviceMapping<ServoController> servoController = new DeviceMapping<>(ServoController.class);
    public DeviceMapping<Servo> servo = new DeviceMapping<>(Servo.class);
    public DeviceMapping<CRServo> crservo = new DeviceMapping<>(CRServo.class);
    public DeviceMapping<TouchSensorMultiplexer> touchSensorMultiplexer = new DeviceMapping<>(TouchSensorMultiplexer.class);
    public DeviceMapping<AnalogInput> analogInput = new DeviceMapping<>(AnalogInput.class);
    public DeviceMapping<DigitalChannel> digitalChannel = new DeviceMapping<>(DigitalChannel.class);
    public DeviceMapping<OpticalDistanceSensor> opticalDistanceSensor = new DeviceMapping<>(OpticalDistanceSensor.class);
    public DeviceMapping<TouchSensor> touchSensor = new DeviceMapping<>(TouchSensor.class);
    public DeviceMapping<PWMOutput> pwmOutput = new DeviceMapping<>(PWMOutput.class);
    public DeviceMapping<I2cDevice> i2cDevice = new DeviceMapping<>(I2cDevice.class);
    public DeviceMapping<I2cDeviceSynch> i2cDeviceSynch = new DeviceMapping<>(I2cDeviceSynch.class);
    public DeviceMapping<ColorSensor> colorSensor = new DeviceMapping<>(ColorSensor.class);
    public DeviceMapping<LED> led = new DeviceMapping<>(LED.class);
    public DeviceMapping<AccelerationSensor> accelerationSensor = new DeviceMapping<>(AccelerationSensor.class);
    public DeviceMapping<CompassSensor> compassSensor = new DeviceMapping<>(CompassSensor.class);
    public DeviceMapping<GyroSensor> gyroSensor = new DeviceMapping<>(GyroSensor.class);
    public DeviceMapping<IrSeekerSensor> irSeekerSensor = new DeviceMapping<>(IrSeekerSensor.class);
    public DeviceMapping<LightSensor> lightSensor = new DeviceMapping<>(LightSensor.class);
    public DeviceMapping<UltrasonicSensor> ultrasonicSensor = new DeviceMapping<>(UltrasonicSensor.class);
    public DeviceMapping<VoltageSensor> voltageSensor = new DeviceMapping<>(VoltageSensor.class);
    public final List<DeviceMapping<? extends HardwareDevice>> allDeviceMappings = new ArrayList<>();
    /** Android Context is not available in the simulator; always null (documented compatibility boundary). */
    public final android.content.Context appContext = null;

    private final Map<String, List<HardwareDevice>> byName = new LinkedHashMap<>();

    public HardwareMap(android.content.Context ctx, com.qualcomm.robotcore.eventloop.opmode.OpModeManagerNotifier n) { this(); }

    protected HardwareMap() {
        allDeviceMappings.add(dcMotorController); allDeviceMappings.add(dcMotor); allDeviceMappings.add(servoController);
        allDeviceMappings.add(servo); allDeviceMappings.add(crservo); allDeviceMappings.add(touchSensorMultiplexer);
        allDeviceMappings.add(analogInput); allDeviceMappings.add(digitalChannel); allDeviceMappings.add(opticalDistanceSensor);
        allDeviceMappings.add(touchSensor); allDeviceMappings.add(pwmOutput); allDeviceMappings.add(i2cDevice);
        allDeviceMappings.add(i2cDeviceSynch); allDeviceMappings.add(colorSensor); allDeviceMappings.add(led);
        allDeviceMappings.add(accelerationSensor); allDeviceMappings.add(compassSensor); allDeviceMappings.add(gyroSensor);
        allDeviceMappings.add(irSeekerSensor); allDeviceMappings.add(lightSensor); allDeviceMappings.add(ultrasonicSensor);
        allDeviceMappings.add(voltageSensor);
    }

    /** Build the map from the simulator's device table. */
    public static HardwareMap gpCreate() {
        HardwareMap m = new HardwareMap();
        Devices.populate(m, Sim.devices());
        return m;
    }

    @SuppressWarnings("unchecked")
    public <T> T get(Class<? extends T> cls, String name) {
        T t = tryGet(cls, name);
        if (t == null) {
            StringBuilder have = new StringBuilder();
            for (Map.Entry<String, List<HardwareDevice>> e : byName.entrySet())
                for (HardwareDevice d : e.getValue()) have.append("\n  ").append(e.getKey()).append(" : ").append(d.getClass().getSimpleName());
            throw new IllegalArgumentException(String.format("Unable to find a hardware device with name \"%s\" and type %s"
                + " (simulator robot description provides:%s)", name, cls.getSimpleName(), have));
        }
        return t;
    }

    @SuppressWarnings("unchecked")
    public <T> T tryGet(Class<? extends T> cls, String name) {
        List<HardwareDevice> l = byName.get(name);
        if (l == null) return null;
        for (HardwareDevice d : l) if (cls.isInstance(d)) { Sim.charge("hardwareMap.get"); return (T) d; }
        // SDK: get(DcMotorEx.class, name) on a device registered as DcMotor works because the impl is DcMotorImplEx
        return null;
    }

    public <T> T get(Class<? extends T> cls, SerialNumber sn) { return null; }

    public HardwareDevice get(String name) {
        List<HardwareDevice> l = byName.get(name);
        if (l == null || l.isEmpty()) throw new IllegalArgumentException("No device with name " + name);
        return l.get(0);
    }

    @SuppressWarnings("unchecked")
    public <T> List<T> getAll(Class<? extends T> cls) {
        List<T> out = new ArrayList<>();
        for (List<HardwareDevice> l : byName.values()) for (HardwareDevice d : l) if (cls.isInstance(d) && !out.contains(d)) out.add((T) d);
        return out;
    }

    public SortedSet<String> getAllNames(Class<? extends HardwareDevice> cls) {
        SortedSet<String> s = new TreeSet<>();
        for (Map.Entry<String, List<HardwareDevice>> e : byName.entrySet()) for (HardwareDevice d : e.getValue()) if (cls.isInstance(d)) s.add(e.getKey());
        return s;
    }

    public void put(String name, HardwareDevice d) { byName.computeIfAbsent(name, k -> new ArrayList<>()).add(d); }
    public void put(String name, List<HardwareDevice> l) { for (HardwareDevice d : l) put(name, d); }
    public void put(SerialNumber sn, String name, HardwareDevice d) { put(name, d); }
    public boolean remove(String name, HardwareDevice d) { List<HardwareDevice> l = byName.get(name); return l != null && l.remove(d); }
    public boolean remove(SerialNumber sn, String name, HardwareDevice d) { return remove(name, d); }

    public Set<String> getNamesOf(HardwareDevice d) {
        Set<String> s = new TreeSet<>();
        for (Map.Entry<String, List<HardwareDevice>> e : byName.entrySet()) if (e.getValue().contains(d)) s.add(e.getKey());
        return s;
    }

    public int size() { int n = 0; for (List<HardwareDevice> l : byName.values()) n += l.size(); return n; }
    public Iterator<HardwareDevice> iterator() {
        List<HardwareDevice> all = new ArrayList<>();
        for (List<HardwareDevice> l : byName.values()) all.addAll(l);
        return all.iterator();
    }
    public Iterable<HardwareDevice> unsafeIterable() { return this; }
    public void logDevices() { for (String n : byName.keySet()) Sim.log("INFO", "device: " + n); }

    public static class DeviceMapping<DEVICE_TYPE extends HardwareDevice> implements Iterable<DEVICE_TYPE> {
        private final Class<DEVICE_TYPE> cls;
        private final Map<String, DEVICE_TYPE> map = new HashMap<>();
        public DeviceMapping(Class<DEVICE_TYPE> c) { cls = c; }
        public Class<DEVICE_TYPE> getDeviceTypeClass() { return cls; }
        public DEVICE_TYPE cast(Object o) { return cls.cast(o); }
        public DEVICE_TYPE get(String name) {
            DEVICE_TYPE d = map.get(name);
            if (d == null) throw new IllegalArgumentException(String.format("Unable to find a hardware device with the name \"%s\"", name));
            Sim.charge("hardwareMap.get");
            return d;
        }
        public void put(String name, DEVICE_TYPE d) { map.put(name, d); }
        public void put(SerialNumber sn, String name, DEVICE_TYPE d) { map.put(name, d); }
        public void putLocal(String name, DEVICE_TYPE d) { map.put(name, d); }
        public boolean contains(String name) { return map.containsKey(name); }
        public boolean remove(String name) { return map.remove(name) != null; }
        public boolean remove(SerialNumber sn, String name) { return remove(name); }
        public Iterator<DEVICE_TYPE> iterator() { return new ArrayList<>(map.values()).iterator(); }
        public Set<Map.Entry<String, DEVICE_TYPE>> entrySet() { return Collections.unmodifiableMap(map).entrySet(); }
        public int size() { return map.size(); }
    }
}
