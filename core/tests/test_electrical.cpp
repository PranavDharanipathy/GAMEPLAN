#include "testing.hpp"
#include "gameplan/robot.hpp"

using namespace gp;

static Components comps() { return Components::load(gptest::root() + "/data/hardware/components.json"); }

static Robot makeRobot(ULevel lvl = ULevel::Nominal, double soc = 0.95) {
    Robot r;
    r.cfg = RobotConfig::load(gptest::root() + "/examples/robots/generic-launcher.robot.json");
    UncertaintyConfig uc;
    uc.setAll(lvl);
    uc.setOverride("t.battery.soc", soc);
    UncertaintySampler us(1234, uc);
    r.build(comps(), us, "t.");
    return r;
}

TEST(motor_spec_derivation_matches_datasheet) {
    auto c = comps();
    const MotorSpec& m = c.motor("gobilda-5203-bare");
    // Free-running at 12 V: current must equal no-load current, speed the free speed.
    double w = m.wFree();
    double i = (12.0 - m.ke() * w) / m.R20();
    CHECK_NEAR(i, m.freeA, 1e-9);
    CHECK_NEAR(m.R20() * m.stallA, 12.0, 1e-9);
    // stall torque through 19.2:1 reproduces the published 24.3 kgf.cm
    CHECK_NEAR(m.stallNm * 19.2032 / 0.0980665, 24.3, 0.05);
}

TEST(drivetrain_reaches_plausible_top_speed) {
    Robot r = makeRobot();
    for (size_t k = 0; k < r.ports.size(); ++k) r.ports[k].power = r.motorSign[k];
    // left side reversed in hardware convention? (our joints: all +power -> forward). Run 2.5 s.
    double dt = 0.0005;
    for (int i = 0; i < 5000; ++i) r.step(dt, i * dt, 1.0);
    double v = r.vel.norm();
    double vFree = 5991.0 / 19.2032 * 2 * kPi / 60.0 * 0.052;  // ~1.70 m/s unloaded wheel surface speed
    vFree *= r.elec.hubs[0].busV / 12.0;  // free speed scales with the actual bus voltage (fresh pack ~13 V)
    CHECK_GT(v, 0.80 * vFree);
    CHECK_LT(v, vFree);
    CHECK_NEAR(r.vel.y, 0.0, 0.05);
    CHECK_NEAR(r.omega, 0.0, 0.05);
}

TEST(strafe_right_moves_negative_y) {
    Robot r = makeRobot();
    int lf = r.motorIndex("lf"), rf = r.motorIndex("rf"), lb = r.motorIndex("lb"), rb = r.motorIndex("rb");
    r.ports[lf].power = 1; r.ports[rf].power = 1; r.ports[lb].power = -1; r.ports[rb].power = -1;  // rf/rb mirrored
    double dt = 0.0005;
    for (int i = 0; i < 3000; ++i) r.step(dt, i * dt, 1.0);
    CHECK_LT(r.vel.y, -0.5);
    CHECK_NEAR(r.vel.x, 0.0, 0.08);
}

TEST(acceleration_draws_current_and_sags_battery) {
    Robot r = makeRobot();
    double dt = 0.0005;
    r.step(dt, 0, 1.0);
    double vIdle = r.elec.battery.st.vTerm;
    for (size_t k = 0; k < r.ports.size(); ++k) r.ports[k].power = r.motorSign[k];
    double minV = 99, maxI = 0;
    for (int i = 0; i < 400; ++i) {
        r.step(dt, i * dt, 1.0);
        minV = std::min(minV, r.elec.battery.st.vTerm);
        maxI = std::max(maxI, r.elec.battery.st.current);
    }
    CHECK_GT(maxI, 15.0);           // 6 motors (4 drive) launching from rest
    CHECK_LT(minV, vIdle - 0.4);   // terminal voltage sags under load
}

