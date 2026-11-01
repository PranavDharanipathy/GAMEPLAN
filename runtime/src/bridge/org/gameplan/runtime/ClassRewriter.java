package org.gameplan.runtime;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Load-time bytecode transformation of robot code and libraries (never of the source tree):
 *  - System.nanoTime/currentTimeMillis, Thread.sleep/yield/onSpinWait  -> virtual-time Sim.* methods
 *  - System.exit                                                         -> Sim.exit (stops the OpMode, not the simulator)
 *  - ProcessBuilder.start / Runtime.exec                                 -> Sim-side denial (SecurityException)
 * plus a constant-pool scan that reports risky / unsupported API references.
 *
 * Implementation: the constant pool is re-emitted with appended entries; Methodref entries are
 * re-pointed; for instance->static redirections the invokevirtual opcodes in Code attributes are
 * patched to invokestatic (same instruction length).
 */
final class ClassRewriter {
    static final String SIM = "org/gameplan/runtime/api/Sim";
    static final String DENY = "org/gameplan/runtime/api/Deny";

    static final class Result {
        byte[] bytes;
        int redirected;
        final Set<String> findings = new LinkedHashSet<>();
        final Set<String> referencedClasses = new LinkedHashSet<>();
    }

    private static final String[][] STATIC_REDIRECTS = {
        {"java/lang/System", "nanoTime", "()J", SIM, "nanoTime"},
        {"java/lang/System", "currentTimeMillis", "()J", SIM, "currentTimeMillis"},
        {"java/lang/System", "exit", "(I)V", SIM, "exit"},
        {"java/lang/Thread", "sleep", "(J)V", SIM, "sleep"},
        {"java/lang/Thread", "sleep", "(JI)V", SIM, "sleep"},
        {"java/lang/Thread", "sleep", "(Ljava/time/Duration;)V", SIM, "sleep"},
        {"java/lang/Thread", "yield", "()V", SIM, "yieldThread"},
        {"java/lang/Thread", "onSpinWait", "()V", SIM, "onSpinWait"},
        {"android/os/SystemClock", "sleep", "(J)V", SIM, "sleep"},
        {"android/os/SystemClock", "uptimeMillis", "()J", SIM, "uptimeMillis"},
        {"android/os/SystemClock", "elapsedRealtime", "()J", SIM, "uptimeMillis"},
        {"android/os/SystemClock", "elapsedRealtimeNanos", "()J", SIM, "nanoTime"},
    };
    // instance method -> static (receiver becomes first argument)
    private static final String[][] VIRTUAL_REDIRECTS = {
        {"java/lang/ProcessBuilder", "start", "()Ljava/lang/Process;", DENY, "start", "(Ljava/lang/ProcessBuilder;)Ljava/lang/Process;"},
        {"java/lang/Runtime", "exec", "(Ljava/lang/String;)Ljava/lang/Process;", DENY, "exec", "(Ljava/lang/Runtime;Ljava/lang/String;)Ljava/lang/Process;"},
        {"java/lang/Runtime", "exec", "([Ljava/lang/String;)Ljava/lang/Process;", DENY, "exec", "(Ljava/lang/Runtime;[Ljava/lang/String;)Ljava/lang/Process;"},
        {"java/lang/Runtime", "halt", "(I)V", DENY, "halt", "(Ljava/lang/Runtime;I)V"},
    };
    private static final String[][] RISKY = {
        {"java/net/Socket", "network socket (blocked by the OS sandbox network namespace where available)"},
        {"java/net/ServerSocket", "listening socket (e.g. an on-robot web server) - not available in simulation"},
        {"java/net/URL", "URL access"},
        {"java/net/HttpURLConnection", "HTTP access"},
        {"java/net/http/HttpClient", "HTTP access"},
        {"java/io/FileOutputStream", "file writes (sandboxed working directory)"},
        {"java/io/FileWriter", "file writes (sandboxed working directory)"},
        {"java/io/RandomAccessFile", "file access"},
        {"java/lang/ProcessBuilder", "process creation (denied)"},
        {"sun/misc/Unsafe", "sun.misc.Unsafe"},
        {"java/lang/System", ""},
    };

