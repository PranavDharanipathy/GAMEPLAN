package org.gameplan.runtime;

import java.io.File;
import java.io.OutputStream;
import java.io.PrintStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.lang.annotation.Annotation;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.TimeUnit;

import org.gameplan.runtime.api.DeviceInfo;
import org.gameplan.runtime.api.Lifecycle;
import org.gameplan.runtime.api.Sim;

/**
 * Entry point of the sandboxed FTC runtime process. Runs the real FTC SDK classes (from the SDK
 * jars of the project's SDK version), GamePlan shadows of the Android-bound SDK classes, the
 * team's compiled code and its libraries, all inside SandboxLoader.
 */
public final class RuntimeMain {
    static final class OpModeEntry {
        String name, group, kind, className;
        boolean linear;
        Class<?> cls;
    }

    private Protocol io;
    private VirtualRuntime rt;
    private SandboxLoader loader;
    private final Map<String, OpModeEntry> opModes = new LinkedHashMap<>();
    private final List<Object> diagnostics = new ArrayList<>();
    private Thread opThread;
    private OpModeEntry current;
    private String sdkVersion = "unknown";

    public static void main(String[] args) throws Exception {
        Thread.currentThread().setName("gp-main");
        String pkg = null;
        for (int i = 0; i < args.length; i++) if (args[i].equals("--package")) pkg = args[++i];
        new RuntimeMain().run(pkg);
    }

    private static final class LogStream extends OutputStream {
        final StringBuilder sb = new StringBuilder();
        final String level;
        LogStream(String level) { this.level = level; }
        @Override public synchronized void write(int b) {
            if (b == '\n') { flushLine(); return; }
            sb.append((char) b);
            if (sb.length() > 4000) flushLine();
        }
        void flushLine() {
            String s = sb.toString();
            sb.setLength(0);
            if (Sim.backend() != null) Sim.log(level, s);
        }
    }

    private void run(String pkg) throws Exception {
        PrintStream protocolOut = new PrintStream(new java.io.FileOutputStream(java.io.FileDescriptor.out), false);
        io = new Protocol(System.in, protocolOut);
        System.setOut(new PrintStream(new LogStream("INFO"), true));   // robot code println -> LOG frames, never the protocol pipe
        Protocol.Frame init = io.read();
        if (init == null || init.type != Protocol.INIT) throw new IllegalStateException("expected INIT");
        Map<String, Object> ij = Json.obj(Json.parse(init.text()));
        List<DeviceInfo> devices = new ArrayList<>();
        Map<String, Integer> hubIdx = new LinkedHashMap<>();
        Map<String, Boolean> hubIsCH = new LinkedHashMap<>();
        for (Object o : Json.arr(ij.get("devices"))) {
            Map<String, Object> d = Json.obj(o);
            if ("hub".equals(d.get("kind"))) {
                hubIdx.put(Json.str(d.get("id"), ""), (int) Json.num(d.get("index"), 0));
                hubIsCH.put(Json.str(d.get("id"), ""), "robot-controller".equals(d.get("role")));
            }
        }
        for (Object o : Json.arr(ij.get("devices"))) {
            Map<String, Object> d = Json.obj(o);
            String hub = Json.str(d.get("hub"), Json.str(d.get("id"), "control"));
            boolean isHub = "hub".equals(d.get("kind"));
            devices.add(new DeviceInfo((int) Json.num(d.get("index"), 0), Json.str(d.get("name"), "?"), Json.str(d.get("kind"), "?"),
                isHub ? Json.str(d.get("id"), "") : hub, Json.str(d.get("component"), ""), (int) Json.num(d.get("port"), 0),
                Json.num(d.get("ticksPerRev"), 537.7), Json.num(d.get("maxRpm"), 312), isHub ? -1 : hubIdx.getOrDefault(hub, -1),
                hubIsCH.getOrDefault(isHub ? Json.str(d.get("id"), "") : hub, true)));
        }
        CostModel cost = new CostModel(Json.str(ij.get("timingProfile"), ""), (long) Json.num(ij.get("seed"), 1),
                                       Json.bool(ij.get("deterministicCompute"), true));
        Map<String, Object> ready = new LinkedHashMap<>();
        try {
            loadPackage(pkg);
        } catch (Throwable e) {
            ready.put("error", "cannot load simulator package: " + e);
            io.sendText(Protocol.READY, Json.write(ready));
            return;
        }
        rt = new VirtualRuntime(io, cost, devices, sdkVersion);
        Sim.install(rt);
        List<Object> list = new ArrayList<>();
        for (OpModeEntry e : opModes.values())
            list.add(Json.map("name", e.name, "group", e.group, "kind", e.kind, "class", e.className, "linear", e.linear));
        ready.put("opModes", list);
        ready.put("sdkVersion", sdkVersion);
        ready.put("diagnostics", diagnostics);
        ready.put("rewrittenClasses", loader.rewrittenClasses);
        ready.put("redirectedCalls", loader.redirectedCalls);
        Map<String, Object> f = new LinkedHashMap<>();
        for (Map.Entry<String, List<String>> e : loader.findings.entrySet()) f.put(e.getKey(), e.getValue());
        ready.put("apiFindings", f);
        io.sendText(Protocol.READY, Json.write(ready));
        mainLoop();
    }

