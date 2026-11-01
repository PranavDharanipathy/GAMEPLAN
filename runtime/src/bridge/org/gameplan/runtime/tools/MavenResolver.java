package org.gameplan.runtime.tools;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import javax.xml.parsers.DocumentBuilderFactory;

import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.Node;
import org.w3c.dom.NodeList;

/**
 * Resolves Maven coordinates declared in an FTC Gradle project to local jars (transitively, with
 * parent POM properties, dependencyManagement/BOM versions, exclusions and highest-version
 * conflict resolution like Gradle). AARs are reduced to classes.jar. Downloads are cached and
 * SHA-1 verified when the repository publishes checksums. Works offline from the cache.
 */
public final class MavenResolver {
    public static final class Coord {
        public final String g, a, v;
        public String packaging = "jar";
        public Coord(String g, String a, String v) { this.g = g; this.a = a; this.v = v; }
        public String key() { return g + ":" + a; }
        @Override public String toString() { return g + ":" + a + ":" + v; }
    }

    public static final class Resolved {
        public final Map<String, Path> jars = new LinkedHashMap<>();       // key -> jar
        public final Map<String, String> versions = new LinkedHashMap<>();
        public final List<String> skipped = new ArrayList<>();             // with reason
        public final List<String> failed = new ArrayList<>();
    }

    private final List<String> repos;
    private final Path cache;
    private final boolean offline;
    private final HttpClient http = HttpClient.newBuilder().followRedirects(HttpClient.Redirect.NORMAL).build();
    private final Map<String, Document> pomCache = new HashMap<>();
    public final Set<String> providedGroups = new HashSet<>();   // e.g. org.firstinspires.ftc (supplied by the SDK provisioner)

    public MavenResolver(List<String> repos, Path cache, boolean offline) {
        this.repos = repos;
        this.cache = cache;
        this.offline = offline;
    }

    public Resolved resolve(List<Coord> roots, Map<String, Set<String>> rootExclusions) {
        Resolved r = new Resolved();
        Map<String, Coord> chosen = new LinkedHashMap<>();
        Deque<Object[]> q = new ArrayDeque<>();   // {coord, exclusions}
        for (Coord c : roots) q.add(new Object[]{c, rootExclusions.getOrDefault(c.key(), new HashSet<>())});
        Set<String> visited = new HashSet<>();
        while (!q.isEmpty()) {
            Object[] item = q.poll();
            Coord c = (Coord) item[0];
            @SuppressWarnings("unchecked") Set<String> excl = (Set<String>) item[1];
            if (providedGroups.contains(c.g)) { r.skipped.add(c + " (provided by the simulator's FTC SDK " + "runtime)"); continue; }
            if (c.g.startsWith("androidx.") || c.g.startsWith("com.android") || c.g.equals("android")) { r.skipped.add(c + " (Android framework library - not available on the desktop runtime)"); continue; }
            Coord prev = chosen.get(c.key());
            if (prev != null && compareVersions(prev.v, c.v) >= 0) continue;
            chosen.put(c.key(), c);
            if (!visited.add(c.toString())) continue;
            Document pom;
            try {
                pom = pom(c);
            } catch (Exception e) {
                r.failed.add(c + ": " + e.getMessage());
                continue;
            }
            if (pom == null) { r.failed.add(c + ": POM not found" + (lastError != null ? " (" + lastError + ")" : "")); lastError = null; continue; }
            String pk = text(pom.getDocumentElement(), "packaging");
            if (pk != null) c.packaging = pk;
            List<Coord> ds;
            try { ds = dependencies(pom, c, excl); } catch (Exception e) { r.failed.add(c + " (POM): " + e.getMessage()); continue; }
            for (Coord d : ds) {
                Set<String> e2 = new HashSet<>(excl);
                e2.addAll(depExclusions.getOrDefault(d.toString(), new HashSet<>()));
                q.add(new Object[]{d, e2});
            }
        }
        for (Coord c : chosen.values()) {
            if (c.packaging.equals("pom") || c.packaging.equals("bom")) continue;
            try {
                Path jar = artifact(c);
                if (jar != null) { r.jars.put(c.key(), jar); r.versions.put(c.key(), c.v); }
                else { r.failed.add(c + ": artifact not found" + (lastError != null ? " (" + lastError + ")" : "")); lastError = null; }
            } catch (Exception e) {
                r.failed.add(c + ": " + e.getMessage());
            }
        }
        return r;
    }

    private final Map<String, Set<String>> depExclusions = new HashMap<>();
    private String lastError;

