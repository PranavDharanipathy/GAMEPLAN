package org.firstinspires.ftc.robotcore.internal.system;

import android.os.Environment;

import org.firstinspires.ftc.robotcore.external.Predicate;
import org.gameplan.runtime.api.OpModeKilledError;
import org.gameplan.runtime.api.Sim;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.List;

/**
 * Desktop shadow of the SDK 12.0.0 AppUtil. The real class implements
 * android.app.Application.ActivityLifecycleCallbacks and cannot load off-Android.
 *
 * Kept (same names, types and values as the real class): every public folder constant, built from
 * Environment.getExternalStorageDirectory() exactly like the SDK (so /sdcard/FIRST/data maps to the
 * simulator's per-run virtual sdcard), and the file / time / diagnostics helpers.
 * Deliberately absent: UI, dialogs, toasts, progress, Bluetooth, USB-fs, web-socket and threeten
 * date helpers. Robot code calling them fails with NoSuchMethodError, which the runtime reports
 * as an incompatibility instead of silently pretending (ROBOT_CODE_COMPATIBILITY.md).
 */
public class AppUtil {
    public static final File ROOT_FOLDER = Environment.getExternalStorageDirectory();
    public static final File FIRST_FOLDER = new File(ROOT_FOLDER + "/FIRST/");
    public static final File LOG_FOLDER = ROOT_FOLDER;
    public static final File MATCH_LOG_FOLDER = new File(FIRST_FOLDER + "/matchlogs/");
    public static final int MAX_MATCH_LOGS_TO_KEEP = 9;
    public static final File CONFIG_FILES_DIR = FIRST_FOLDER;
    public static final File BLOCK_OPMODES_DIR = new File(FIRST_FOLDER, "/blocks/");
    public static final String BLOCKS_BLK_EXT = ".blk";
    public static final String BLOCKS_JS_EXT = ".js";
    public static final File BLOCKS_SOUNDS_DIR = new File(BLOCK_OPMODES_DIR, "/sounds/");
    public static final File ROBOT_SETTINGS = new File(FIRST_FOLDER, "/settings/");
    public static final File ROBOT_DATA_DIR = new File(FIRST_FOLDER, "/data/");
    public static final File UPDATES_DIR = new File(FIRST_FOLDER, "/updates/");
    public static final File RC_APP_UPDATE_DIR = new File(UPDATES_DIR, "/Robot Controller Application/");
    public static final File LYNX_FIRMWARE_UPDATE_DIR = new File(UPDATES_DIR, "/Expansion Hub Firmware/");
    public static final File OTA_UPDATE_DIR = new File(ROOT_FOLDER, "/OTA-Updates");
    public static final File SOUNDS_DIR = new File(FIRST_FOLDER, "sounds");
    public static final File SOUNDS_CACHE = new File(SOUNDS_DIR, "cache");
    public static final File WEBCAM_CALIBRATIONS_DIR = new File(FIRST_FOLDER, "/webcamcalibrations/");
    public static final String PROGRESS_NAMESPACE = "progress";
    public static final String SHOW_PROGRESS_MSG = "showProgress";
    public static final String DISMISS_PROGRESS_MSG = "dismissProgress";
    public static final String TAG = "AppUtil";

    private static final AppUtil INSTANCE = new AppUtil();
    protected AppUtil() {}
    public static AppUtil getInstance() { return INSTANCE; }

