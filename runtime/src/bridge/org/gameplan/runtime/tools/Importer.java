package org.gameplan.runtime.tools;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.Stream;

import javax.tools.Diagnostic;
import javax.tools.DiagnosticCollector;
import javax.tools.JavaCompiler;
import javax.tools.JavaFileObject;
import javax.tools.StandardJavaFileManager;
import javax.tools.ToolProvider;

import org.gameplan.runtime.Json;

/**
 * Imports a complete Android Studio FTC project into a simulator package WITHOUT modifying the
 * project. Steps: inspect Gradle files, discover SDK version / libraries / repositories, scan
 * sources (OpModes, hardware names, vision, threads, Android APIs), resolve dependencies, compile
 * TeamCode against the real FTC SDK jars with failure isolation, and write
 *   <out>/package.json, <out>/classes/, <out>/compatibility.json, <out>/robot-skeleton.json
 *
 *   java -cp gameplan-runtime.jar org.gameplan.runtime.tools.Importer <project> --out <dir>
 *        [--runtime-build runtime/build] [--offline] [--watch]
 */
public final class Importer {
    private final Path project, out, runtimeBuild;
    private final boolean offline;
    private final Map<String, Object> report = new LinkedHashMap<>();
    private final List<Object> notes = new ArrayList<>();
    private String kotlinCp;

