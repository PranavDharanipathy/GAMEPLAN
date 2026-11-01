package org.firstinspires.ftc.vision;

import java.util.ArrayList;
import java.util.List;

import org.firstinspires.ftc.robotcore.external.hardware.camera.BuiltinCameraDirection;
import org.firstinspires.ftc.robotcore.external.hardware.camera.CameraName;
import org.firstinspires.ftc.robotcore.external.hardware.camera.WebcamName;
import org.firstinspires.ftc.robotcore.external.hardware.camera.controls.CameraControl;
import org.gameplan.runtime.api.DeviceInfo;
import org.gameplan.runtime.api.Sim;
import org.gameplan.vsdk.Webcam;

/**
 * GamePlan shadow of VisionPortal (SDK 12.0 API). The simulator renders camera views in Unreal and
 * produces VIRTUAL detections for known processors (AprilTagProcessor); custom processors receive
 * no frames (reported as a compatibility boundary).
 */
public abstract class VisionPortal implements org.firstinspires.ftc.robotcore.external.stream.CameraStreamSource {
    public static final int DEFAULT_VIEW_CONTAINER_ID = 0;
    public enum CameraState { OPENING_CAMERA_DEVICE, CAMERA_DEVICE_READY, STARTING_STREAM, STREAMING, STOPPING_STREAM, CLOSING_CAMERA_DEVICE, CAMERA_DEVICE_CLOSED, ERROR }
    public enum StreamFormat { YUY2, MJPEG }
    public enum MultiPortalLayout { VERTICAL, HORIZONTAL }

    public static int[] makeMultiPortalView(int n, MultiPortalLayout l) { return new int[n]; }
    public static VisionPortal easyCreateWithDefaults(BuiltinCameraDirection d, VisionProcessor... p) { return new Builder().setCamera(d).addProcessors(p).build(); }
    public static VisionPortal easyCreateWithDefaults(CameraName c, VisionProcessor... p) { return new Builder().setCamera(c).addProcessors(p).build(); }

    public abstract void setProcessorEnabled(VisionProcessor p, boolean e);
    public abstract boolean getProcessorEnabled(VisionProcessor p);
    public abstract CameraState getCameraState();
    public abstract void saveNextFrameRaw(String f);
    public abstract void stopStreaming();
    public abstract void resumeStreaming();
    public abstract void stopLiveView();
    public abstract void resumeLiveView();
    public abstract float getFps();
    public abstract <T extends CameraControl> T getCameraControl(Class<T> c);
    public abstract void setActiveCamera(WebcamName w);
    public abstract WebcamName getActiveCamera();
    public abstract void close();

    public static class Builder {
        private CameraName camera;
        private final List<VisionProcessor> processors = new ArrayList<>();
        public Builder setCamera(CameraName c) { camera = c; return this; }
        public Builder setCamera(BuiltinCameraDirection d) { Sim.log("WARN", "phone cameras are not simulated; use a webcam in the robot description"); return this; }
        public Builder setStreamFormat(StreamFormat f) { return this; }
        public Builder enableLiveView(boolean b) { return this; }
        public Builder setAutoStopLiveView(boolean b) { return this; }
        public Builder setLiveViewContainerId(int id) { return this; }
        public Builder setCameraResolution(android.util.Size s) { return this; }
        public Builder addProcessor(VisionProcessor p) { processors.add(p); return this; }
        public Builder addProcessors(VisionProcessor... p) { for (VisionProcessor x : p) processors.add(x); return this; }
        public Builder setAutoStartStreamOnBuild(boolean b) { return this; }
        public Builder setShowStatsOverlay(boolean b) { return this; }
        public VisionPortal build() {
            DeviceInfo cam = camera instanceof Webcam.SimCamera ? ((Webcam.SimCamera) camera).gpDevice() : null;
            if (cam == null) throw new IllegalArgumentException("VisionPortal needs a webcam from hardwareMap that exists in the robot description");
            for (VisionProcessor p : processors) {
                if (p instanceof org.gameplan.vsdk.SimAprilTagProcessor) ((org.gameplan.vsdk.SimAprilTagProcessor) p).attach(cam);
                else Sim.log("WARN", "VisionPortal: custom processor " + p.getClass().getName()
                    + " receives no camera frames in the simulator (virtual vision only supports AprilTagProcessor). Its outputs are NOT simulated.");
            }
            return new org.gameplan.vsdk.SimVisionPortal(camera, processors);
        }
    }
}
