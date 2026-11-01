package org.firstinspires.ftc.teamcode;

import com.qualcomm.robotcore.eventloop.opmode.LinearOpMode;
import com.qualcomm.robotcore.eventloop.opmode.TeleOp;

import org.firstinspires.ftc.robotcore.external.hardware.camera.WebcamName;
import org.firstinspires.ftc.robotcore.external.navigation.DistanceUnit;
import org.firstinspires.ftc.vision.VisionPortal;
import org.firstinspires.ftc.vision.apriltag.AprilTagClusterDetection;
import org.firstinspires.ftc.vision.apriltag.AprilTagDetection;
import org.firstinspires.ftc.vision.apriltag.AprilTagLibrary;
import org.firstinspires.ftc.vision.apriltag.AprilTagProcessor;
import org.firstinspires.ftc.vision.apriltag.AprilTagSingleDetection;

/**
 * Standard SDK 12 AprilTag usage: VisionPortal + AprilTagProcessor. With the default (current game)
 * library the HIVE tags arrive as cluster detections; "AprilTag Watch (singles)" builds a custom
 * library that lists tags 30-45 individually, so the same tags arrive as single detections.
 */
@TeleOp(name = "AprilTag Watch", group = "fixture")
public class AprilTagWatch extends LinearOpMode {
    protected AprilTagLibrary library() { return null; }

    @Override public void runOpMode() {
        AprilTagProcessor.Builder b = new AprilTagProcessor.Builder();
        if (library() != null) b.setTagLibrary(library());
        AprilTagProcessor tags = b.build();
        VisionPortal portal = new VisionPortal.Builder().setCamera(hardwareMap.get(WebcamName.class, "Webcam 1")).addProcessor(tags).build();
        waitForStart();
        int clusters = 0, singlesWithPose = 0, singlesNoMetadata = 0, maxPercent = 0;
        String last = "-";
        while (opModeIsActive()) {
            for (AprilTagDetection d : tags.getFreshDetections()) {
                if (d instanceof AprilTagClusterDetection) {
                    AprilTagClusterDetection c = (AprilTagClusterDetection) d;
                    clusters++;
                    maxPercent = Math.max(maxPercent, c.percentClusterFound);
                    last = String.format("%s %d%% range %.1f in", c.metadata.name, c.percentClusterFound, c.ftcPose.range);
                } else if (d instanceof AprilTagSingleDetection) {
                    AprilTagSingleDetection s = (AprilTagSingleDetection) d;
                    if (s.metadata == null) { singlesNoMetadata++; continue; }   // SDK: no pose without metadata
                    singlesWithPose++;
                    last = String.format("tag %d range %.1f in", s.id, s.ftcPose.range);
                }
            }
            telemetry.addData("clusters", clusters);
            telemetry.addData("maxPercent", maxPercent);
            telemetry.addData("singlesWithPose", singlesWithPose);
            telemetry.addData("singlesNoMetadata", singlesNoMetadata);
            telemetry.addData("last", last);
            telemetry.update();
            sleep(50);
        }
        portal.close();
    }
}
