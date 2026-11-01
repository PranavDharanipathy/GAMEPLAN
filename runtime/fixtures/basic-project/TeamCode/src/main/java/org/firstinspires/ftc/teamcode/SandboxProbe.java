package org.firstinspires.ftc.teamcode;

import com.qualcomm.robotcore.eventloop.opmode.OpMode;
import com.qualcomm.robotcore.eventloop.opmode.TeleOp;

import org.firstinspires.ftc.robotcore.internal.system.AppUtil;

import java.io.File;
import java.io.FileOutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.file.Files;

/**
 * Hostile-code fixture for the sandbox test: tries to escape the simulator's confinement and
 * reports each outcome as telemetry. A real team OpMode never does this.
 */
@TeleOp(name = "Sandbox Probe", group = "fixture")
public class SandboxProbe extends OpMode {
    private String readHome, readHost, writePkg, writeTmp, writeSdcard, tcp;

    private static String tryRun(Runnable r) {
        try { r.run(); return "ALLOWED"; } catch (Throwable t) {
            Throwable c = t.getCause() != null ? t.getCause() : t;
            String m = String.valueOf(c.getMessage());
            return "denied (" + c.getClass().getSimpleName() + ": " + (m.length() > 60 ? m.substring(0, 60) : m) + ")";
        }
    }
    private static void rethrow(Exception e) { throw new RuntimeException(e); }

    @Override public void init() {
        final File pkg = new File(".").getAbsoluteFile();
        readHome = tryRun(() -> {
            File[] l = new File(System.getProperty("user.home")).listFiles();
            if (l == null) throw new SecurityException("no listing");
        });
        readHost = tryRun(() -> {   // the simulator repository that contains the package
            try { Files.readAllBytes(new File(pkg, "../../../README.md").toPath()); } catch (Exception e) { rethrow(e); }
        });
        writePkg = tryRun(() -> {
            try (FileOutputStream o = new FileOutputStream(new File(pkg, "escape.txt"))) { o.write(1); } catch (Exception e) { rethrow(e); }
        });
        writeTmp = tryRun(() -> {
            try (FileOutputStream o = new FileOutputStream("/tmp/gameplan-escape-probe.txt")) { o.write(1); } catch (Exception e) { rethrow(e); }
        });
        writeSdcard = tryRun(() -> {
            File d = new File(AppUtil.ROBOT_DATA_DIR, "probe");
            d.mkdirs();
            try (FileOutputStream o = new FileOutputStream(new File(d, "log.txt"))) { o.write("ok".getBytes()); } catch (Exception e) { rethrow(e); }
        });
        tcp = tryRun(() -> {
            try (Socket s = new Socket()) { s.connect(new InetSocketAddress("1.1.1.1", 443), 500); } catch (Exception e) { rethrow(e); }
        });
    }

    @Override public void loop() {
        telemetry.addData("readHome", readHome);
        telemetry.addData("readHost", readHost);
        telemetry.addData("writePkg", writePkg);
        telemetry.addData("writeTmp", writeTmp);
        telemetry.addData("writeSdcard", writeSdcard);
        telemetry.addData("tcp", tcp);
    }
}
