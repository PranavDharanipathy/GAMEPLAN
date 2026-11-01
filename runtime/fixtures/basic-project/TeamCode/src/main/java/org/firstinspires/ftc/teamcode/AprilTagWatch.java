package org.firstinspires.ftc.teamcode;

import com.qualcomm.robotcore.eventloop.opmode.LinearOpMode;
import com.qualcomm.robotcore.eventloop.opmode.TeleOp;

import org.firstinspires.ftc.robotcore.external.hardware.camera.WebcamName;
import org.firstinspires.ftc.vision.VisionPortal;
import org.firstinspires.ftc.vision.apriltag.AprilTagDetection;
import org.firstinspires.ftc.vision.apriltag.AprilTagProcessor;
import org.firstinspires.ftc.vision.apriltag.AprilTagSingleDetection;

/** Standard SDK AprilTag usage: VisionPortal + AprilTagProcessor, reports detections. */
@TeleOp(name = "AprilTag Watch", group = "fixture")
public class AprilTagWatch extends LinearOpMode {
    @Override public void runOpMode() {
        AprilTagProcessor tags = new AprilTagProcessor.Builder().build();
        VisionPortal portal = new VisionPortal.Builder().setCamera(hardwareMap.get(WebcamName.class, "Webcam 1")).addProcessor(tags).build();
        waitForStart();
        int seen = 0;
        while (opModeIsActive()) {
            for (AprilTagDetection d : tags.getDetections()) {
                if (d instanceof AprilTagSingleDetection) {
                    AprilTagSingleDetection s = (AprilTagSingleDetection) d;
                    seen++;
                    telemetry.addData("tag " + s.id, "range %.1f in bearing %.1f deg", s.ftcPose.range, s.ftcPose.bearing);
                }
            }
            telemetry.addData("detections so far", seen);
            telemetry.update();
            sleep(50);
        }
        portal.close();
    }
}
