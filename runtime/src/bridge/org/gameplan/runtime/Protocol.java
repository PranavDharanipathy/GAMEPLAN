package org.gameplan.runtime;

import java.io.DataInputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

/** Length-prefixed little-endian frames over stdin/stdout (see runtime/PROTOCOL.md). */
public final class Protocol {
    public static final int INIT = 1, STATE = 2, SHUTDOWN = 9;
    public static final int READY = 10, SYNC = 11, TELEMETRY = 12, LOOP = 13, LOG = 14, STATUS = 15, HWSTATS = 16, BYE = 17;

    private final DataInputStream in;
    private final OutputStream out;

    public Protocol(InputStream in, OutputStream out) {
        this.in = new DataInputStream(in);
        this.out = out;
    }

    public static final class Frame {
        public final int type;
        public final ByteBuffer payload;
        Frame(int type, ByteBuffer payload) { this.type = type; this.payload = payload; }
        public String text() { return StandardCharsets.UTF_8.decode(payload.duplicate()).toString(); }
    }

    public Frame read() throws IOException {
        byte[] lenB = new byte[4];
        try {
            in.readFully(lenB);
        } catch (EOFException e) {
            return null;
        }
        int len = ByteBuffer.wrap(lenB).order(ByteOrder.LITTLE_ENDIAN).getInt();
        byte[] body = new byte[len];
        in.readFully(body);
        ByteBuffer p = ByteBuffer.wrap(body, 1, len - 1).slice().order(ByteOrder.LITTLE_ENDIAN);
        return new Frame(body[0] & 0xff, p);
    }

    public synchronized void send(int type, byte[] payload) {
        try {
            ByteBuffer h = ByteBuffer.allocate(5).order(ByteOrder.LITTLE_ENDIAN);
            h.putInt(payload.length + 1);
            h.put((byte) type);
            out.write(h.array());
            out.write(payload);
            out.flush();
        } catch (IOException e) {
            throw new RuntimeException("host pipe closed", e);
        }
    }

    public void sendText(int type, String s) { send(type, s.getBytes(StandardCharsets.UTF_8)); }

    public static String readString16(ByteBuffer b) {
        int n = b.getShort() & 0xffff;
        byte[] x = new byte[n];
        b.get(x);
        return new String(x, StandardCharsets.UTF_8);
    }

    public static String readString32(ByteBuffer b) {
        int n = b.getInt();
        byte[] x = new byte[n];
        b.get(x);
        return new String(x, StandardCharsets.UTF_8);
    }
}
