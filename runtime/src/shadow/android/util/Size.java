package android.util;

public final class Size {
    private final int w, h;
    public Size(int w, int h) { this.w = w; this.h = h; }
    public int getWidth() { return w; }
    public int getHeight() { return h; }
    @Override public String toString() { return w + "x" + h; }
}
