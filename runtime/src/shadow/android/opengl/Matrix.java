package android.opengl;

/**
 * Pure-math stand-in for android.opengl.Matrix (column-major 4x4 float matrices), used by the FTC
 * SDK's OpenGLMatrix / Orientation / RevHubOrientationOnRobot. Same contracts as the Android API.
 */
public class Matrix {
    public static void setIdentityM(float[] m, int o) { for (int i = 0; i < 16; i++) m[o + i] = (i % 5 == 0) ? 1f : 0f; }
    public static void multiplyMM(float[] r, int ro, float[] a, int ao, float[] b, int bo) {
        float[] t = new float[16];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += a[ao + k * 4 + i] * b[bo + j * 4 + k];
                t[j * 4 + i] = s;
            }
        System.arraycopy(t, 0, r, ro, 16);
    }
    public static void multiplyMV(float[] r, int ro, float[] m, int mo, float[] v, int vo) {
        float[] t = new float[4];
        for (int i = 0; i < 4; i++) { float s = 0; for (int k = 0; k < 4; k++) s += m[mo + k * 4 + i] * v[vo + k]; t[i] = s; }
        System.arraycopy(t, 0, r, ro, 4);
    }
    public static void transposeM(float[] r, int ro, float[] m, int mo) {
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) r[ro + i * 4 + j] = m[mo + j * 4 + i];
    }
    public static void translateM(float[] m, int o, float x, float y, float z) {
        for (int i = 0; i < 4; i++) m[o + 12 + i] += m[o + i] * x + m[o + 4 + i] * y + m[o + 8 + i] * z;
    }
    public static void translateM(float[] r, int ro, float[] m, int mo, float x, float y, float z) {
        System.arraycopy(m, mo, r, ro, 16);
        translateM(r, ro, x, y, z);
    }
    public static void scaleM(float[] m, int o, float x, float y, float z) {
        for (int i = 0; i < 4; i++) { m[o + i] *= x; m[o + 4 + i] *= y; m[o + 8 + i] *= z; }
    }
    public static void scaleM(float[] r, int ro, float[] m, int mo, float x, float y, float z) {
        System.arraycopy(m, mo, r, ro, 16);
        scaleM(r, ro, x, y, z);
    }
    public static void setRotateM(float[] m, int o, float a, float x, float y, float z) {
        setIdentityM(m, o);
        double rad = Math.toRadians(a);
        float s = (float) Math.sin(rad), c = (float) Math.cos(rad);
        float len = (float) Math.sqrt(x * x + y * y + z * z);
        if (len == 0) return;
        x /= len; y /= len; z /= len;
        float nc = 1 - c;
        m[o] = x * x * nc + c; m[o + 4] = x * y * nc - z * s; m[o + 8] = x * z * nc + y * s;
        m[o + 1] = y * x * nc + z * s; m[o + 5] = y * y * nc + c; m[o + 9] = y * z * nc - x * s;
        m[o + 2] = x * z * nc - y * s; m[o + 6] = y * z * nc + x * s; m[o + 10] = z * z * nc + c;
    }
    public static void rotateM(float[] m, int o, float a, float x, float y, float z) {
        float[] r = new float[16], t = new float[16];
        setRotateM(r, 0, a, x, y, z);
        multiplyMM(t, 0, m, o, r, 0);
        System.arraycopy(t, 0, m, o, 16);
    }
    public static void rotateM(float[] rm, int ro, float[] m, int mo, float a, float x, float y, float z) {
        float[] r = new float[16];
        setRotateM(r, 0, a, x, y, z);
        multiplyMM(rm, ro, m, mo, r, 0);
    }
    public static boolean invertM(float[] inv, int io, float[] m, int mo) {
        double[] a = new double[16];
        for (int i = 0; i < 16; i++) a[i] = m[mo + i];
        double[] b = new double[16];
        for (int i = 0; i < 4; i++) b[i * 5] = 1;
        for (int c = 0; c < 4; c++) {
            int p = c;
            for (int r = c + 1; r < 4; r++) if (Math.abs(a[c * 4 + r]) > Math.abs(a[c * 4 + p])) p = r;
            if (Math.abs(a[c * 4 + p]) < 1e-12) return false;
            for (int k = 0; k < 4; k++) { double t = a[k * 4 + c]; a[k * 4 + c] = a[k * 4 + p]; a[k * 4 + p] = t; t = b[k * 4 + c]; b[k * 4 + c] = b[k * 4 + p]; b[k * 4 + p] = t; }
            double d = a[c * 4 + c];
            for (int k = 0; k < 4; k++) { a[k * 4 + c] /= d; b[k * 4 + c] /= d; }
            for (int r = 0; r < 4; r++) {
                if (r == c) continue;
                double f = a[c * 4 + r];
                for (int k = 0; k < 4; k++) { a[k * 4 + r] -= f * a[k * 4 + c]; b[k * 4 + r] -= f * b[k * 4 + c]; }
            }
        }
        for (int i = 0; i < 16; i++) inv[io + i] = (float) b[i];
        return true;
    }
    public static float length(float x, float y, float z) { return (float) Math.sqrt(x * x + y * y + z * z); }
}
