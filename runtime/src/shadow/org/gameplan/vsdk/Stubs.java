package org.gameplan.vsdk;

import java.lang.reflect.Proxy;

import com.qualcomm.robotcore.hardware.configuration.typecontainers.MotorConfigurationType;

import org.gameplan.runtime.api.DeviceInfo;
import org.gameplan.runtime.api.Sim;

/** Helpers for SDK types that are rarely used by robot code and Android-heavy in the real SDK. */
public final class Stubs {
    private Stubs() {}

    /** Controller objects (DcMotorController, ServoController, ...): a proxy that returns neutral defaults and logs use. */
    @SuppressWarnings("unchecked")
    public static <T> T proxy(Class<T> iface, DeviceInfo info) {
        return (T) Proxy.newProxyInstance(iface.getClassLoader(), new Class<?>[]{iface}, (p, m, a) -> {
            String n = m.getName();
            if (n.equals("toString")) return "GamePlan " + iface.getSimpleName() + " for " + info;
            if (n.equals("hashCode")) return System.identityHashCode(p);
            if (n.equals("equals")) return p == a[0];
            if (n.equals("getConnectionInfo")) return info.hub;
            if (n.equals("getDeviceName")) return "GamePlan " + iface.getSimpleName();
            Sim.log("WARN", "controller-level SDK call " + iface.getSimpleName() + "." + n + "() is not simulated; returning a default");
            Class<?> r = m.getReturnType();
            if (r == boolean.class) return false;
            if (r == int.class || r == short.class || r == byte.class) return 0;
            if (r == long.class) return 0L;
            if (r == double.class) return 0.0;
            if (r == float.class) return 0f;
            return null;
        });
    }

    public static MotorConfigurationType motorType(DeviceInfo info) {
        try {
            MotorConfigurationType t = MotorConfigurationType.getUnspecifiedMotorType().clone();
            t.setMaxRPM(info.maxRpm);
            t.setTicksPerRev(info.ticksPerRev);
            t.setAchieveableMaxRPMFraction(0.85);
            return t;
        } catch (Throwable e) {
            return null;  // MotorConfigurationType needs gson/Android resources in some SDK builds
        }
    }
}
