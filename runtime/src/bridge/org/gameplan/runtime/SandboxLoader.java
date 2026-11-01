package org.gameplan.runtime;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.jar.JarEntry;
import java.util.jar.JarFile;

/**
 * Child-first class loader for robot code, its libraries and the FTC SDK. Search order is the
 * order of the roots: [shadow SDK classes, FTC SDK jars, library jars, team code]. Every class is
 * passed through ClassRewriter. The bridge API (org.gameplan.runtime.api) and the JDK come from
 * the parent so there is exactly one copy of the virtual clock.
 */
final class SandboxLoader extends ClassLoader {
    private final List<Object> roots = new ArrayList<>();   // File (dir) or JarFile
    private final List<String> rootNames = new ArrayList<>();
    final Map<String, List<String>> findings = new TreeMap<>();
    int rewrittenClasses, redirectedCalls;
    private final java.util.Set<String> teamRoots = new java.util.HashSet<>();

    SandboxLoader(ClassLoader parent) { super(parent); }

    void addRoot(File f, boolean teamCode) throws IOException {
        if (f.isDirectory()) roots.add(f);
        else roots.add(new JarFile(f));
        rootNames.add(f.getPath());
        if (teamCode) teamRoots.add(f.getPath());
    }

    private static boolean parentFirst(String name) {
        return name.startsWith("java.") || name.startsWith("javax.") || name.startsWith("jdk.") || name.startsWith("sun.")
            || name.startsWith("com.sun.") || name.startsWith("org.gameplan.runtime.") || name.startsWith("org.w3c.") || name.startsWith("org.xml.");
    }

    @Override
    protected Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
        synchronized (getClassLoadingLock(name)) {
            Class<?> c = findLoadedClass(name);
            if (c == null) {
                if (parentFirst(name)) c = getParent().loadClass(name);
                else {
                    try { c = findClass(name); } catch (ClassNotFoundException e) { c = getParent().loadClass(name); }
                }
            }
            if (resolve) resolveClass(c);
            return c;
        }
    }

    @Override
    protected Class<?> findClass(String name) throws ClassNotFoundException {
        String path = name.replace('.', '/') + ".class";
        for (int i = 0; i < roots.size(); i++) {
            byte[] b = read(roots.get(i), path);
            if (b == null) continue;
            try {
                ClassRewriter.Result r = ClassRewriter.rewrite(b, true);
                if (r.redirected > 0) { rewrittenClasses++; redirectedCalls += r.redirected; }
                if (teamRoots.contains(rootNames.get(i)) && !r.findings.isEmpty()) findings.put(name, new ArrayList<>(r.findings));
                return defineClass(name, r.bytes, 0, r.bytes.length);
            } catch (IOException e) {
                throw new ClassNotFoundException(name + ": " + e.getMessage());
            }
        }
        throw new ClassNotFoundException(name);
    }

    private static byte[] read(Object root, String path) {
        try {
            if (root instanceof File) {
                File f = new File((File) root, path);
                return f.isFile() ? Files.readAllBytes(f.toPath()) : null;
            }
            JarFile j = (JarFile) root;
            JarEntry e = j.getJarEntry(path);
            if (e == null) return null;
            try (InputStream in = j.getInputStream(e)) {
                ByteArrayOutputStream o = new ByteArrayOutputStream();
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
                return o.toByteArray();
            }
        } catch (IOException e) {
            return null;
        }
    }

    @Override
    protected URL findResource(String name) {
        for (Object r : roots) {
            try {
                if (r instanceof File) {
                    File f = new File((File) r, name);
                    if (f.exists()) return f.toURI().toURL();
                } else {
                    JarFile j = (JarFile) r;
                    if (j.getJarEntry(name) != null) return new URL("jar:" + new File(j.getName()).toURI() + "!/" + name);
                }
            } catch (IOException ignored) {}
        }
        return null;
    }

    @Override
    protected Enumeration<URL> findResources(String name) {
        URL u = findResource(name);
        return u == null ? Collections.emptyEnumeration() : Collections.enumeration(Collections.singletonList(u));
    }

    /** All class names under the team-code roots. */
    List<String> teamClasses() throws IOException {
        List<String> out = new ArrayList<>();
        for (int i = 0; i < roots.size(); i++) {
            if (!teamRoots.contains(rootNames.get(i))) continue;
            Object r = roots.get(i);
            if (r instanceof File) {
                File base = (File) r;
                try (java.util.stream.Stream<java.nio.file.Path> s = Files.walk(base.toPath())) {
                    s.filter(p -> p.toString().endsWith(".class")).forEach(p -> {
                        String rel = base.toPath().relativize(p).toString().replace(File.separatorChar, '/');
                        out.add(rel.substring(0, rel.length() - 6).replace('/', '.'));
                    });
                }
            } else {
                Enumeration<JarEntry> en = ((JarFile) r).entries();
                while (en.hasMoreElements()) {
                    String n = en.nextElement().getName();
                    if (n.endsWith(".class")) out.add(n.substring(0, n.length() - 6).replace('/', '.'));
                }
            }
        }
        Collections.sort(out);
        return out;
    }
}