    private List<Coord> dependencies(Document pom, Coord self, Set<String> excl) throws Exception {
        Map<String, String> props = properties(pom, self);
        Map<String, String> managed = managed(pom, props, 0);
        List<Coord> out = new ArrayList<>();
        Element deps = child(pom.getDocumentElement(), "dependencies");
        if (deps == null) return out;
        NodeList nl = deps.getElementsByTagName("dependency");
        for (int i = 0; i < nl.getLength(); i++) {
            Element d = (Element) nl.item(i);
            if (d.getParentNode() != deps) continue;
            String g = subst(text(d, "groupId"), props), a = subst(text(d, "artifactId"), props), v = subst(text(d, "version"), props);
            String scope = text(d, "scope"), optional = text(d, "optional");
            if ("test".equals(scope) || "provided".equals(scope) || "system".equals(scope) || "true".equals(optional)) continue;
            if (excl.contains(g + ":" + a) || excl.contains(g + ":*")) continue;
            if (v == null || v.isEmpty()) v = managed.get(g + ":" + a);
            if (v == null) continue;
            v = cleanVersion(v);
            Coord c = new Coord(g, a, v);
            Element ex = child(d, "exclusions");
            if (ex != null) {
                Set<String> s = new HashSet<>();
                NodeList el = ex.getElementsByTagName("exclusion");
                for (int k = 0; k < el.getLength(); k++) s.add(text((Element) el.item(k), "groupId") + ":" + text((Element) el.item(k), "artifactId"));
                depExclusions.put(c.toString(), s);
            }
            out.add(c);
        }
        return out;
    }

    private static String cleanVersion(String v) {
        v = v.trim();
        if (v.startsWith("[") || v.startsWith("(")) {   // version range: take the lower bound
            String inner = v.substring(1, v.length() - 1);
            String lo = inner.split(",")[0].trim();
            return lo.isEmpty() ? inner.split(",")[1].trim() : lo;
        }
        return v;
    }

    private Map<String, String> properties(Document pom, Coord self) throws Exception {
        Map<String, String> p = new HashMap<>();
        Element parent = child(pom.getDocumentElement(), "parent");
        if (parent != null) {
            Coord pc = new Coord(text(parent, "groupId"), text(parent, "artifactId"), text(parent, "version"));
            Document pp = pom(pc);
            if (pp != null) p.putAll(properties(pp, pc));
        }
        Element props = child(pom.getDocumentElement(), "properties");
        if (props != null) {
            NodeList nl = props.getChildNodes();
            for (int i = 0; i < nl.getLength(); i++) if (nl.item(i) instanceof Element) p.put(nl.item(i).getNodeName(), nl.item(i).getTextContent().trim());
        }
        p.put("project.version", self.v);
        p.put("project.groupId", self.g);
        p.put("version", self.v);
        if (parent != null) p.put("project.parent.version", text(parent, "version"));
        return p;
    }

    private Map<String, String> managed(Document pom, Map<String, String> props, int depth) throws Exception {
        Map<String, String> m = new HashMap<>();
        if (depth > 6) return m;
        Element parent = child(pom.getDocumentElement(), "parent");
        if (parent != null) {
            Document pp = pom(new Coord(text(parent, "groupId"), text(parent, "artifactId"), text(parent, "version")));
            if (pp != null) m.putAll(managed(pp, props, depth + 1));
        }
        Element dm = child(pom.getDocumentElement(), "dependencyManagement");
        if (dm == null) return m;
        Element deps = child(dm, "dependencies");
        if (deps == null) return m;
        NodeList nl = deps.getElementsByTagName("dependency");
        for (int i = 0; i < nl.getLength(); i++) {
            Element d = (Element) nl.item(i);
            String g = subst(text(d, "groupId"), props), a = subst(text(d, "artifactId"), props), v = subst(text(d, "version"), props);
            if ("import".equals(text(d, "scope"))) {
                Document bom = pom(new Coord(g, a, v));
                if (bom != null) m.putAll(managed(bom, properties(bom, new Coord(g, a, v)), depth + 1));
            } else if (v != null) m.put(g + ":" + a, v);
        }
        return m;
    }

    private static String subst(String s, Map<String, String> p) {
        if (s == null) return null;
        Matcher m = Pattern.compile("\\$\\{([^}]+)}").matcher(s);
        StringBuffer b = new StringBuffer();
        while (m.find()) m.appendReplacement(b, Matcher.quoteReplacement(p.getOrDefault(m.group(1), m.group(0))));
        m.appendTail(b);
        return b.toString();
    }

