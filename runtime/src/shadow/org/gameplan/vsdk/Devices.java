package org.gameplan.vsdk;

import java.util.List;

import com.qualcomm.hardware.lynx.LynxModule;
import com.qualcomm.robotcore.hardware.AnalogInput;
import com.qualcomm.robotcore.hardware.CRServoImplEx;
import com.qualcomm.robotcore.hardware.DcMotorImplEx;
import com.qualcomm.robotcore.hardware.HardwareMap;
import com.qualcomm.robotcore.hardware.ServoImplEx;

import org.gameplan.runtime.api.DeviceInfo;

/** Builds the HardwareMap from the robot description's device table. */
public final class Devices {
    private Devices() {}

    public static void populate(HardwareMap m, List<DeviceInfo> devices) {
        boolean anyVoltage = false;
        for (DeviceInfo d : devices) {
            switch (d.kind) {
                case "motor": { DcMotorImplEx x = new DcMotorImplEx(d); m.dcMotor.put(d.name, x); m.put(d.name, x); break; }
                case "servo": { ServoImplEx x = new ServoImplEx(d); m.servo.put(d.name, x); m.put(d.name, x); break; }
                case "crservo": { CRServoImplEx x = new CRServoImplEx(d); m.crservo.put(d.name, x); m.put(d.name, x); break; }
                case "imu": m.put(d.name, new SimImu(d)); break;
                case "voltage": { SimSensors.Voltage v = new SimSensors.Voltage(d, 1); m.voltageSensor.put(d.name, v); m.put(d.name, v); anyVoltage = true; break; }
                case "distance": m.put(d.name, new SimSensors.Distance(d)); break;
                case "color": { SimSensors.Color c = new SimSensors.Color(d); m.colorSensor.put(d.name, c); m.put(d.name, c); break; }
                case "touch": { SimSensors.Touch t = new SimSensors.Touch(d); m.touchSensor.put(d.name, t); m.put(d.name, t); break; }
                case "digital": { SimSensors.Digital g = new SimSensors.Digital(d); m.digitalChannel.put(d.name, g); m.put(d.name, g); break; }
                case "analog": case "analog_encoder": { AnalogInput a = new AnalogInput(d); m.analogInput.put(d.name, a); m.put(d.name, a); break; }
                case "pinpoint": m.put(d.name, new com.qualcomm.hardware.gobilda.GoBildaPinpointDriver(d)); break;
                case "webcam": m.put(d.name, Webcam.create(d)); break;
                case "hub": m.put(d.name, new LynxModule(d)); break;
                default: break;
            }
        }
        if (!anyVoltage) {
            // every REV hub reports its input voltage through a VoltageSensor named after the hub
            for (DeviceInfo d : devices) if (d.kind.equals("hub")) {
                SimSensors.Voltage v = new SimSensors.Voltage(d, 1);
                m.voltageSensor.put(d.name, v);
                m.put(d.name, v);
            }
        }
    }
}