    private void loadPackage(String pkg) throws Exception {
        File dir = new File(pkg);
        Map<String, Object> m = Json.obj(Json.parse(new String(Files.readAllBytes(new File(dir, "package.json").toPath()), StandardCharsets.UTF_8)));
        sdkVersion = Json.str(m.get("sdkVersion"), "unknown");
        loader = new SandboxLoader(RuntimeMain.class.getClassLoader());
        Map<String, Object> cp = Json.obj(m.get("classpath"));
        for (Object s : Json.arr(cp.get("shadow"))) loader.addRoot(resolve(dir, (String) s), false);
        for (Object s : Json.arr(cp.get("sdk"))) loader.addRoot(resolve(dir, (String) s), false);
        for (Object s : Json.arr(cp.get("libs"))) loader.addRoot(resolve(dir, (String) s), false);
        for (Object s : Json.arr(cp.get("team"))) loader.addRoot(resolve(dir, (String) s), true);
        Class<?> opMode = Class.forName("com.qualcomm.robotcore.eventloop.opmode.OpMode", false, loader);
        Class<?> linear = Class.forName("com.qualcomm.robotcore.eventloop.opmode.LinearOpMode", false, loader);
        @SuppressWarnings("unchecked") Class<? extends Annotation> teleOp = (Class<? extends Annotation>) Class.forName("com.qualcomm.robotcore.eventloop.opmode.TeleOp", false, loader);
        @SuppressWarnings("unchecked") Class<? extends Annotation> auto = (Class<? extends Annotation>) Class.forName("com.qualcomm.robotcore.eventloop.opmode.Autonomous", false, loader);
        @SuppressWarnings("unchecked") Class<? extends Annotation> disabled = (Class<? extends Annotation>) Class.forName("com.qualcomm.robotcore.eventloop.opmode.Disabled", false, loader);
        for (String cn : loader.teamClasses()) {
            Class<?> c;
            try {
                c = Class.forName(cn, false, loader);
            } catch (Throwable e) {
                diagnostics.add(Json.map("class", cn, "problem", "cannot load: " + e));
                continue;
            }
            try {
                if (!opMode.isAssignableFrom(c) || Modifier.isAbstract(c.getModifiers())) continue;
                Annotation a = c.getAnnotation(teleOp);
                String kind = "TeleOp";
                if (a == null) { a = c.getAnnotation(auto); kind = "Autonomous"; }
                if (a == null) continue;
                if (c.getAnnotation(disabled) != null) { diagnostics.add(Json.map("class", cn, "problem", "@Disabled (not registered, as on the robot)")); continue; }
                OpModeEntry e = new OpModeEntry();
                Method nm = a.annotationType().getMethod("name");
                Method gm = a.annotationType().getMethod("group");
                String name = (String) nm.invoke(a);
                e.name = name == null || name.isEmpty() ? c.getSimpleName() : name;
                e.group = (String) gm.invoke(a);
                e.kind = kind;
                e.className = cn;
                e.linear = linear.isAssignableFrom(c);
                e.cls = c;
                if (opModes.containsKey(e.name)) diagnostics.add(Json.map("class", cn, "problem", "duplicate OpMode name '" + e.name + "'"));
                opModes.put(e.name, e);
            } catch (Throwable t) {
                diagnostics.add(Json.map("class", cn, "problem", "inspection failed: " + t));
            }
        }
    }