    static Result rewrite(byte[] in, boolean transform) throws IOException {
        Result res = new Result();
        ByteBuffer b = ByteBuffer.wrap(in);
        if (b.getInt() != 0xCAFEBABE) throw new IOException("not a class file");
        int minor = b.getShort() & 0xffff, major = b.getShort() & 0xffff;
        int cpCount = b.getShort() & 0xffff;
        Object[] cp = new Object[cpCount];
        int[] tags = new int[cpCount];
        int cpStart = b.position();
        for (int i = 1; i < cpCount; i++) {
            int tag = b.get() & 0xff;
            tags[i] = tag;
            switch (tag) {
                case 1: { int n = b.getShort() & 0xffff; byte[] x = new byte[n]; b.get(x); cp[i] = modifiedUtf8(x); break; }
                case 3: case 4: cp[i] = new int[]{b.getInt()}; break;
                case 5: case 6: cp[i] = new long[]{b.getLong()}; i++; break;
                case 7: case 8: case 16: case 19: case 20: cp[i] = new int[]{b.getShort() & 0xffff}; break;
                case 9: case 10: case 11: case 12: case 17: case 18: cp[i] = new int[]{b.getShort() & 0xffff, b.getShort() & 0xffff}; break;
                case 15: cp[i] = new int[]{b.get() & 0xff, b.getShort() & 0xffff}; break;
                default: throw new IOException("bad constant pool tag " + tag);
            }
        }
        int cpEnd = b.position();
        // scan
        for (int i = 1; i < cpCount; i++) {
            if (tags[i] == 7) {
                String cn = (String) cp[((int[]) cp[i])[0]];
                res.referencedClasses.add(cn);
                for (String[] r : RISKY) if (cn.equals(r[0]) && !r[1].isEmpty()) res.findings.add(r[1]);
                if (cn.startsWith("android/") && !cn.startsWith("android/os/SystemClock")) res.findings.add("Android API: " + cn.replace('/', '.'));
            }
        }
        if (!transform) { res.bytes = in; return res; }
        // find method refs to redirect
        List<Object[]> extra = new ArrayList<>();   // appended cp entries: {tag, payload}
        int next = cpCount;
        Map<String, Integer> utf = new HashMap<>(), cls = new HashMap<>(), nat = new HashMap<>();
        java.util.function.Function<String, Integer> addUtf = s -> {
            return utf.computeIfAbsent(s, k -> { extra.add(new Object[]{1, k}); return cpCount + extra.size() - 1; });
        };
        Map<Integer, int[]> newMethodref = new HashMap<>();
        Set<Integer> toStaticIdx = new java.util.HashSet<>();
        for (int i = 1; i < cpCount; i++) {
            if (tags[i] != 10) continue;
            int[] mr = (int[]) cp[i];
            String owner = (String) cp[((int[]) cp[mr[0]])[0]];
            int[] nt = (int[]) cp[mr[1]];
            String name = (String) cp[nt[0]], desc = (String) cp[nt[1]];
            for (String[] r : STATIC_REDIRECTS) {
                if (owner.equals(r[0]) && name.equals(r[1]) && desc.equals(r[2])) {
                    int c = cls.computeIfAbsent(r[3], k -> { int u = addUtf.apply(k); extra.add(new Object[]{7, u}); return cpCount + extra.size() - 1; });
                    int n = nat.computeIfAbsent(r[4] + r[2], k -> { int a = addUtf.apply(r[4]); int d = addUtf.apply(r[2]); extra.add(new Object[]{12, new int[]{a, d}}); return cpCount + extra.size() - 1; });
                    newMethodref.put(i, new int[]{c, n});
                    res.redirected++;
                }
            }
            for (String[] r : VIRTUAL_REDIRECTS) {
                if (owner.equals(r[0]) && name.equals(r[1]) && desc.equals(r[2])) {
                    int c = cls.computeIfAbsent(r[3], k -> { int u = addUtf.apply(k); extra.add(new Object[]{7, u}); return cpCount + extra.size() - 1; });
                    int n = nat.computeIfAbsent(r[4] + r[5], k -> { int a = addUtf.apply(r[4]); int d = addUtf.apply(r[5]); extra.add(new Object[]{12, new int[]{a, d}}); return cpCount + extra.size() - 1; });
                    newMethodref.put(i, new int[]{c, n});
                    toStaticIdx.add(i);
                    res.redirected++;
                }
            }
        }
        if (newMethodref.isEmpty()) { res.bytes = in; return res; }
        // rest of the class (after the constant pool), patched for invokevirtual -> invokestatic
        byte[] rest = new byte[in.length - cpEnd];
        System.arraycopy(in, cpEnd, rest, 0, rest.length);
        if (!toStaticIdx.isEmpty()) patchInvokes(rest, cp, tags, toStaticIdx);
        ByteArrayOutputStream bo = new ByteArrayOutputStream(in.length + 256);
        DataOutputStream o = new DataOutputStream(bo);
        o.writeInt(0xCAFEBABE);
        o.writeShort(minor);
        o.writeShort(major);
        o.writeShort(cpCount + extra.size());
        // original pool with re-pointed methodrefs
        ByteBuffer src = ByteBuffer.wrap(in, cpStart, cpEnd - cpStart);
        for (int i = 1; i < cpCount; i++) {
            int tag = src.get() & 0xff;
            int len;
            switch (tag) {
                case 1: len = src.getShort() & 0xffff; o.writeByte(1); o.writeShort(len); byte[] x = new byte[len]; src.get(x); o.write(x); continue;
                case 5: case 6: len = 8; break;
                case 3: case 4: len = 4; break;
                case 7: case 8: case 16: case 19: case 20: len = 2; break;
                case 15: len = 3; break;
                default: len = 4;
            }
            byte[] body = new byte[len];
            src.get(body);
            if (tag == 10 && newMethodref.containsKey(i)) {
                int[] m = newMethodref.get(i);
                o.writeByte(10);
                o.writeShort(m[0]);
                o.writeShort(m[1]);
            } else {
                o.writeByte(tag);
                o.write(body);
            }
            if (tag == 5 || tag == 6) i++;
        }
        for (Object[] e : extra) {
            int tag = (Integer) e[0];
            o.writeByte(tag);
            if (tag == 1) o.writeUTF((String) e[1]);
            else if (tag == 7) o.writeShort((Integer) e[1]);
            else if (tag == 12) { int[] v = (int[]) e[1]; o.writeShort(v[0]); o.writeShort(v[1]); }
        }
        o.write(rest);
        res.bytes = bo.toByteArray();
        return res;
    }