    private Document pom(Coord c) throws Exception {
        String k = c.toString();
        if (pomCache.containsKey(k)) return pomCache.get(k);
        byte[] b = fetch(c, "pom");
        Document d = null;
        if (b != null) {
            DocumentBuilderFactory f = DocumentBuilderFactory.newInstance();
            f.setFeature("http://apache.org/xml/features/disallow-doctype-decl", true);   // no XXE
            f.setNamespaceAware(false);
            d = f.newDocumentBuilder().parse(new ByteArrayInputStream(b));
        }
        pomCache.put(k, d);
        return d;
    }

    private Path artifact(Coord c) throws Exception {
        String ext = c.packaging.equals("aar") ? "aar" : "jar";
        if (c.packaging.equals("bundle") || c.packaging.equals("maven-plugin")) ext = "jar";
        Path out = cache.resolve(c.g.replace('.', '/')).resolve(c.a).resolve(c.v).resolve(c.a + "-" + c.v + ".jar");
        if (Files.exists(out)) return out;
        byte[] b = fetch(c, ext);
        if (b == null && ext.equals("jar")) { b = fetch(c, "aar"); ext = b == null ? ext : "aar"; }
        if (b == null) return null;
        Files.createDirectories(out.getParent());
        if (ext.equals("aar")) {
            try (ZipInputStream z = new ZipInputStream(new ByteArrayInputStream(b))) {
                ZipEntry e;
                while ((e = z.getNextEntry()) != null) if (e.getName().equals("classes.jar")) { Files.copy(z, out); return out; }
            }
            return null;
        }
        Files.write(out, b);
        return out;
    }

    private byte[] fetch(Coord c, String ext) throws Exception {
        String rel = c.g.replace('.', '/') + "/" + c.a + "/" + c.v + "/" + c.a + "-" + c.v + "." + ext;
        Path cached = cache.resolve("raw").resolve(rel);
        if (Files.exists(cached)) return Files.readAllBytes(cached);
        if (offline) return null;
        for (String repo : repos) {
            String url = (repo.endsWith("/") ? repo : repo + "/") + rel;
            try {
                HttpResponse<byte[]> r = null;
                for (int attempt = 0; attempt < 5; attempt++) {
                    r = http.send(HttpRequest.newBuilder(URI.create(url)).build(), HttpResponse.BodyHandlers.ofByteArray());
                    if (r.statusCode() != 429 && r.statusCode() < 500) break;
                    Thread.sleep(1000L << attempt);   // repository rate limiting / transient error: back off
                }
                if (r.statusCode() == 429 || r.statusCode() >= 500) lastError = "HTTP " + r.statusCode() + " from " + repo + " (rate limited / unavailable)";
                if (r.statusCode() != 200) continue;
                byte[] body = r.body();
                HttpResponse<String> s = http.send(HttpRequest.newBuilder(URI.create(url + ".sha1")).build(), HttpResponse.BodyHandlers.ofString());
                if (s.statusCode() == 200) {
                    String exp = s.body().trim().split("\\s+")[0];
                    if (!sha1(body).equalsIgnoreCase(exp)) throw new SecurityException("checksum mismatch: " + url);
                }
                Files.createDirectories(cached.getParent());
                Files.write(cached, body);
                return body;
            } catch (IOException e) {
                // try next repository
            }
        }
        return null;
    }

    static String sha1(byte[] b) throws Exception {
        StringBuilder h = new StringBuilder();
        for (byte x : MessageDigest.getInstance("SHA-1").digest(b)) h.append(String.format("%02x", x));
        return h.toString();
    }

    private static Element child(Element e, String name) {
        if (e == null) return null;
        for (Node n = e.getFirstChild(); n != null; n = n.getNextSibling()) if (n instanceof Element && n.getNodeName().equals(name)) return (Element) n;
        return null;
    }

    private static String text(Element e, String name) {
        Element c = child(e, name);
        return c == null ? null : c.getTextContent().trim();
    }

    public static int compareVersions(String a, String b) {
        String[] x = a.split("[.\\-+]"), y = b.split("[.\\-+]");
        for (int i = 0; i < Math.max(x.length, y.length); i++) {
            String p = i < x.length ? x[i] : "0", q = i < y.length ? y[i] : "0";
            boolean pn = p.matches("\\d+"), qn = q.matches("\\d+");
            int c;
            if (pn && qn) c = Long.compare(Long.parseLong(p), Long.parseLong(q));
            else if (pn) c = 1;
            else if (qn) c = -1;
            else c = p.compareTo(q);
            if (c != 0) return c;
        }
        return 0;
    }
}