    private static File resolve(File base, String p) {
        File f = new File(p);
        return f.isAbsolute() ? f : new File(base, p);
    }

    // ------------------------------------------------------------------ lockstep loop
    private void mainLoop() throws Exception {
        VirtualRuntime.Req pendingReq = null;
        boolean justStarted = false;
        while (true) {
            Protocol.Frame f = io.read();
            if (f == null || f.type == Protocol.SHUTDOWN) break;
            if (f.type != Protocol.STATE) continue;
            String control;
            synchronized (rt) {
                control = applyState(f.payload);
                if (pendingReq != null) { pendingReq.isDone = true; pendingReq = null; }
                rt.notifyAll();
            }
            if ("START_THREAD".equals(control)) justStarted = true;
            VirtualRuntime.Req req = null;
            List<double[]> extra = new ArrayList<>();
            if (opThread != null) {
                if (justStarted) { opThread.start(); justStarted = false; }
                req = rt.requests.poll(20, TimeUnit.SECONDS);
                if (req == null) {
                    rt.status("stuck", current == null ? null : current.name,
                        "robot code ran for 20 s of host time without any SDK call (pure busy loop?). On a Control Hub the RC would report the OpMode stuck and restart.");
                    rt.killed = true;
                    opThread.interrupt();
                    opThread = null;
                } else if (req.finished) {
                    synchronized (rt) { extra.addAll(req.cmds); extra.addAll(stopOutputs()); }
                    rt.status("stopped", current == null ? null : current.name, null);
                    opThread = null;
                    current = null;
                    req = null;
                } else if (rt.stopRequested && rt.stopRequestedAt >= 0 && req.t - rt.stopRequestedAt > 0.9) {
                    if (!rt.killed) {
                        rt.killed = true;
                        rt.status("force-stopping", current == null ? null : current.name,
                            "OpMode did not exit within msStuckDetectStop (900 ms) after STOP; interrupting it as the SDK does");
                        opThread.interrupt();
                    }
                }
            }
            double target;
            if (req != null) {
                target = Math.max(req.t, rt.t);
                extra.addAll(0, req.cmds);
                pendingReq = req;
            } else {
                target = rt.t + 0.02;   // idle: no OpMode code is running
                synchronized (rt) { rt.t = target; extra.addAll(rt.pending); rt.pending.clear(); }
            }
            sendSync(target, extra);
        }
        io.sendText(Protocol.HWSTATS, Json.write(rt.hwStats()));
        io.send(Protocol.BYE, new byte[0]);
        System.exit(0);
    }

    private List<double[]> stopOutputs() {
        // SDK behaviour when an OpMode stops: all motors to zero power, servo PWM disabled
        List<double[]> c = new ArrayList<>();
        for (DeviceInfo d : rt.devices) {
            if (d.kind.equals("motor")) { c.add(new double[]{d.index, 1, 0.0, rt.t}); c.add(new double[]{d.index, 2, 0, rt.t}); }
            if (d.kind.equals("servo") || d.kind.equals("crservo")) c.add(new double[]{d.index, 2, 0.0, rt.t});
        }
        rt.lastWritten.clear();
        return c;
    }

    private void sendSync(double t, List<double[]> cmds) {
        ByteBuffer b = ByteBuffer.allocate(8 + 2 + cmds.size() * (2 + 1 + 8 + 8)).order(ByteOrder.LITTLE_ENDIAN);
        b.putDouble(t);
        b.putShort((short) cmds.size());
        for (double[] c : cmds) {
            b.putShort((short) c[0]);
            b.put((byte) c[1]);
            b.putDouble(c[2]);
            b.putDouble(c[3]);
        }
        io.send(Protocol.SYNC, b.array());
    }

