package android.os;

import java.io.File;

/**
 * Desktop stand-in for android.os.Environment. "External storage" (/sdcard on a Control Hub) is a
 * per-run directory the simulator owns (system property gameplan.sdcard, set by the host). It is the
 * only place robot code can write: on Linux the runtime process is confined to it with Landlock.
 * AppUtil.ROOT_FOLDER / FIRST_FOLDER / ROBOT_DATA_DIR resolve under it, so team logs written to
 * /sdcard/FIRST/data/... land in the match recording's robot-files/<robot>/sdcard/FIRST/data/....
 */
public class Environment {
    public static final String MEDIA_MOUNTED = "mounted";
    public static final String DIRECTORY_DOWNLOADS = "Download";
    public static final String DIRECTORY_PICTURES = "Pictures";
    public static final String DIRECTORY_DOCUMENTS = "Documents";

    private static File root() {
        File f = new File(System.getProperty("gameplan.sdcard", "sdcard")).getAbsoluteFile();
        f.mkdirs();
        return f;
    }
    public static File getExternalStorageDirectory() { return root(); }
    public static File getExternalStoragePublicDirectory(String type) { return new File(root(), type); }
    public static String getExternalStorageState() { return MEDIA_MOUNTED; }
    public static String getExternalStorageState(File path) { return MEDIA_MOUNTED; }
    public static File getDataDirectory() { return new File(root(), ".data"); }
    public static File getDownloadCacheDirectory() { return new File(root(), ".cache"); }
    public static boolean isExternalStorageRemovable() { return false; }
    public static boolean isExternalStorageEmulated() { return true; }
}
