import java.io.*;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.jar.*;
import java.util.stream.*;
import java.util.zip.*;
import javax.tools.*;

/**
 * Cross-platform build of the GamePlan FTC runtime. Needs only a JDK (11+):
 *
 *   java runtime/tools/Build.java [--sdk 12.0.0] [--offline]
 *
 * 1. provisions the official FTC SDK jars for the requested SDK version from Maven Central
 *    (AAR -> classes.jar, SHA-1 verified against the repository checksum) into runtime/sdk-cache/
 * 2. compiles the bridge (gameplan-runtime.jar) and the SDK shadows (gameplan-shadow-<ver>.jar)
 *    against the real SDK jars, so every shadow is checked against the real SDK signatures.
 */
public class Build {
    static final String CENTRAL = "https://repo1.maven.org/maven2/";
    static final String[] SDK_ARTIFACTS = {"RobotCore", "Hardware", "FtcCommon", "Vision"};
    static final String[][] SUPPORT = {{"com.google.code.gson", "gson", "2.10.1"}};

    public static void main(String[] args) throws Exception {
        String sdk = "12.0.0";
        boolean offline = false;
        for (int i = 0; i < args.length; i++) {
            if (args[i].equals("--sdk")) sdk = args[++i];
            if (args[i].equals("--offline")) offline = true;
        }
        Path root = Paths.get(Build.class.getProtectionDomain().getCodeSource() == null ? "runtime" : "runtime");
        if (!Files.isDirectory(root.resolve("src"))) root = Paths.get(".");
        if (!Files.isDirectory(root.resolve("src"))) throw new IllegalStateException("run from the GAMEPLAN repository root or runtime/");
        Path cache = root.resolve("sdk-cache").resolve(sdk);
        Files.createDirectories(cache);
        List<Path> sdkJars = new ArrayList<>();
        for (String a : SDK_ARTIFACTS) sdkJars.add(provisionAar(cache, "org.firstinspires.ftc", a, sdk, offline));
        for (String[] s : SUPPORT) sdkJars.add(provisionJar(cache, s[0], s[1], s[2], offline));
        Path build = root.resolve("build");
        Path bridgeOut = build.resolve("classes/bridge"), shadowOut = build.resolve("classes/shadow-" + sdk);
        deleteTree(bridgeOut);
        deleteTree(shadowOut);
        compile(root.resolve("src/bridge"), bridgeOut, List.of(), "11");
        jar(bridgeOut, build.resolve("gameplan-runtime.jar"), "org.gameplan.runtime.RuntimeMain");
        List<Path> cp = new ArrayList<>(sdkJars);
        cp.add(0, bridgeOut);
        compile(root.resolve("src/shadow"), shadowOut, cp, "11");
        jar(shadowOut, build.resolve("gameplan-shadow-" + sdk + ".jar"), null);
        // SDK manifest consumed by the importer
        StringBuilder m = new StringBuilder("{\n  \"sdkVersion\": \"" + sdk + "\",\n  \"jars\": [");
        for (int i = 0; i < sdkJars.size(); i++) m.append(i > 0 ? ", " : "").append('"').append(sdkJars.get(i).toAbsolutePath().toString().replace("\\", "/")).append('"');
        m.append("],\n  \"shadow\": \"").append(build.resolve("gameplan-shadow-" + sdk + ".jar").toAbsolutePath().toString().replace("\\", "/")).append("\"\n}\n");
        Files.writeString(build.resolve("sdk-" + sdk + ".json"), m.toString());
        System.out.println("built " + build.resolve("gameplan-runtime.jar") + " and gameplan-shadow-" + sdk + ".jar");
    }

    static Path provisionAar(Path cache, String g, String a, String v, boolean offline) throws Exception {
        Path out = cache.resolve(a + ".jar");
        if (Files.exists(out)) return out;
        if (offline) throw new IllegalStateException("offline and missing " + out);
        String base = CENTRAL + g.replace('.', '/') + "/" + a + "/" + v + "/" + a + "-" + v + ".aar";
        byte[] aar = fetch(base);
        verifySha1(aar, new String(fetch(base + ".sha1")).trim().split("\\s+")[0], base);
        try (ZipInputStream z = new ZipInputStream(new ByteArrayInputStream(aar))) {
            ZipEntry e;
            while ((e = z.getNextEntry()) != null) {
                if (e.getName().equals("classes.jar")) { Files.copy(z, out); break; }
            }
        }
        System.out.println("provisioned " + a + " " + v + " (sha1 verified)");
        return out;
    }

