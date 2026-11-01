package org.gameplan.vsdk;

import java.lang.reflect.Proxy;

import com.qualcomm.robotcore.hardware.HardwareDevice;

import org.firstinspires.ftc.robotcore.external.hardware.camera.WebcamName;
import org.gameplan.runtime.api.DeviceInfo;

/** WebcamName handle for a simulated robot camera (frames are rendered by the host; detections are virtual). */
public final class Webcam {
    private Webcam() {}
    public static HardwareDevice create(DeviceInfo d) {
        return (HardwareDevice) Proxy.newProxyInstance(WebcamName.class.getClassLoader(), new Class<?>[]{WebcamName.class, SimCamera.class}, (p, m, a) -> {
            switch (m.getName()) {
                case "toString": case "getDeviceName": case "getUsbDeviceNameIfAttached": return d.name;
                case "hashCode": return System.identityHashCode(p);
                case "equals": return p == a[0];
                case "isAttached": case "isWebcam": return true;
                case "gpDevice": return d;
                case "getManufacturer": return HardwareDevice.Manufacturer.Other;
                case "getConnectionInfo": return "GamePlan simulated USB camera";
                case "getVersion": return 1;
                default:
                    Class<?> r = m.getReturnType();
                    if (r == boolean.class) return false;
                    if (r == int.class) return 0;
                    return null;
            }
        });
    }
    /** Marker so vision shadows can recover the device index from a WebcamName. */
    public interface SimCamera { DeviceInfo gpDevice(); }
}
