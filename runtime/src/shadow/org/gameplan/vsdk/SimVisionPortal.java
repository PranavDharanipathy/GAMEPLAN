package org.gameplan.vsdk;

import java.util.List;

import org.firstinspires.ftc.robotcore.external.function.Consumer;
import org.firstinspires.ftc.robotcore.external.function.Continuation;
import org.firstinspires.ftc.robotcore.external.hardware.camera.CameraName;
import org.firstinspires.ftc.robotcore.external.hardware.camera.WebcamName;
import org.firstinspires.ftc.robotcore.external.hardware.camera.controls.CameraControl;
import org.firstinspires.ftc.vision.VisionPortal;
import org.firstinspires.ftc.vision.VisionProcessor;

public class SimVisionPortal extends VisionPortal {
    private final CameraName camera;
    private final List<VisionProcessor> processors;
    private boolean streaming = true;
    public SimVisionPortal(CameraName c, List<VisionProcessor> p) { camera = c; processors = p; }
    @Override public void setProcessorEnabled(VisionProcessor p, boolean e) { if (p instanceof SimAprilTagProcessor) ((SimAprilTagProcessor) p).enabled = e; }
    @Override public boolean getProcessorEnabled(VisionProcessor p) { return !(p instanceof SimAprilTagProcessor) || ((SimAprilTagProcessor) p).enabled; }
    @Override public CameraState getCameraState() { return streaming ? CameraState.STREAMING : CameraState.CAMERA_DEVICE_READY; }
    @Override public void saveNextFrameRaw(String f) {}
    @Override public void stopStreaming() { streaming = false; }
    @Override public void resumeStreaming() { streaming = true; }
    @Override public void stopLiveView() {}
    @Override public void resumeLiveView() {}
    @Override public float getFps() { return 30f; }
    @Override public <T extends CameraControl> T getCameraControl(Class<T> c) { return null; }
    @Override public void setActiveCamera(WebcamName w) {}
    @Override public WebcamName getActiveCamera() { return camera instanceof WebcamName ? (WebcamName) camera : null; }
    @Override public void close() { streaming = false; }
    @Override public void getFrameBitmap(Continuation<? extends Consumer<android.graphics.Bitmap>> c) {}
}