    // ----- files
    public File getRelativePath(File root, File file) {
        String r = root.getAbsolutePath(), f = file.getAbsolutePath();
        return f.startsWith(r) ? new File(f.substring(Math.min(f.length(), r.length() + 1))) : file;
    }
    public void ensureDirectoryExists(File dir) { ensureDirectoryExists(dir, true); }
    public void ensureDirectoryExists(File dir, boolean logFailure) {
        if (!dir.isDirectory() && !dir.mkdirs() && logFailure) System.err.println(TAG + ": failed to create directory " + dir);
    }
    public void deleteChildren(File dir) {
        File[] c = dir.listFiles();
        if (c != null) for (File f : c) delete(f);
    }
    public void delete(File f) {
        if (f.isDirectory()) deleteChildren(f);
        if (!f.delete() && f.exists()) System.err.println(TAG + ": failed to delete " + f);
    }
    public List<File> filesUnder(File dir) { return filesUnder(dir, (Predicate<File>) null); }
    public List<File> filesUnder(File dir, Predicate<File> p) {
        List<File> out = new ArrayList<>();
        File[] c = dir.listFiles();
        if (c == null) return out;
        for (File f : c) {
            if (f.isDirectory()) out.addAll(filesUnder(f, p));
            else if (p == null || p.test(f)) out.add(f);
        }
        return out;
    }
    public List<File> filesUnder(File dir, final String ext) { return filesUnder(dir, f -> f.getName().endsWith(ext)); }
    public List<File> filesIn(File dir) { return filesIn(dir, (Predicate<File>) null); }
    public List<File> filesIn(File dir, Predicate<File> p) {
        List<File> out = new ArrayList<>();
        File[] c = dir.listFiles();
        if (c != null) for (File f : c) if (f.isFile() && (p == null || p.test(f))) out.add(f);
        return out;
    }
    public List<File> filesIn(File dir, final String ext) { return filesIn(dir, f -> f.getName().endsWith(ext)); }
    public File getSettingsFile(String name) {
        File f = new File(name);
        return f.isAbsolute() ? f : new File(ROBOT_SETTINGS, name);
    }
    public void copyFile(File from, File to) throws IOException {
        try (InputStream in = new FileInputStream(from)) { copyStream(in, to); }
    }
    public void copyStream(InputStream in, File to) throws IOException {
        try (OutputStream o = new FileOutputStream(to)) { copyStream(in, o); }
    }
    public void copyStream(File from, OutputStream out) throws IOException {
        try (InputStream in = new FileInputStream(from)) { copyStream(in, out); }
    }
    public void copyStream(InputStream in, OutputStream out) throws IOException {
        byte[] b = new byte[8192];
        int n;
        while ((n = in.read(b)) > 0) out.write(b, 0, n);
    }
    public File createTempFile(String prefix, String suffix, File dir) throws IOException {
        return File.createTempFile(prefix, suffix, dir);
    }
    public File createTempDirectory(String prefix, String suffix, File dir) throws IOException {
        File f = File.createTempFile(prefix, suffix, dir);
        if (!f.delete() || !f.mkdir()) throw new IOException("cannot create temp directory " + f);
        return f;
    }
    public static String computeMd5(File f) throws NoSuchAlgorithmException, IOException {
        MessageDigest md = MessageDigest.getInstance("MD5");
        try (InputStream in = new FileInputStream(f)) {
            byte[] b = new byte[8192];
            int n;
            while ((n = in.read(b)) > 0) md.update(b, 0, n);
        }
        StringBuilder s = new StringBuilder();
        for (byte x : md.digest()) s.append(String.format("%02x", x));
        return s.toString();
    }

    // ----- application identity / lifecycle
    public void restartApp(int code) { throw new OpModeKilledError("AppUtil.restartApp(" + code + ") called by robot code"); }
    public void exitApplication(int code) { throw new OpModeKilledError("AppUtil.exitApplication(" + code + ") called by robot code"); }
    public void exitApplication() { exitApplication(0); }
    public String getApplicationId() { return "com.qualcomm.ftcrobotcontroller"; }
    public boolean isRobotController() { return true; }
    public boolean isDriverStation() { return false; }
    public String getAppName() { return "Robot Controller"; }
    public String getRemoteAppName() { return "Driver Station"; }
    public static String getSdkVersionString(int major, int minor, int patch) { return major + "." + minor + (patch != 0 ? "." + patch : ""); }
    public static String getSdkVersionString() { return getSdkVersionString(12, 0, 0); }

    // ----- time: the simulator's virtual wall clock (deterministic)
    public long getWallClockTime() { return Sim.currentTimeMillis(); }
    public void setWallClockTime(long ms) {}
    public void setWallClockIfCurrentlyInsane(long ms, String tz) {}
    public void setTimeZone(String tz) {}
    public boolean isSaneWallClockTime(long ms) { return ms > 1_500_000_000_000L; }

    // ----- diagnostics
    public String findCaller(String message, int skip) {
        StackTraceElement[] st = new Throwable().getStackTrace();
        int i = Math.min(st.length - 1, 2 + skip);
        return i >= 0 ? message + " " + st[i] : message;
    }
    public RuntimeException unreachable() { return unreachable("unreachable code reached"); }
    public RuntimeException unreachable(Throwable t) { return unreachable("unreachable code reached", t); }
    public RuntimeException unreachable(String m) { return new RuntimeException(m); }
    public RuntimeException unreachable(String m, Throwable t) { return new RuntimeException(m, t); }
    public RuntimeException failFast(String tag, String format, Object... args) { return failFast(tag, String.format(format, args)); }
    public RuntimeException failFast(String tag, String m) { throw new OpModeKilledError(tag + ": " + m); }
    public RuntimeException failFast(String tag, Throwable t, String format, Object... args) { return failFast(tag, t, String.format(format, args)); }
    public RuntimeException failFast(String tag, Throwable t, String m) { throw new OpModeKilledError(tag + ": " + m + " (" + t + ")"); }
}