    /** Apply a STATE frame (called with the rt monitor held). Returns "START_THREAD" when a new OpMode thread was created. */
    private String applyState(ByteBuffer p) {
        double t = p.getDouble();
        int control = p.get() & 0xff;
        String name = Protocol.readString16(p);
        int flags = p.get() & 0xff;
        rt.rcAlive = (flags & 1) != 0;
        rt.dsDisconnected = (flags & 2) != 0;
        rt.inputAllowed = (flags & 4) != 0;
        for (int g = 0; g < 2; g++) {
            float lx = p.getFloat(), ly = p.getFloat(), rx = p.getFloat(), ry = p.getFloat(), lt = p.getFloat(), rtr = p.getFloat();
            int buttons = p.getInt();
            org.gameplan.runtime.api.GamepadData d = rt.pads[g];
            if (d.lx != lx || d.ly != ly || d.rx != rx || d.ry != ry || d.lt != lt || d.rt != rtr || d.buttons != buttons) rt.padSeq[g]++;
            d.lx = lx; d.ly = ly; d.rx = rx; d.ry = ry; d.lt = lt; d.rt = rtr; d.buttons = buttons;
        }
        int n = p.getShort() & 0xffff;
        for (int i = 0; i < n; i++) {
            int dev = p.getShort() & 0xffff;
            int field = p.get() & 0xff;
            double v = p.getDouble();
            rt.values.put(dev * 64 + field, v);
        }
        String vis = Protocol.readString32(p);
        if (!vis.isEmpty()) {
            for (Object o : Json.arr(Json.parse(vis))) {
                Map<String, Object> cam = Json.obj(o);
                rt.vision.put((int) Json.num(cam.get("device"), -1), Json.write(cam));
            }
        }
        rt.t = Math.max(rt.t, t);
        rt.lastStateT = rt.t;
        String result = null;
        if (!rt.rcAlive && opThread != null) {
            rt.killed = true;
            rt.status("robot-controller-down", current == null ? null : current.name, "Robot Controller lost power / crashed; robot code is no longer running");
        }
        switch (control) {
            case 1: {  // INIT
                OpModeEntry e = opModes.get(name);
                if (e == null) {
                    rt.status("init-failed", name, "no OpMode named '" + name + "' (available: " + opModes.keySet() + ")");
                    break;
                }
                if (opThread != null) { rt.status("init-ignored", name, "an OpMode is already running"); break; }
                rt.startRequested = false;
                rt.stopRequested = false;
                rt.stopRequestedAt = -1;
                rt.killed = false;
                rt.inInit = true;
                current = e;
                opThread = new Thread(() -> runOpMode(e), "OpModeThread");
                opThread.setContextClassLoader(loader);
                rt.opThread = opThread;
                result = "START_THREAD";
                rt.status("init", e.name, null);
                break;
            }
            case 2:
                rt.startRequested = true;
                rt.inInit = false;
                if (current != null) rt.status("running", current.name, null);
                break;
            case 3:
                if (!rt.stopRequested) { rt.stopRequested = true; rt.stopRequestedAt = rt.t; }
                break;
            default: break;
        }
        return result;
    }

    private void runOpMode(OpModeEntry e) {
        try {
            Object om = e.cls.getDeclaredConstructor().newInstance();
            Object hw = Class.forName("com.qualcomm.robotcore.hardware.HardwareMap", true, loader).getMethod("gpCreate").invoke(null);
            Object tel = Class.forName("org.gameplan.vsdk.TelemetryImpl", true, loader).getDeclaredConstructor().newInstance();
            Lifecycle lc = (Lifecycle) om;
            lc.gpAttach(hw, tel);
            lc.gpRun();
        } catch (Throwable t) {
            Throwable c = t instanceof java.lang.reflect.InvocationTargetException ? t.getCause() : t;
            StringWriter sw = new StringWriter();
            c.printStackTrace(new PrintWriter(sw));
            boolean killed = c instanceof org.gameplan.runtime.api.OpModeKilledError;
            Map<String, Object> m = Json.map("opModeState", killed ? "force-stopped" : "crashed", "opMode", e.name,
                "error", String.valueOf(c), "stack", sw.toString(), "deterministic", rt.deterministic, "t", rt.t);
            io.sendText(Protocol.STATUS, Json.write(m));
        } finally {
            synchronized (rt) { rt.postFinished(); }
        }
    }
}