    /** Walk methods' Code attributes and turn invokevirtual #idx into invokestatic #idx for redirected refs. */
    private static void patchInvokes(byte[] rest, Object[] cp, int[] tags, Set<Integer> idx) {
        ByteBuffer b = ByteBuffer.wrap(rest);
        b.getShort(); b.getShort(); b.getShort();          // access, this, super
        int ifc = b.getShort() & 0xffff;
        b.position(b.position() + 2 * ifc);
        int fields = b.getShort() & 0xffff;
        for (int f = 0; f < fields; f++) { b.position(b.position() + 6); skipAttributes(b); }
        int methods = b.getShort() & 0xffff;
        for (int m = 0; m < methods; m++) {
            b.position(b.position() + 6);
            int ac = b.getShort() & 0xffff;
            for (int a = 0; a < ac; a++) {
                int nameIdx = b.getShort() & 0xffff;
                int len = b.getInt();
                int start = b.position();
                if ("Code".equals(cp[nameIdx])) {
                    b.position(start + 4);
                    int codeLen = b.getInt();
                    int code = b.position();
                    int pc = 0;
                    while (pc < codeLen) {
                        int op = rest[code + pc] & 0xff;
                        if (op == 0xb6) {  // invokevirtual
                            int ref = ((rest[code + pc + 1] & 0xff) << 8) | (rest[code + pc + 2] & 0xff);
                            if (idx.contains(ref)) rest[code + pc] = (byte) 0xb8;
                        }
                        pc += insnLength(rest, code, pc);
                    }
                }
                b.position(start + len);
            }
        }
    }

    private static void skipAttributes(ByteBuffer b) {
        int n = b.getShort() & 0xffff;
        for (int i = 0; i < n; i++) { b.getShort(); int len = b.getInt(); b.position(b.position() + len); }
    }

    private static int insnLength(byte[] c, int code, int pc) {
        int op = c[code + pc] & 0xff;
        switch (op) {
            case 0xaa: { // tableswitch
                int p = (pc + 4) & ~3;
                int lo = readInt(c, code + p + 4), hi = readInt(c, code + p + 8);
                return p - pc + 12 + 4 * (hi - lo + 1);
            }
            case 0xab: { // lookupswitch
                int p = (pc + 4) & ~3;
                int n = readInt(c, code + p + 4);
                return p - pc + 8 + 8 * n;
            }
            case 0xc4: { // wide
                int op2 = c[code + pc + 1] & 0xff;
                return op2 == 0x84 ? 6 : 4;
            }
            default: return LEN[op];
        }
    }

    private static int readInt(byte[] c, int i) {
        return ((c[i] & 0xff) << 24) | ((c[i + 1] & 0xff) << 16) | ((c[i + 2] & 0xff) << 8) | (c[i + 3] & 0xff);
    }

    private static final int[] LEN = new int[256];
    static {
        java.util.Arrays.fill(LEN, 1);
        int[] two = {0x10, 0x12, 0x15, 0x16, 0x17, 0x18, 0x19, 0x36, 0x37, 0x38, 0x39, 0x3a, 0xa9, 0xbc};
        int[] three = {0x11, 0x13, 0x14, 0x84, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8,
                       0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xbb, 0xbd, 0xc0, 0xc1, 0xc6, 0xc7};
        for (int o : two) LEN[o] = 2;
        for (int o : three) LEN[o] = 3;
        LEN[0xb9] = 5; LEN[0xba] = 5; LEN[0xc5] = 4; LEN[0xc8] = 5; LEN[0xc9] = 5;
    }

    private static String modifiedUtf8(byte[] x) {
        try {
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            DataOutputStream d = new DataOutputStream(bo);
            d.writeShort(x.length);
            d.write(x);
            return new java.io.DataInputStream(new java.io.ByteArrayInputStream(bo.toByteArray())).readUTF();
        } catch (IOException e) {
            return new String(x, java.nio.charset.StandardCharsets.UTF_8);
        }
    }
}
