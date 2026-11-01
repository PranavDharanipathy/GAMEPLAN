package org.firstinspires.ftc.teamcode;

import android.content.Context;
import com.qualcomm.robotcore.eventloop.opmode.OpMode;
import com.qualcomm.robotcore.eventloop.opmode.TeleOp;
import org.firstinspires.ftc.robotcore.internal.system.AppUtil;

/** Uses Android-only API: must be reported and excluded, never silently "simulated". */
@TeleOp(name = "Android Only")
public class DashboardUser extends OpMode {
    @Override public void init() { Context c = AppUtil.getInstance().getApplication(); telemetry.addData("ctx", c); }
    @Override public void loop() {}
}