TEST(intake_load_reduces_drive_voltage_and_acceleration) {
    // Same robot, same seed: running the stalled intake + flywheel spin-up while accelerating
    // must lower hub bus voltage and therefore drivetrain acceleration (coupling, no ad-hoc rule).
    auto run = [](bool loadOn) {
        Robot r = makeRobot();
        for (int k = 0; k < 4; ++k) r.ports[k].power = r.motorSign[k];
        int in = r.motorIndex("intake"), sh = r.motorIndex("shooter");
        if (loadOn) { r.ports[in].power = 1.0; r.ports[sh].power = 1.0; }
        double dt = 0.0005, x = 0;
        for (int i = 0; i < 600; ++i) {
            if (loadOn) { r.joints[r.motorJoint[in]].loadTorque = 3.0; }  // jammed ball on the intake roller
            r.step(dt, i * dt, 1.0);
        }
        x = r.pos.x;
        return std::make_pair(x, r.elec.hubs[0].busV);
    };
    auto a = run(false), b = run(true);
    CHECK_LT(b.second, a.second);
    CHECK_LT(b.first, a.first);
}

TEST(servo_current_depends_on_load_and_model) {
    Robot r = makeRobot();
    int g = r.servoIndex("gate"), h = r.servoIndex("hood");
    auto& es = r.elec;
    es.servos[g].pwmEnabled = true; es.servos[h].pwmEnabled = true;
    es.servos[g].pulseUs = 1500; es.servos[h].pulseUs = 1500;
    double dt = 0.0005;
    for (int i = 0; i < 2000; ++i) { r.step(dt, i * dt, 1.0); }
    double holdG = es.servos[g].current;
    // heavier gravity load -> more holding current
    es.servos[g].gravityNm = 1.2;
    for (int i = 0; i < 2000; ++i) { r.step(dt, i * dt, 1.0); }
    CHECK_GT(es.servos[g].current, holdG * 3);
    // moving draws more than holding unloaded
    es.servos[h].pulseUs = 2300;
    double peak = 0;
    for (int i = 0; i < 200; ++i) { r.step(dt, i * dt, 1.0); peak = std::max(peak, es.servos[h].current); }
    CHECK_GT(peak, 0.5);
}

TEST(servo_pair_limit_folds_back_shared_rail) {
    Robot r = makeRobot();
    auto& es = r.elec;
    // put both servos on the same pair (ports 0/1) and stall them hard
    int g = r.servoIndex("gate"), h = r.servoIndex("hood");
    es.servos[h].pairIndex = es.servos[g].pairIndex;
    for (auto& s : es.servos) { s.pwmEnabled = true; s.pulseUs = 2400; s.maxAngle = 0.0; s.gravityNm = 0; }
    std::vector<double> w(es.motors.size(), 0.0);
    es.solve(w, 0.0);
    int pair = es.servos[g].pairIndex;
    CHECK_LT(es.hubs[0].servoPairI[pair], es.hubs[0].spec.servoPairLimitA * 1.01);
    CHECK_LT(es.hubs[0].servoPairV[pair], 5.0);
}

TEST(battery_ocv_and_resistance_respond_to_state) {
    Robot full = makeRobot(ULevel::Nominal, 0.95), low = makeRobot(ULevel::Nominal, 0.10);
    CHECK_GT(full.elec.battery.ocv(), low.elec.battery.ocv());
    CHECK_GT(low.elec.battery.r0(), full.elec.battery.r0());
}

TEST(wire_resistance_changes_with_thermal_time_constant) {
    ThermalResistor tr; tr.r20 = 0.02; tr.heatCap = 2.0; tr.rth = 40;
    double r0 = tr.r();
    tr.step(20.0, 25, 0.001);
    CHECK_LT(tr.r() - r0, 0.0001 * r0 + 1e-6 + 0.02 * 0.00393 * 0.01);  // no instantaneous jump
    for (int i = 0; i < 60000; ++i) tr.step(20.0, 25, 0.001);
    CHECK_GT(tr.r(), r0 * 1.05);  // heats over a minute
}

TEST(backlash_creates_lost_motion_visible_to_encoder) {
    GearedJoint j; j.ratio = 19.2; j.eff = 0.9; j.backlash = 0.05; j.jm = 2e-6; j.jl = 1e-3; j.engaged = 0;
    double dt = 0.0002;
    for (int i = 0; i < 400; ++i) j.integrate(0.05, dt);   // drive +
    double thL = j.theta;
    for (int i = 0; i < 200; ++i) j.integrate(-0.05, dt);  // reverse: motor crosses gap first
    // during reversal the motor side moved back further than the load side
    CHECK_LT(j.thetam - j.theta, 0.0);
    CHECK_GT(j.theta, thL - 0.5);
}