    static Path provisionJar(Path cache, String g, String a, String v, boolean offline) throws Exception {
        Path out = cache.resolve(a + "-" + v + ".jar");
        if (Files.exists(out)) return out;
        if (offline) throw new IllegalStateException("offline and missing " + out);
        String url = CENTRAL + g.replace('.', '/') + "/" + a + "/" + v + "/" + a + "-" + v + ".jar";
        byte[] b = fetch(url);
        verifySha1(b, new String(fetch(url + ".sha1")).trim().split("\\s+")[0], url);
        Files.write(out, b);
        return out;
    }

    static byte[] fetch(String url) throws Exception {
        HttpClient c = HttpClient.newBuilder().followRedirects(HttpClient.Redirect.NORMAL).build();
        HttpResponse<byte[]> r = c.send(HttpRequest.newBuilder(URI.create(url)).build(), HttpResponse.BodyHandlers.ofByteArray());
        if (r.statusCode() != 200) throw new IOException("HTTP " + r.statusCode() + " for " + url);
        return r.body();
    }

    static void verifySha1(byte[] data, String expected, String what) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-1");
        StringBuilder h = new StringBuilder();
        for (byte x : md.digest(data)) h.append(String.format("%02x", x));
        if (!h.toString().equalsIgnoreCase(expected)) throw new SecurityException("checksum mismatch for " + what);
    }

    static void compile(Path src, Path out, List<Path> cp, String release) throws Exception {
        Files.createDirectories(out);
        List<File> files;
        try (Stream<Path> s = Files.walk(src)) { files = s.filter(p -> p.toString().endsWith(".java")).map(Path::toFile).collect(Collectors.toList()); }
        JavaCompiler jc = ToolProvider.getSystemJavaCompiler();
        StandardJavaFileManager fm = jc.getStandardFileManager(null, null, null);
        List<String> opts = new ArrayList<>(List.of("-d", out.toString(), "--release", release, "-encoding", "UTF-8", "-Xlint:-options", "-nowarn"));
        if (!cp.isEmpty()) { opts.add("-cp"); opts.add(cp.stream().map(Path::toString).collect(Collectors.joining(File.pathSeparator))); }
        DiagnosticCollector<JavaFileObject> dc = new DiagnosticCollector<>();
        boolean ok = jc.getTask(null, fm, dc, opts, null, fm.getJavaFileObjectsFromFiles(files)).call();
        for (Diagnostic<? extends JavaFileObject> d : dc.getDiagnostics())
            if (d.getKind() == Diagnostic.Kind.ERROR) System.err.println(d.getSource() == null ? "" : d.getSource().getName() + ":" + d.getLineNumber() + ": " + d.getMessage(null));
        if (!ok) throw new IllegalStateException("compilation failed: " + src);
    }

    static void jar(Path dir, Path out, String main) throws Exception {
        Manifest mf = new Manifest();
        mf.getMainAttributes().put(Attributes.Name.MANIFEST_VERSION, "1.0");
        if (main != null) mf.getMainAttributes().put(Attributes.Name.MAIN_CLASS, main);
        List<Path> files;
        try (Stream<Path> s = Files.walk(dir)) { files = s.filter(Files::isRegularFile).sorted().collect(Collectors.toList()); }
        try (JarOutputStream j = new JarOutputStream(Files.newOutputStream(out), mf)) {
            for (Path f : files) {
                JarEntry e = new JarEntry(dir.relativize(f).toString().replace('\\', '/'));
                e.setTime(315532800000L);  // reproducible jars
                j.putNextEntry(e);
                Files.copy(f, j);
                j.closeEntry();
            }
        }
    }

    static void deleteTree(Path p) throws IOException {
        if (!Files.exists(p)) return;
        try (Stream<Path> s = Files.walk(p)) { s.sorted(Comparator.reverseOrder()).forEach(x -> x.toFile().delete()); }
    }
}
