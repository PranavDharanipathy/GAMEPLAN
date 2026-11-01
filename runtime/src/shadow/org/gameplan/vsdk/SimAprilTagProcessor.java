package org.gameplan.vsdk;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import org.firstinspires.ftc.robotcore.external.navigation.AngleUnit;
import org.firstinspires.ftc.robotcore.external.navigation.DistanceUnit;
import org.firstinspires.ftc.robotcore.internal.camera.calibration.CameraCalibration;
import org.firstinspires.ftc.vision.apriltag.AprilTagClusterDetection;
import org.firstinspires.ftc.vision.apriltag.AprilTagClusterMemberMetadata;
import org.firstinspires.ftc.vision.apriltag.AprilTagClusterMetadata;
import org.firstinspires.ftc.vision.apriltag.AprilTagDetection;
import org.firstinspires.ftc.vision.apriltag.AprilTagLibrary;
import org.firstinspires.ftc.vision.apriltag.AprilTagMetadata;
import org.firstinspires.ftc.vision.apriltag.AprilTagPoseFtc;
import org.firstinspires.ftc.vision.apriltag.AprilTagProcessor;
import org.firstinspires.ftc.vision.apriltag.AprilTagSingleDetection;
import org.gameplan.runtime.api.DeviceInfo;
import org.gameplan.runtime.api.Sim;
import org.opencv.core.Point;

/**
 * Virtual AprilTag perception. Detections are produced by the host from scene geometry (camera
 * pose, FOV, tag pose incl. moving HIVE cells, occlusion, motion blur, pixel size, latency,
 * misses and rare false positives). No image processing is executed.
 */
public class SimAprilTagProcessor extends AprilTagProcessor {
    private final DistanceUnit du;
    private final AngleUnit au;
    private final AprilTagLibrary lib;
    private DeviceInfo cam;
    volatile boolean enabled = true;
    private double lastFrame = -2;

    public SimAprilTagProcessor(DistanceUnit du, AngleUnit au, AprilTagLibrary lib) { this.du = du; this.au = au; this.lib = lib; }
    public void attach(DeviceInfo c) { cam = c; }

    @Override public void setDecimation(float d) {}
    @Override public void setPoseSolver(PoseSolver s) {}
    @Override public int getPerTagAvgPoseSolveTime() { return 2; }
    @Override public void init(int w, int h, CameraCalibration c) {}
    @Override public Object processFrame(org.opencv.core.Mat m, long t) { return null; }
    @Override public void onDrawFrame(android.graphics.Canvas c, int w, int h, float a, float b, Object o) {}

    @Override public ArrayList<AprilTagDetection> getDetections() { return read(false); }
    @Override public ArrayList<AprilTagDetection> getFreshDetections() { return read(true); }

    /** Member ids of a library cluster (the SDK keeps them package-private). */
    private static List<Integer> memberIds(AprilTagClusterMetadata c) {
        List<Integer> ids = new ArrayList<>();
        try {
            java.lang.reflect.Field f = AprilTagClusterMetadata.class.getDeclaredField("clusterMembers");
            f.setAccessible(true);
            for (Object m : (List<?>) f.get(c)) ids.add(((AprilTagClusterMemberMetadata) m).id);
        } catch (ReflectiveOperationException e) {
            throw new IllegalStateException("SDK AprilTagClusterMetadata layout changed", e);
        }
        return ids;
    }

    private AprilTagPoseFtc pose(Map<String, Object> d) {
        double dk = du.fromMeters(1.0);
        double ak = au == AngleUnit.DEGREES ? 180.0 / Math.PI : 1.0;
        return new AprilTagPoseFtc(MiniJson.num(d.get("x")) * dk, MiniJson.num(d.get("y")) * dk, MiniJson.num(d.get("z")) * dk,
            MiniJson.num(d.get("yaw")) * ak, MiniJson.num(d.get("pitch")) * ak, MiniJson.num(d.get("roll")) * ak,
            MiniJson.num(d.get("range")) * dk, MiniJson.num(d.get("bearing")) * ak, MiniJson.num(d.get("elevation")) * ak);
    }

    /**
     * Mirrors AprilTagProcessorImpl (SDK 12.0.0): every detected tag whose id is a member of a library
     * cluster is consumed by that cluster, which yields one AprilTagClusterDetection with
     * percentClusterFound = round(seen / members * 100) and the pose of the cluster frame; other tags
     * become AprilTagSingleDetections, with metadata and pose only if the library knows the tag.
     * robotPose / rawPose are not produced (see ROBOT_CODE_COMPATIBILITY.md).
     */
    private ArrayList<AprilTagDetection> read(boolean freshOnly) {
        ArrayList<AprilTagDetection> out = new ArrayList<>();
        if (cam == null || !enabled) return out;
        String js = Sim.vision(cam.index);
        if (js == null) return out;
        Map<String, Object> camJ = org.gameplan.vsdk.MiniJson.obj(js);
        double ft = MiniJson.num(camJ.get("frameTime"));
        if (freshOnly && ft == lastFrame) return out;
        lastFrame = ft;
        long ftNs = (long) (ft * 1e9);
        java.util.Set<Integer> clustered = new java.util.HashSet<>();
        List<?> hostClusters = camJ.get("clusters") instanceof List ? (List<?>) camJ.get("clusters") : new ArrayList<>();
        if (lib != null) {
            for (AprilTagClusterMetadata lc : lib.getAllClusters()) {
                List<Integer> members = memberIds(lc);
                clustered.addAll(members);
                // the physical cell whose detected tags overlap this library cluster most
                Map<String, Object> best = null;
                int bestSeen = 0;
                for (Object o : hostClusters) {
                    Map<String, Object> hc = MiniJson.asMap(o);
                    int seen = 0;
                    for (Object id : (List<?>) hc.get("seen")) if (members.contains((int) MiniJson.num(id))) seen++;
                    if (seen > bestSeen) { bestSeen = seen; best = hc; }
                }
                if (best == null) continue;
                int percent = Math.round(bestSeen / (float) members.size() * 100.0f);
                out.add(new AprilTagClusterDetection(percent, lc, du, pose(best), null, null, ftNs));
            }
        }
        for (Object o : (List<?>) camJ.get("detections")) {
            Map<String, Object> d = MiniJson.asMap(o);
            int id = (int) MiniJson.num(d.get("id"));
            if (clustered.contains(id)) continue;
            List<?> c = (List<?>) d.get("corners");
            Point[] corners = new Point[4];
            for (int k = 0; k < 4; k++) corners[k] = new Point(MiniJson.num(c.get(2 * k)), MiniJson.num(c.get(2 * k + 1)));
            AprilTagMetadata md = lib != null ? lib.lookupTag(id) : null;
            out.add(new AprilTagSingleDetection(id, 0, (float) MiniJson.num(d.get("margin")), new Point(MiniJson.num(d.get("cx")), MiniJson.num(d.get("cy"))),
                corners, md, md != null ? pose(d) : null, null, null, ftNs, du));
        }
        return out;
    }
}