    Importer(Path project, Path out, Path runtimeBuild, boolean offline) {
        this.project = project.toAbsolutePath().normalize();
        this.out = out.toAbsolutePath().normalize();
        this.runtimeBuild = runtimeBuild.toAbsolutePath().normalize();
        this.offline = offline;
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 1) { System.err.println("usage: Importer <project> --out DIR [--runtime-build DIR] [--offline] [--watch]"); System.exit(2); }
        Path proj = Paths.get(args[0]), out = Paths.get("out/packages/" + Paths.get(args[0]).toAbsolutePath().normalize().getFileName());
        Path rb = Paths.get("runtime/build");
        boolean offline = false, watch = false;
        for (int i = 1; i < args.length; i++) {
            switch (args[i]) {
                case "--out": out = Paths.get(args[++i]); break;
                case "--runtime-build": rb = Paths.get(args[++i]); break;
                case "--offline": offline = true; break;
                case "--watch": watch = true; break;
                default: break;
            }
        }
        Importer im = new Importer(proj, out, rb, offline);
        String hash = im.run();
        if (!watch) return;
        // Source sync: rebuild the package when the (read-only) project changes.
        System.out.println("watching " + im.project + " for changes (Ctrl-C to stop)");
        while (true) {
            Thread.sleep(2000);
            String h = im.sourceHash(im.sources());
            if (!h.equals(hash)) {
                System.out.println("project changed; rebuilding simulator package");
                hash = new Importer(proj, out, rb, offline).run();
            }
        }
    }

    // ------------------------------------------------------------------ main flow
    String run() throws Exception {
        if (!Files.isDirectory(project)) throw new IllegalArgumentException("not a directory: " + project);
        Files.createDirectories(out);
        List<Path> srcs = sources();
        String srcHash = sourceHash(srcs);
        report.put("schema", "gameplan.compatibility/1");
        report.put("project", project.toString());
        report.put("sourceSha256", srcHash);
        report.put("sourcePolicy", "read-only: the importer never writes inside the project; generated code and artifacts live under " + out);

        // --- Gradle inspection
        List<String> gradleTexts = new ArrayList<>();
        for (String f : new String[]{"build.gradle", "build.dependencies.gradle", "build.common.gradle", "TeamCode/build.gradle", "TeamCode/build.gradle.kts", "settings.gradle", "settings.gradle.kts"}) {
            Path p = project.resolve(f);
            if (Files.exists(p)) gradleTexts.add(new String(Files.readAllBytes(p), StandardCharsets.UTF_8));
        }
        String gradle = String.join("\n", gradleTexts);
        Map<String, MavenResolver.Coord> coords = new LinkedHashMap<>();
        Map<String, Set<String>> exclusions = new HashMap<>();
        Matcher dm = Pattern.compile("(?m)^\\s*(implementation|api|compileOnly|runtimeOnly)\\s*\\(?\\s*['\"]([^'\":\\s]+):([^'\":\\s]+):([^'\"\\s]+)['\"][ \\t]*\\)?[ \\t]*(\\{[^}]*\\})?").matcher(gradle);
        while (dm.find()) {
            MavenResolver.Coord c = new MavenResolver.Coord(dm.group(2), dm.group(3), dm.group(4));
            if (dm.group(1).equals("compileOnly")) continue;
            coords.put(c.key(), c);
            if (dm.group(5) != null) {
                Matcher em = Pattern.compile("exclude\\s*\\(?\\s*group\\s*:\\s*['\"]([^'\"]+)['\"]").matcher(dm.group(5));
                while (em.find()) exclusions.computeIfAbsent(c.key(), k -> new HashSet<>()).add(em.group(1) + ":*");
            }
        }
        List<String> repos = new ArrayList<>(Arrays.asList("https://repo1.maven.org/maven2/", "https://maven-central.storage-download.googleapis.com/maven2/", "https://dl.google.com/dl/android/maven2/"));
        Matcher rm = Pattern.compile("maven\\s*\\{\\s*url\\s*=?\\s*(?:uri\\()?['\"]([^'\"]+)['\"]").matcher(gradle);
        while (rm.find()) if (!repos.contains(rm.group(1))) repos.add(rm.group(1));
        String sdkVersion = "12.0.0";
        MavenResolver.Coord rc = coords.get("org.firstinspires.ftc:RobotCore");
        if (rc != null) sdkVersion = rc.v;
        report.put("sdkVersion", sdkVersion);
        report.put("repositories", repos);
        List<Object> deps = new ArrayList<>();
        for (MavenResolver.Coord c : coords.values()) deps.add(c.toString());
        report.put("declaredDependencies", deps);
        report.put("libraries", detectLibraries(coords.keySet(), srcs));
        Map<String, Object> kotlin = new LinkedHashMap<>();
        Matcher km = Pattern.compile("org\\.jetbrains\\.kotlin[.:a-z\\-]*['\"]?\\s*version\\s*['\"]([\\d.]+)").matcher(gradle);
        if (km.find()) kotlin.put("pluginVersion", km.group(1));

        // --- SDK provisioning (built by runtime/tools/Build.java for this SDK version)
        Path sdkManifest = runtimeBuild.resolve("sdk-" + sdkVersion + ".json");
        if (!Files.exists(sdkManifest)) {
            notes.add("SDK " + sdkVersion + " runtime not built; falling back to 12.0.0 adapters (run: java runtime/tools/Build.java --sdk " + sdkVersion + ")");
            sdkManifest = runtimeBuild.resolve("sdk-12.0.0.json");
            report.put("sdkAdapter", "12.0.0 (fallback)");
        } else report.put("sdkAdapter", sdkVersion);
        Map<String, Object> sm = Json.obj(Json.parse(new String(Files.readAllBytes(sdkManifest), StandardCharsets.UTF_8)));
        List<String> sdkJars = new ArrayList<>();
        for (Object o : Json.arr(sm.get("jars"))) sdkJars.add((String) o);
        String shadowJar = (String) sm.get("shadow");

        // --- Android platform libraries that robot code can use without declaring them
        List<Object> substitutes = new ArrayList<>();
        boolean usesOrgJson = false;
        for (Path p : srcs) if (new String(Files.readAllBytes(p), StandardCharsets.UTF_8).contains("import org.json.")) usesOrgJson = true;
        if (usesOrgJson && !coords.containsKey("org.json:json")) {
            coords.put("org.json:json", new MavenResolver.Coord("org.json", "json", "20231013"));
            substitutes.add("org.json (built into Android) -> org.json:json:20231013 on the desktop; API-compatible for common use, edge-case behaviour may differ");
        }
        report.put("platformSubstitutes", substitutes);

        // --- dependency resolution
        MavenResolver mr = new MavenResolver(repos, Paths.get(System.getProperty("user.home"), ".gameplan", "maven"), offline);
        mr.providedGroups.add("org.firstinspires.ftc");
        MavenResolver.Resolved res = mr.resolve(new ArrayList<>(coords.values()), exclusions);
        List<String> libJars = new ArrayList<>();
        for (Path p : res.jars.values()) libJars.add(p.toString());
        Map<String, Object> rj = new LinkedHashMap<>();
        rj.put("resolved", res.versions);
        rj.put("skipped", res.skipped);
        rj.put("failed", res.failed);
        report.put("dependencyResolution", rj);

        // --- static scan
        Map<String, String> constants = stringConstants(srcs);
        Map<String, Object> scan = scan(srcs, constants);
        report.put("sourceScan", scan);

        // --- compile with failure isolation
        Path classes = out.resolve("classes");
        deleteTree(classes);
        Files.createDirectories(classes);
        List<Path> javaFiles = srcs.stream().filter(p -> p.toString().endsWith(".java")).collect(Collectors.toList());
        List<Path> ktFiles = srcs.stream().filter(p -> p.toString().endsWith(".kt")).collect(Collectors.toList());
        Map<String, String> excluded = new TreeMap<>();
        List<String> compileCp = new ArrayList<>(sdkJars);
        compileCp.addAll(libJars);
        compileCp.add(shadowJar);   // last: supplies only the android.* stand-ins that exist nowhere else
        if (!ktFiles.isEmpty()) {
            String kc = System.getenv("GAMEPLAN_KOTLINC_JAR");
            if (kc == null && kotlin.get("pluginVersion") != null) {
                // provision the Kotlin compiler matching the project's Kotlin Gradle plugin
                String kv = (String) kotlin.get("pluginVersion");
                MavenResolver kr = new MavenResolver(Arrays.asList("https://repo1.maven.org/maven2/", "https://maven-central.storage-download.googleapis.com/maven2/"), Paths.get(System.getProperty("user.home"), ".gameplan", "maven"), offline);
                MavenResolver.Resolved kres = kr.resolve(Arrays.asList(new MavenResolver.Coord("org.jetbrains.kotlin", "kotlin-compiler-embeddable", kv)), new HashMap<>());
                if (kres.failed.isEmpty() && kres.jars.containsKey("org.jetbrains.kotlin:kotlin-compiler-embeddable")) {
                    kc = kres.jars.values().stream().map(Path::toString).collect(Collectors.joining(File.pathSeparator));
                    kotlin.put("compiler", "org.jetbrains.kotlin:kotlin-compiler-embeddable:" + kv + " (provisioned, " + kres.jars.size() + " jars)");
                } else kotlin.put("provisioningFailed", kres.failed);
            }
            kotlinCp = kc;
            boolean kotlinOk = kc != null && compileKotlin(kc, ktFiles, javaFiles, compileCp, classes, excluded);
            kotlin.put("files", ktFiles.size());
            kotlin.put("compiled", kotlinOk);
            if (!kotlinOk) {
                kotlin.put("note", "Kotlin sources need the bundled Kotlin compiler (GAMEPLAN_KOTLINC_JAR). They are excluded together with every Java file that depends on them.");
                for (Path k : ktFiles) excluded.put(rel(k), "Kotlin compiler not available in this build");
            }
            report.put("kotlin", kotlin);
        }
        Set<Path> remaining = new LinkedHashSet<>(javaFiles);
        List<String> cpWithKotlin = new ArrayList<>(compileCp);
        cpWithKotlin.add(0, classes.toString());
        for (int round = 0; round < 60 && !remaining.isEmpty(); round++) {
            Map<Path, String> errs = javac(new ArrayList<>(remaining), cpWithKotlin, classes);
            if (errs.isEmpty()) break;
            for (Map.Entry<Path, String> e : errs.entrySet()) {
                remaining.remove(e.getKey());
                excluded.put(rel(e.getKey()), e.getValue());
            }
            deleteTree(classes);
            Files.createDirectories(classes);
            if (!ktFiles.isEmpty() && Boolean.TRUE.equals(kotlin.get("compiled"))) compileKotlin(kotlinCp, ktFiles, new ArrayList<>(remaining), compileCp, classes, excluded);
        }
        List<Object> compiled = new ArrayList<>();
        for (Path p : remaining) compiled.add(rel(p));
        Map<String, Object> cj = new LinkedHashMap<>();
        cj.put("compiled", compiled.size());
        cj.put("excluded", excluded);
        cj.put("note", "Excluded files failed to compile against the FTC SDK " + sdkVersion + " + resolved libraries (usually Android-only APIs or missing dependencies) or depend on such files. They are NOT simulated; nothing was rewritten to make them compile.");
        report.put("compilation", cj);

        // --- package manifest
        Map<String, Object> pkg = new LinkedHashMap<>();
        pkg.put("schema", "gameplan.package/1");
        pkg.put("project", project.toString());
        pkg.put("sourceSha256", srcHash);
        pkg.put("sdkVersion", sdkVersion);
        Map<String, Object> cp = new LinkedHashMap<>();
        cp.put("shadow", Arrays.asList(shadowJar));
        cp.put("sdk", sdkJars);
        cp.put("libs", libJars);
        cp.put("team", Arrays.asList("classes"));
        pkg.put("classpath", cp);
        Files.write(out.resolve("package.json"), Json.write(pkg).getBytes(StandardCharsets.UTF_8));
        report.put("notes", notes);
        report.put("compatibilityBoundaries", boundaries(scan));
        Files.write(out.resolve("compatibility.json"), pretty(Json.write(report)).getBytes(StandardCharsets.UTF_8));
        Files.write(out.resolve("robot-skeleton.json"), pretty(Json.write(skeleton(scan))).getBytes(StandardCharsets.UTF_8));
        System.out.println("package: " + out + "  (" + compiled.size() + " compiled, " + excluded.size() + " excluded; SDK " + sdkVersion + ")");
        return srcHash;
    }

    // ------------------------------------------------------------------ sources
    List<Path> sources() throws IOException {
        Path tc = project.resolve("TeamCode/src/main/java");
        if (!Files.isDirectory(tc)) tc = project.resolve("TeamCode/src/main/kotlin");
        if (!Files.isDirectory(tc)) throw new IllegalArgumentException("no TeamCode/src/main/java in " + project);
        try (Stream<Path> s = Files.walk(tc)) {
            return s.filter(p -> p.toString().endsWith(".java") || p.toString().endsWith(".kt")).sorted().collect(Collectors.toList());
        }
    }

    String sourceHash(List<Path> srcs) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        for (Path p : srcs) { md.update(rel(p).getBytes(StandardCharsets.UTF_8)); md.update(Files.readAllBytes(p)); }
        StringBuilder h = new StringBuilder();
        for (byte b : md.digest()) h.append(String.format("%02x", b));
        return h.toString();
    }

    private String rel(Path p) { return project.relativize(p.toAbsolutePath().normalize()).toString().replace(File.separatorChar, '/'); }

    private static List<Object> detectLibraries(Set<String> coords, List<Path> srcs) throws IOException {
        String[][] known = {
            {"com.pedropathing", "Pedro Pathing", "runs as ordinary JVM code on top of the simulated SDK devices"},
            {"com.acmerobotics.roadrunner", "Road Runner", "runs as ordinary JVM code on top of the simulated SDK devices"},
            {"org.ftclib", "FTCLib", "runs on top of the simulated SDK devices"},
            {"com.arcrobotics", "FTCLib", "runs on top of the simulated SDK devices"},
            {"com.acmerobotics.dashboard", "FTC Dashboard", "web server is not started; telemetry is captured"},
            {"com.acmerobotics.slothboard", "FTC Dashboard (Sloth fork)", "web server is not started; telemetry is captured"},
            {"dev.frozenmilk", "Sloth / Dairy", "hot-reload loader is not used (the simulator rebuilds the package instead)"},
            {"org.openftc", "EasyOpenCV", "raw camera pipelines are NOT executed (virtual vision boundary)"},
        };
        List<Object> out = new ArrayList<>();
        for (String[] k : known) for (String c : coords) if (c.startsWith(k[0])) { out.add(Json.map("library", k[1], "coordinate", c, "simulation", k[2])); break; }
        return out;
    }

    private static Map<String, String> stringConstants(List<Path> srcs) throws IOException {
        Map<String, String> m = new HashMap<>();
        Pattern cls = Pattern.compile("(?:class|object|interface)\\s+(\\w+)");
        Pattern f = Pattern.compile("(?:static\\s+)?(?:final\\s+)?(?:public\\s+|private\\s+|protected\\s+)?(?:static\\s+)?(?:final\\s+)?String\\s+(\\w+)\\s*=\\s*\"([^\"]*)\"");
        for (Path p : srcs) {
            String s = new String(Files.readAllBytes(p), StandardCharsets.UTF_8);
            Matcher cm = cls.matcher(s);
            String c = cm.find() ? cm.group(1) : "";
            Matcher fm = f.matcher(s);
            while (fm.find()) { m.put(c + "." + fm.group(1), fm.group(2)); m.putIfAbsent(fm.group(1), fm.group(2)); }
        }
        return m;
    }

    private Map<String, Object> scan(List<Path> srcs, Map<String, String> consts) throws IOException {
        Map<String, Object> out = new LinkedHashMap<>();
        List<Object> opModes = new ArrayList<>();
        Map<String, Object> devices = new TreeMap<>();
        Set<String> vision = new LinkedHashSet<>(), threads = new LinkedHashSet<>(), android = new LinkedHashSet<>(), network = new LinkedHashSet<>();
        Pattern om = Pattern.compile("@(TeleOp|Autonomous)\\s*(\\(([^)]*)\\))?[\\s\\S]{0,200}?class\\s+(\\w+)");
        Pattern get1 = Pattern.compile("hardwareMap\\s*\\.\\s*get\\s*\\(\\s*([\\w.]+)\\.class\\s*,\\s*([\\w.\"]+)\\s*\\)");
        Pattern get2 = Pattern.compile("hardwareMap\\s*\\.\\s*(dcMotor|servo|crservo|voltageSensor|analogInput|digitalChannel|touchSensor|colorSensor)\\s*\\.\\s*get\\s*\\(\\s*([\\w.\"]+)\\s*\\)");
        Pattern getKt = Pattern.compile("hardwareMap\\s*\\.\\s*get\\s*\\(\\s*([\\w.]+)::class\\.java\\s*,\\s*([\\w.\"]+)\\s*\\)");
        for (Path p : srcs) {
            String s = new String(Files.readAllBytes(p), StandardCharsets.UTF_8);
            String r = rel(p);
            Matcher m = om.matcher(s);
            while (m.find()) {
                if (s.substring(Math.max(0, m.start() - 40), m.start()).contains("@Disabled") || s.contains("@Disabled\n@" + m.group(1))) {}
                opModes.add(Json.map("kind", m.group(1), "class", m.group(4), "args", m.group(3) == null ? "" : m.group(3).trim(), "file", r,
                                     "disabled", s.contains("@Disabled")));
            }
            for (Pattern gp : new Pattern[]{get1, get2, getKt}) {
                Matcher g = gp.matcher(s);
                while (g.find()) {
                    String type = g.group(1), arg = g.group(2);
                    String name = arg.startsWith("\"") ? arg.replace("\"", "") : consts.getOrDefault(arg, consts.getOrDefault(arg.substring(arg.lastIndexOf('.') + 1), "?" + arg));
                    String norm = normaliseType(type);
                    @SuppressWarnings("unchecked") Map<String, Object> d = (Map<String, Object>) devices.computeIfAbsent(name, k -> Json.map("name", k, "types", new ArrayList<>(), "files", new ArrayList<>()));
                    @SuppressWarnings("unchecked") List<Object> ts = (List<Object>) d.get("types");
                    if (!ts.contains(norm)) ts.add(norm);
                    @SuppressWarnings("unchecked") List<Object> fs = (List<Object>) d.get("files");
                    if (!fs.contains(r)) fs.add(r);
                }
            }
            if (s.contains("VisionPortal")) vision.add("VisionPortal (" + r + ")");
            if (s.contains("AprilTagProcessor")) vision.add("AprilTagProcessor (" + r + ")");
            if (s.contains("OpenCvPipeline") || s.contains("VisionProcessor")) vision.add("custom raw-frame pipeline (" + r + ")");
            if (s.contains("Limelight3A")) vision.add("Limelight3A (" + r + ")");
            if (s.matches("(?s).*new\\s+Thread\\s*\\(.*") || s.contains("ExecutorService") || s.contains("Executors.")) threads.add(r);
            Matcher am = Pattern.compile("import\\s+(android\\.[\\w.]+)").matcher(s);
            while (am.find()) android.add(am.group(1) + " (" + r + ")");
            if (s.contains("ServerSocket") || s.contains("NanoHTTPD") || s.contains("WebHandler") || s.contains("HttpURLConnection")) network.add(r);
        }
        out.put("opModes", opModes);
        out.put("hardwareDevices", new ArrayList<>(devices.values()));
        out.put("vision", new ArrayList<>(vision));
        out.put("threads", new ArrayList<>(threads));
        out.put("androidApis", new ArrayList<>(android));
        out.put("networkOrWebServers", new ArrayList<>(network));
        return out;
    }

    private static String normaliseType(String t) {
        String s = t.substring(t.lastIndexOf('.') + 1);
        switch (s) {
            case "dcMotor": case "DcMotor": case "DcMotorEx": case "DcMotorSimple": return "motor";
            case "servo": case "Servo": case "ServoImplEx": return "servo";
            case "crservo": case "CRServo": case "CRServoImplEx": return "crservo";
            case "IMU": case "BNO055IMU": case "BHI260IMU": return "imu";
            case "voltageSensor": case "VoltageSensor": return "voltage";
            case "DistanceSensor": case "Rev2mDistanceSensor": return "distance";
            case "colorSensor": case "ColorSensor": case "NormalizedColorSensor": case "RevColorSensorV3": return "color";
            case "touchSensor": case "TouchSensor": return "touch";
            case "digitalChannel": case "DigitalChannel": return "digital";
            case "analogInput": case "AnalogInput": return "analog";
            case "GoBildaPinpointDriver": return "pinpoint";
            case "WebcamName": return "webcam";
            case "LynxModule": return "hub";
            case "Limelight3A": return "limelight (unsupported)";
            default: return s;
        }
    }

    private static List<Object> boundaries(Map<String, Object> scan) {
        List<Object> b = new ArrayList<>();
        b.add("Executes: the team's compiled Java bytecode, its libraries, and the official FTC SDK " +
              "classes that do not need Android (interfaces, Gamepad, ElapsedTime, navigation types, ...).");
        b.add("Replaced by GamePlan shadows (same public API, simulated behaviour): OpMode/LinearOpMode lifecycle, HardwareMap, " +
              "DcMotorImpl(Ex), ServoImpl(Ex), CRServoImpl(Ex), AnalogInput, LynxModule, GoBildaPinpointDriver, RobotLog, IMU, sensors, Telemetry.");
        b.add("Not reproduced: the Android Robot Controller app, Android OS services, USB/Lynx serial protocol, OnBot Java, Blocks.");
        if (!Json.arr(scan.get("vision")).isEmpty())
            b.add("Vision: AprilTag/VisionPortal results are VIRTUAL (generated from scene geometry with a detection-uncertainty model). Raw-frame pipelines and Limelight are not executed.");
        if (!Json.arr(scan.get("threads")).isEmpty())
            b.add("Threads: robot-code threads run and share the virtual clock; matches using them are not bit-reproducible and counterfactual branches are labelled approximate.");
        if (!Json.arr(scan.get("networkOrWebServers")).isEmpty())
            b.add("Network/web servers in robot code cannot open sockets in the sandbox (no network namespace).");
        return b;
    }

    private static Map<String, Object> skeleton(Map<String, Object> scan) {
        List<Object> motors = new ArrayList<>(), servos = new ArrayList<>(), sensors = new ArrayList<>();
        int mp = 0, sp = 0;
        for (Object o : Json.arr(scan.get("hardwareDevices"))) {
            Map<String, Object> d = Json.obj(o);
            String name = (String) d.get("name");
            List<Object> ts = Json.arr(d.get("types"));
            String t = ts.isEmpty() ? "?" : (String) ts.get(0);
            String hub = mp < 4 ? "control" : "expansion";
            switch (t) {
                case "motor": motors.add(Json.map("name", name, "component", "gobilda-5203-bare", "gearbox", "gobilda-yj-19.2", "hub", mp < 4 ? "control" : "expansion", "port", mp % 4,
                    "lead", Json.map("awg", "18AWG", "length_m", 0.45), "_todo", "confirm motor model, gearbox and port")); mp++; break;
                case "servo": case "crservo": servos.add(Json.map("name", name, "component", t.equals("crservo") ? "generic-cr-servo" : "gobilda-2000-0025-0002",
                    "hub", sp < 6 ? "control" : "expansion", "port", sp % 6, "_todo", "confirm servo model and load")); sp++; break;
                default: sensors.add(Json.map("name", name, "type", t)); break;
            }
            if (hub.isEmpty()) break;
        }
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("schema", "gameplan.robot/1");
        m.put("_generatedBy", "GamePlan importer from hardwareMap usage; fill in physical data from CAD / measurements");
        m.put("motors", motors);
        m.put("servos", servos);
        m.put("sensors", sensors);
        return m;
    }

    // ------------------------------------------------------------------ compilers
    private Map<Path, String> javac(List<Path> files, List<String> cp, Path classes) throws IOException {
        Map<Path, String> errs = new LinkedHashMap<>();
        if (files.isEmpty()) return errs;
        JavaCompiler jc = ToolProvider.getSystemJavaCompiler();
        StandardJavaFileManager fm = jc.getStandardFileManager(null, Locale.ROOT, StandardCharsets.UTF_8);
        List<String> opts = new ArrayList<>(Arrays.asList("-d", classes.toString(), "--release", "8", "-encoding", "UTF-8", "-nowarn", "-Xlint:-options",
            "-proc:none", "-cp", String.join(File.pathSeparator, cp)));
        DiagnosticCollector<JavaFileObject> dc = new DiagnosticCollector<>();
        List<File> fl = files.stream().map(Path::toFile).collect(Collectors.toList());
        boolean ok = jc.getTask(null, fm, dc, opts, null, fm.getJavaFileObjectsFromFiles(fl)).call();
        if (ok) return errs;
        for (Diagnostic<? extends JavaFileObject> d : dc.getDiagnostics()) {
            if (d.getKind() != Diagnostic.Kind.ERROR || d.getSource() == null) continue;
            Path p = Paths.get(d.getSource().toUri()).toAbsolutePath().normalize();
            errs.putIfAbsent(p, "line " + d.getLineNumber() + ": " + d.getMessage(Locale.ROOT).split("\n")[0]);
        }
        if (errs.isEmpty()) errs.put(files.get(0), "compiler failed without a file-level diagnostic");
        return errs;
    }

    private boolean compileKotlin(String kotlincJar, List<Path> kt, List<Path> java, List<String> cp, Path classes, Map<String, String> excluded) {
        try {
            List<String> cmd = new ArrayList<>(Arrays.asList(Paths.get(System.getProperty("java.home"), "bin", "java").toString(), "-cp", kotlincJar,
                "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler", "-no-reflect", "-jvm-target", "1.8", "-d", classes.toString(), "-cp", String.join(File.pathSeparator, cp)));
            for (Path p : kt) cmd.add(p.toString());
            for (Path p : java) cmd.add(p.toString());
            Process pr = new ProcessBuilder(cmd).redirectErrorStream(true).start();
            String o = new String(pr.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
            if (pr.waitFor() != 0) { notes.add("kotlinc failed: " + o.substring(0, Math.min(2000, o.length()))); return false; }
            return true;
        } catch (Exception e) {
            notes.add("kotlinc could not run: " + e);
            return false;
        }
    }

    private static void deleteTree(Path p) throws IOException {
        if (!Files.exists(p)) return;
        try (Stream<Path> s = Files.walk(p)) { s.sorted((a, b) -> b.compareTo(a)).forEach(x -> x.toFile().delete()); }
    }

    static String pretty(String json) {
        StringBuilder b = new StringBuilder();
        int ind = 0;
        boolean q = false;
        for (int i = 0; i < json.length(); i++) {
            char c = json.charAt(i);
            if (c == '"' && (i == 0 || json.charAt(i - 1) != '\\')) q = !q;
            if (q) { b.append(c); continue; }
            if (c == '{' || c == '[') { b.append(c).append('\n'); ind += 2; for (int k = 0; k < ind; k++) b.append(' '); }
            else if (c == '}' || c == ']') { b.append('\n'); ind -= 2; for (int k = 0; k < ind; k++) b.append(' '); b.append(c); }
            else if (c == ',') { b.append(",\n"); for (int k = 0; k < ind; k++) b.append(' '); }
            else if (c == ':') b.append(": ");
            else b.append(c);
        }
        return b.toString();
    }
}
