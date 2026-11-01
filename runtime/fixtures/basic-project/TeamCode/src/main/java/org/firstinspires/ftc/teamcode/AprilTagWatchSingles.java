package org.firstinspires.ftc.teamcode;

import com.qualcomm.robotcore.eventloop.opmode.TeleOp;

import org.firstinspires.ftc.robotcore.external.navigation.DistanceUnit;
import org.firstinspires.ftc.vision.apriltag.AprilTagLibrary;

/** Same as AprilTag Watch, with a custom library that knows the HIVE tags only as single tags. */
@TeleOp(name = "AprilTag Watch (singles)", group = "fixture")
public class AprilTagWatchSingles extends AprilTagWatch {
    @Override protected AprilTagLibrary library() {
        AprilTagLibrary.Builder b = new AprilTagLibrary.Builder();
        for (int id = 30; id <= 45; id++) b.addTag(id, "HIVE " + id, 3.25, DistanceUnit.INCH);
        return b.build();
    }
}
