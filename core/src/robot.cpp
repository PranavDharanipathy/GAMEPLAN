#include "gameplan/error.hpp"
#include "gameplan/robot.hpp"

#include <cmath>
#include <stdexcept>

namespace gp {

// ---------------- GearedJoint ----------------
void GearedJoint::integrate(double tm, double dt) {
    lastMotorTorque = tm;
    const double G = ratio;
    auto drive = [&](double torqueMotor, double wref) {
        double tout = torqueMotor * G;
        return (tout * wref >= 0) ? tout * eff : tout / eff;
    };
    auto fric = [&](double ww) { return bearingCoulomb * std::tanh(ww / 0.5) + bearingViscous * ww; };
    if (backlash <= 0) engaged = (engaged == 0 ? 1 : engaged);
    if (engaged != 0) {
        double J = jmOut() + jl;
        double tout = drive(tm, w);
        double fr = fric(w);
        double accExplicit = (tout - loadTorque - fr) / J;
        double tc = jl * accExplicit + loadTorque + fr;  // torque the gear teeth transmit to the load
        if (backlash > 0 && engaged * tc < 0) {
            engaged = 0;  // teeth separate; fall through to free integration
        } else {
            // semi-implicit in the stiff load damping
            double wn = w + dt * (tout - loadTorque - fr) / (J + dt * loadDamping);
            w = wn;
            wm = wn;
            theta += w * dt;
            thetam += w * dt;
            return;
        }
    }
    double jmo = std::max(jmOut(), 1e-9);
    double accm = drive(tm, wm) / jmo;
    wm += accm * dt;
    w += dt * (-loadTorque - fric(w)) / (jl + dt * loadDamping);
    thetam += wm * dt;
    theta += w * dt;
    gap = clampd(thetam - theta, -backlash / 2 - 1e-6, backlash / 2 + 1e-6);
    if (thetam - theta >= backlash / 2 || thetam - theta <= -backlash / 2) {
        engaged = (thetam - theta) > 0 ? 1 : -1;
        double wn = (jmo * wm + jl * w) / (jmo + jl);  // momentum-conserving plastic impact
        wm = w = wn;
        thetam = theta + engaged * backlash / 2;
    }
}

// ---------------- PortController (REV firmware approximation) ----------------
double PortController::compute(double pos, double vel, double dt) {
    double p = pos - double(encoderOffset);
    if (!motorEnabled) { busy = false; return 0.0; }
    switch (mode) {
        case RunMode::StopAndResetEncoder:
            encoderOffset = (long long)std::llround(pos);
            integ = 0;
            busy = false;
            return 0.0;
        case RunMode::RunWithoutEncoder:
            busy = false;
            return clampd(power, -1.0, 1.0);
        case RunMode::RunUsingEncoder:
        case RunMode::RunToPosition: {
            double target;
            if (mode == RunMode::RunToPosition) {
                double e = targetPosition - p;
                busy = std::fabs(e) > tolerance;
                double vmax = std::fabs(power) * maxTicksPerSec;
                target = clampd(posP * 0.5 * e, -vmax, vmax);
                if (!busy) target = 0.5 * posP * e;  // hold
            } else {
                busy = false;
                target = velocityCommand ? targetVelocity : power * maxTicksPerSec;
            }
            double en = (target - vel) / maxTicksPerSec;
            integ = clampd(integ + en * dt, -1.0, 1.0);
            double d = (en - prevErr) / std::max(dt, 1e-6);
            prevErr = en;
            double ff = target / maxTicksPerSec;  // characterised feed-forward (approximation, see docs)
            double u = ff * (1.0 + kF * 1e-4) + 0.1 * kP * en + 0.3 * kI * integ + 0.001 * kD * d;
            return clampd(u, -1.0, 1.0);
        }
    }
    return 0.0;
}

// ---------------- RobotConfig ----------------
RobotConfig RobotConfig::load(const std::string& path) {
    std::string text = readTextFile(path);
    RobotConfig c = fromJson(Json::parse(text), path);
    c.contentHash = sha256Hex(text);
    return c;
}

RobotConfig RobotConfig::fromJson(const Json& j, const std::string& path) {
    RobotConfig c;
    c.raw = j;
    c.sourcePath = path;
    c.contentHash = sha256Hex(j.dump());
    c.name = j["name"].str("robot");
    c.team = j["team"].str("");
    c.variant = j["variant"].str("default");
    const Json& ch = j["chassis"];
    c.mass = ch["mass_kg"].num(13.0);
    c.length = ch["length_m"].num(0.43);
    c.width = ch["width_m"].num(0.43);
    c.height = ch["height_m"].num(0.40);
    if (ch["cog_m"].isArray()) { c.cogX = ch["cog_m"][0].num(); c.cogY = ch["cog_m"][1].num(); c.cogZ = ch["cog_m"][2].num(); }
    c.yawInertia = ch["yawInertia_kgm2"].num(0.0);
    const Json& dt = j["drivetrain"];
    c.wheelInertia = dt["wheelInertia_kgm2"].num(1.5e-4);
    c.tileMu = dt["tileMu"].num(0.95);
    c.rollerRollingMu = dt["rollerRollingMu"].num(0.02);
    double R = dt["wheelRadius_m"].num(0.052);
    const double s2 = std::sqrt(0.5);
    for (auto& w : dt["wheels"].items()) {
        Wheel wh;
        wh.name = w["motor"].str();
        wh.pos = {w["pos"][0].num(), w["pos"][1].num()};
        std::string roller = w["roller"].str("");
        // X-pattern mecanum: lf & rb push forward-right, rf & lb forward-left (when spun forward)
        bool fl = wh.pos.x > 0, left = wh.pos.y > 0;
        if (roller.empty()) roller = (fl == left) ? "fr" : "fl";
        wh.n = roller == "fr" ? Vec2{s2, -s2} : roller == "fl" ? Vec2{s2, s2} : Vec2{1, 0};
        if (dt["type"].str("mecanum") == "tank") wh.n = {1, 0};
        wh.radius = R;
        c.wheels.push_back(wh);
    }
    for (auto& m : j["motors"].items()) c.motorDefs.push_back(m);
    for (auto& s : j["servos"].items()) c.servoDefs.push_back(s);
    for (auto& m : j["mechanisms"].items()) c.mechanismDefs.push_back(m);
    for (auto& s : j["sensors"].items()) c.sensorDefs.push_back(s);
    for (auto& h : j["hubs"].items()) c.hubDefs.push_back(h);
    c.batteryDef = j["battery"];
    c.wiringDef = j["wiring"];
    c.imperfections = j["imperfections"];
    c.overrides = j["overrides"];
    return c;
}

// ---------------- Robot ----------------
int Robot::motorIndex(const std::string& n) const {
    for (size_t i = 0; i < motorNames.size(); ++i) if (motorNames[i] == n) return int(i);
    return -1;
}
int Robot::servoIndex(const std::string& n) const {
    for (size_t i = 0; i < servoNames.size(); ++i) if (servoNames[i] == n) return int(i);
    return -1;
}

void Robot::build(const Components& comps, UncertaintySampler& us, const std::string& pre) {
    auto P = [&](const std::string& name, const std::string& sub, double nominal, double ideal, Dist d, double spread, int adverse,
                 double lo = -1e300, double hi = 1e300) {
        ParamSpec p;
        p.name = pre + name; p.subsystem = sub; p.nominal = nominal; p.ideal = ideal; p.dist = d; p.spread = spread;
        p.adverse = adverse; p.minV = lo; p.maxV = hi;
        return us.sample(p);
    };
    const Json& imp = cfg.imperfections;
    // Battery
    elec.battery.spec = comps.battery(cfg.batteryDef["component"].str("rev-12v-slim-3000"));
    elec.battery.st.soc = P("battery.soc", "battery", cfg.batteryDef["soc"].num(0.95), cfg.batteryDef["soc"].num(0.95), Dist::Normal, 0.04, -1, 0.05, 1.0);
    elec.battery.st.cycles = cfg.batteryDef["cycles"].num(20);
    elec.battery.r0Scale = P("battery.r0_scale", "battery", 1.0, 1.0, Dist::LogNormal, 0.18, +1);
    elec.battery.capacityScale = P("battery.capacity_scale", "battery", 1.0, 1.0, Dist::Normal, 0.05, -1, 0.5, 1.2);
    elec.battery.st.temp = P("battery.temp_C", "battery", cfg.batteryDef["temp_C"].num(25), 25, Dist::Normal, 4, +1);
    // Main path: fuse + switch + main leads
    double mainLen = cfg.wiringDef["main"]["length_m"].num(0.4);
    std::string mainAwg = cfg.wiringDef["main"]["awg"].str("12AWG");
    elec.mainPath.r20 = comps.mainSwitchOhm + 0.003 + comps.wireOhmPerM(mainAwg) * mainLen * 2 + comps.xt30Ohm;
    elec.mainPath.alpha = comps.copperAlpha;
    elec.mainPath.heatCap = 6.0;
    elec.mainPath.rth = 25;
    elec.mainPath.scale = P("electrical.main_path_scale", "electrical", 1.0, 1.0, Dist::LogNormal, 0.25, +1);
    // Hubs
    for (auto& hd : cfg.hubDefs) {
        HubState h;
        h.id = hd["id"].str();
        h.spec = comps.hub(hd["component"].str());
        h.parent = hd["parent"].str("battery");
        h.feed.r20 = comps.wireOhmPerM(hd["awg"].str("14AWG")) * hd["length_m"].num(0.3) * 2 + comps.xt30Ohm * hd["xt30"].num(1);
        h.feed.alpha = comps.copperAlpha;
        h.feed.heatCap = 3.0;
        h.feed.rth = 30;
        h.feed.scale = P("electrical.feed_scale." + h.id, "electrical", 1.0, 1.0, Dist::LogNormal, 0.3, +1);
        elec.hubs.push_back(h);
    }
    if (elec.hubs.empty()) GP_THROW(std::runtime_error("robot has no hubs: " + cfg.name));
    // Motors + joints
    for (auto& md : cfg.motorDefs) {
        MotorUnit mu;
        mu.name = md["name"].str();
        mu.spec = comps.motor(md["component"].str());
        mu.gear = comps.gearbox(md["gearbox"].str("gobilda-yj-1"));
        mu.rScale = P("motors.r_scale." + mu.name, "motors", 1.0, 1.0, Dist::Normal, 0.04, +1, 0.7, 1.4);
        mu.ktScale = P("motors.kt_scale." + mu.name, "motors", 1.0, 1.0, Dist::Normal, 0.03, -1, 0.7, 1.3);
        mu.frictionScale = P("motors.friction_scale." + mu.name, "motors", 1.0, 1.0, Dist::LogNormal, 0.25, +1);
        mu.lead.r20 = comps.wireOhmPerM(md["lead"]["awg"].str("18AWG")) * md["lead"]["length_m"].num(0.5) * 2 + 0.002;
        mu.lead.alpha = comps.copperAlpha;
        mu.lead.heatCap = 1.2;
        mu.lead.rth = 45;
        mu.lead.scale = P("electrical.lead_scale." + mu.name, "electrical", 1.0, 1.0, Dist::LogNormal, 0.2, +1);
        mu.tWinding = mu.tCase = elec.ambient;
        int hub = elec.hubIndex(md["hub"].str("control"));
        if (hub < 0) GP_THROW(std::runtime_error("motor " + mu.name + " references unknown hub"));
        elec.motors.push_back(mu);
        elec.motorHub.push_back(hub);
        motorNames.push_back(mu.name);
        GearedJoint j;
        j.motor = int(elec.motors.size()) - 1;
        j.ratio = mu.gear.ratio;
        j.eff = P("mechanical.gear_eff." + mu.name, "mechanical", mu.gear.efficiency, 1.0, Dist::Normal, 0.03, -1, 0.5, 0.99);
        j.backlash = P("mechanical.backlash." + mu.name, "mechanical", mu.gear.backlash, 0.0, Dist::LogNormal, 0.35, +1);
        j.jm = mu.spec.rotorJ;
        j.jl = md["loadInertia_kgm2"].num(1e-4);
        j.engaged = j.backlash > 0 ? 0 : 1;
        joints.push_back(j);
        motorJoint.push_back(int(joints.size()) - 1);
        motorSign.push_back(md["mirrored"].boolean(false) ? -1.0 : 1.0);
        PortController pc;
        pc.ticksPerRev = mu.spec.encoderCPR * mu.gear.ratio;
        pc.maxTicksPerSec = mu.spec.freeRpm / 60.0 / mu.gear.ratio * pc.ticksPerRev * 0.85;
        ports.push_back(pc);
    }
    // Servos
    for (auto& sd : cfg.servoDefs) {
        ServoUnit su;
        su.name = sd["name"].str();
        su.spec = comps.servo(sd["component"].str());
        su.hubId = sd["hub"].str("control");
        int port = sd["port"].integer();
        su.pairIndex = std::max(0, std::min(2, port / 2));
        su.external = sd["powerModule"].isString();
        su.extVoltage = 6.0;
        const Json& ld = sd["load"];
        su.loadJ = ld["inertia_kgm2"].num(0.0005);
        su.gravityNm = ld["gravityTorque_Nm"].num(0.0);
        su.gravityPhase = ld["gravityPhase_rad"].num(0.0);
        su.viscous = ld["viscous"].num(0.002);
        su.coulomb = ld["coulomb_Nm"].num(0.01);
        if (ld["minAngle_rad"].isNumber()) su.minAngle = ld["minAngle_rad"].num();
        if (ld["maxAngle_rad"].isNumber()) su.maxAngle = ld["maxAngle_rad"].num();
        int hub = elec.hubIndex(su.hubId);
        if (hub < 0) GP_THROW(std::runtime_error("servo " + su.name + " references unknown hub"));
        elec.servos.push_back(su);
        elec.servoHub.push_back(hub);
        servoNames.push_back(su.name);
    }
    // Wheels
    for (auto& w : cfg.wheels) {
        Wheel wh = w;
        int m = motorIndex(w.name);
        if (m < 0) GP_THROW(std::runtime_error("wheel motor not found: " + w.name));
        wh.joint = motorJoint[m];
        joints[wh.joint].jl = cfg.wheelInertia;
        wh.muScale = P("drivetrain.wheel_mu." + w.name, "drivetrain", 1.0, 1.0, Dist::LogNormal, 0.06, -1);
        wh.misalign = P("drivetrain.misalign_rad." + w.name, "drivetrain", 0.0, 0.0, Dist::Normal,
                        imp["wheelMisalign_rad"].num(0.006), 0);
        wh.n = wh.n.rotated(wh.misalign);
        wheels.push_back(wh);
    }
    // Mechanisms
    for (auto& md : cfg.mechanismDefs) {
        Mechanism m;
        m.id = md["id"].str();
        std::string t = md["type"].str("generic");
        m.type = t == "intake" ? MechType::Intake : t == "flywheel" ? MechType::Flywheel : t == "arm" ? MechType::Arm
               : t == "turret" ? MechType::Turret : t == "gate" ? MechType::Gate : t == "hood" ? MechType::Hood : MechType::Generic;
        if (md["motor"].isString()) {
            int mi = motorIndex(md["motor"].str());
            if (mi < 0) GP_THROW(std::runtime_error("mechanism motor not found: " + md["motor"].str()));
            m.joint = motorJoint[mi];
            joints[m.joint].jl = md["inertia_kgm2"].num(joints[m.joint].jl);
            joints[m.joint].bearingViscous = md["viscous"].num(2e-5);
            joints[m.joint].bearingCoulomb = md["coulomb_Nm"].num(0.003);
        }
        for (auto& s : md["servos"].items()) {
            int si = servoIndex(s.str());
            if (si < 0) GP_THROW(std::runtime_error("mechanism servo not found: " + s.str()));
            m.servos.push_back(si);
        }
        if (md["servo"].isString()) {
            int si = servoIndex(md["servo"].str());
            if (si < 0) GP_THROW(std::runtime_error("mechanism servo not found: " + md["servo"].str()));
            m.servos.push_back(si);
        }
        m.rollerRadius = md["rollerRadius_m"].num(0.03);
        m.captureSpeed = md["captureSurfaceSpeed_mps"].num(1.2);
        m.width = md["width_m"].num(0.30);
        m.reach = md["reach_m"].num(0.06);
        m.capacity = int(md["capacity"].num(3));
        m.flyRadius = md["radius_m"].num(0.048);
        m.launchEff = md["launchEfficiency"].num(0.45);
        m.launchHeight = md["launchHeight_m"].num(0.30);
        m.launchFwd = md["launchForward_m"].num(0.0);
        m.minLaunchFrac = md["minLaunchFraction"].num(0.2);
        m.openAbove = md["openAbove"].num(0.5);
        m.feedInterval = md["feedInterval_s"].num(0.25);
        m.angleAt0 = md["angleAt0_rad"].num(0.35);
        m.angleAt1 = md["angleAt1_rad"].num(1.1);
        m.turretGear = md["gearRatio"].num(1.0);
        m.gravityTorque = md["gravityTorque_Nm"].num(0.0);
        for (auto& pp : md["ballPath_m"].items()) m.path.push_back({pp[0].num(), pp[1].num(), pp[2].num()});
        m.rollerZ = md["rollerHeight_m"].num(-1);
        m.mouthHeight = md["mouthHeight_m"].num(0.13);
        m.beltGripS = md["beltGrip_s"].num(0.03);
        m.compression = md["compression_m"].num(0.006);
        m.spinSense = md["spinSense"].num(1.0);
        mechs.push_back(m);
    }
    // Sensors
    for (auto& sd : cfg.sensorDefs) {
        SensorDef s;
        s.name = sd["name"].str();
        std::string t = sd["type"].str();
        s.type = t == "imu" ? SensorType::Imu : t == "pinpoint" ? SensorType::Pinpoint : t == "voltage" ? SensorType::Voltage
               : t == "distance" ? SensorType::Distance : t == "color" ? SensorType::Color : t == "touch" ? SensorType::Touch
               : t == "webcam" ? SensorType::Webcam : t == "analog_encoder" ? SensorType::AnalogEncoder
               : t == "digital" ? SensorType::Digital : SensorType::Analog;
        s.hub = sd["hub"].str("control");
        if (sd["pos"].isArray()) s.pos = {sd["pos"][0].num(), sd["pos"][1].num(), sd["pos"][2].num()};
        s.yaw = sd["yaw_rad"].num(0);
        s.pitch = sd["pitch_rad"].num(0);
        s.hfov = sd["hfov_rad"].num(1.22);
        s.vfov = sd["vfov_rad"].num(0.92);
        if (sd["res"].isArray()) { s.resX = sd["res"][0].integer(); s.resY = sd["res"][1].integer(); }
        s.fps = sd["fps"].num(30);
        s.podX = sd["xPodOffset_m"].num(0);
        s.podY = sd["yPodOffset_m"].num(0);
        if (sd["mechanism"].isString())
            for (size_t i = 0; i < mechs.size(); ++i) if (mechs[i].id == sd["mechanism"].str()) s.mechanism = int(i);
        sensors.push_back(s);
    }
    // Chassis
    yawInertia = cfg.yawInertia > 0 ? cfg.yawInertia : cfg.mass * (cfg.length * cfg.length + cfg.width * cfg.width) / 12.0;
    cogOffsetX = P("mechanical.cog_x", "mechanical", cfg.cogX, cfg.cogX, Dist::Normal, 0.01, 0);
    cogOffsetY = P("mechanical.cog_y", "mechanical", cfg.cogY, cfg.cogY, Dist::Normal, 0.01, 0);
    frameFlexRad = P("mechanical.diag_load_imbalance", "mechanical", imp["diagonalLoadImbalance"].num(0.04), 0.0,
                     Dist::Normal, 0.03, 0, -0.3, 0.3);
    wheelSlipScale = P("drivetrain.slip_velocity_scale", "drivetrain", 1.0, 1.0, Dist::LogNormal, 0.2, +1);
}

OBB2 Robot::footprint() const {
    OBB2 b;
    b.c = pos;
    b.halfL = cfg.length / 2;
    b.halfW = cfg.width / 2;
    b.angle = heading;
    return b;
}

double Robot::encoderTicks(int m) const {
    const auto& j = joints[motorJoint[m]];
    const auto& mu = elec.motors[m];
    return motorSign[m] * j.thetam * j.ratio * mu.spec.encoderCPR / (2 * kPi);
}
double Robot::encoderVelocity(int m) const {
    const auto& j = joints[motorJoint[m]];
    const auto& mu = elec.motors[m];
    return motorSign[m] * j.wm * j.ratio * mu.spec.encoderCPR / (2 * kPi);
}
double Robot::surfaceSpeed(int mech) const {
    const auto& m = mechs[mech];
    if (m.joint < 0) return 0;
    double r = m.type == MechType::Flywheel ? m.flyRadius : m.rollerRadius;
    return joints[m.joint].w * r;
}
double Robot::servoFraction(int s) const {
    const auto& sv = elec.servos[s];
    return clampd((sv.pulseUs - 600.0) / 1800.0, 0.0, 1.0);  // SDK default PwmRange 600-2400 us
}
bool Robot::anyPoweredOutput() const {
    for (auto& m : elec.motors) if (std::fabs(m.duty) > 1e-3 && m.enabled) return true;
    for (auto& s : elec.servos) if (s.pwmEnabled && std::fabs(s.omega) > 0.2) return true;
    return false;
}

void Robot::applyImpulse(Vec2 J, Vec2 p) {
    vel += J / cfg.mass;
    omega += (p - pos).cross(J) / yawInertia;
}

void Robot::step(double dt, double t, double muScale) {
    const double m = cfg.mass;
    // 1) Firmware controllers -> duties
    for (size_t i = 0; i < ports.size(); ++i) {
        auto& pc = ports[i];
        auto& mu = elec.motors[i];
        bool dead = disabled || !rcAlive;
        pc.motorEnabled = !dead;
        double duty = pc.compute(encoderTicks(int(i)), encoderVelocity(int(i)), dt);
        mu.duty = dead ? 0.0 : duty;
        mu.floatMode = dead ? true : !pc.brake;
        mu.enabled = !dead;
    }
    // 2) Electrical network at present motor speeds
    std::vector<double> wMotor(elec.motors.size());
    for (size_t i = 0; i < elec.motors.size(); ++i) wMotor[i] = motorSign[i] * joints[motorJoint[i]].motorOmega();
    elec.solve(wMotor, t);
    for (auto& sv : elec.servos) sv.integrate(dt);   // servo output dynamics under the solved rail voltage
    // 3) Wheel normal loads (static + load transfer + diagonal imbalance from frame/tile flex)
    double h = cfg.cogZ;
    double a = 0, b = 0;
    for (auto& w : wheels) { a = std::max(a, std::fabs(w.pos.x)); b = std::max(b, std::fabs(w.pos.y)); }
    a = std::max(a, 0.05); b = std::max(b, 0.05);
    double mg = m * kG;
    double ca = std::cos(heading), sa = std::sin(heading);
    for (auto& w : wheels) {
        double sx = w.pos.x > 0 ? 1 : -1, sy = w.pos.y > 0 ? 1 : -1;
        double N = mg / 4.0 * (1 + cogOffsetX / a * sx) * (1 + cogOffsetY / b * sy);
        N -= sx * m * accel.x * h / (4 * a);
        N -= sy * m * accel.y * h / (4 * b);
        N *= 1 + frameFlexRad * sx * sy;
        if (pitch > 1e-4 && sx > 0) N = 0;        // nose lifted
        if (pitch < -1e-4 && sx < 0) N = 0;
        if (roll > 1e-4 && sy > 0) N = 0;
        if (roll < -1e-4 && sy < 0) N = 0;
        w.normal = std::max(0.0, N);
    }
    // 4) Wheel-ground contact (mecanum: traction only along n; free rolling along roller axis)
    Vec2 F{0, 0};
    double Tz = 0;
    for (auto& w : wheels) {
        auto& j = joints[w.joint];
        Vec2 rW = w.pos.rotated(heading);
        Vec2 vc = vel + Vec2{-omega * rW.y, omega * rW.x};
        Vec2 nW = w.n.rotated(heading);
        Vec2 rollAxW = nW.perp();
        Vec2 dW = Vec2{1, 0}.rotated(heading);
        double c = dW.dot(nW);
        double s = vc.dot(nW) - j.w * w.radius * c;
        double s0 = 0.04 * wheelSlipScale;
        double mu = cfg.tileMu * w.muScale * muScale;
        double fmag = -mu * w.normal * std::tanh(s / s0);
        Vec2 fw = nW * fmag;
        // roller rolling resistance along roller axis
        double vr = vc.dot(rollAxW);
        fw += rollAxW * (-cfg.rollerRollingMu * w.normal * std::tanh(vr / 0.02));
        w.force = fw;
        w.slip = s;
        slipEnergy += std::fabs(fmag * s) * dt;
        F += fw;
        Tz += rW.cross(fw);
        // reaction on the wheel joint (output side)
        j.loadTorque = fmag * c * w.radius;   // ground reaction opposes wheel spin (subtracted in GearedJoint)
        double sech = 1.0 / std::cosh(s / s0);
        j.loadDamping = mu * w.normal * sech * sech / s0 * w.radius * w.radius * c * c;
    }
    (void)ca; (void)sa;
    // mechanism loads that are not ground contact are set by World before step (intake/flywheel)
    // 5) Integrate joints with motor torques
    for (size_t i = 0; i < joints.size(); ++i) {
        auto& j = joints[i];
        double tm = j.motor >= 0 ? motorSign[j.motor] * elec.motors[j.motor].torque : 0.0;
        j.integrate(tm, dt);
    }
    for (auto& mech : mechs) if (mech.joint >= 0) { joints[mech.joint].loadTorque = 0; joints[mech.joint].loadDamping = 0; }
    // 6) Chassis rigid body (planar) + tipping
    if (!tipped) {
        Vec2 acc = F / m;
        double alpha = Tz / yawInertia;
        vel += acc * dt;
        omega += alpha * dt;
        pos += vel * dt;
        heading = wrapAngle(heading + omega * dt);
        accel = acc.rotated(-heading);
        // pitch about the support edge when inertial moment exceeds gravity's restoring moment
        double frontN = 0, rearN = 0;
        for (auto& w : wheels) (w.pos.x > 0 ? frontN : rearN) += w.normal;
        double Ie = m * (cfg.length * cfg.length + cfg.height * cfg.height) / 12.0 + m * (a * a + h * h);
        auto tipAxis = [&](double& ang, double& rate, double ainert, double d0) {
            double d = d0 * std::cos(std::fabs(ang)) - h * std::sin(std::fabs(ang));
            double H = h * std::cos(std::fabs(ang)) + d0 * std::sin(std::fabs(ang));
            double M = m * std::fabs(ainert) * H - mg * d;
            if (std::fabs(ang) < 1e-5 && M <= 0) { ang = 0; rate = 0; return; }
            double dir = ang != 0 ? sgn(ang) : sgn(ainert);
            rate += dir * M / Ie * dt;
            ang += rate * dt;
            if (sgn(ang) != dir || std::fabs(ang) < 1e-5) { ang = 0; rate = 0; }
            if (d < 0) tipped = true;
        };
        tipAxis(pitch, pitchRate, accel.x, a + cogOffsetX * (accel.x > 0 ? 1 : -1));
        tipAxis(roll, rollRate, accel.y, b + cogOffsetY * (accel.y > 0 ? 1 : -1));
    } else {
        vel *= 0.9;
        omega *= 0.9;
        pos += vel * dt;
    }
    double sp = vel.norm();
    distance += sp * dt;
    maxSpeed = std::max(maxSpeed, sp);
    // 7) Slow electrical states
    elec.step(dt, t);
}

Json Robot::snapshot() const {
    Json j = Json::object();
    Json c = Json::array();
    for (double v : {pos.x, pos.y, vel.x, vel.y, heading, omega, accel.x, accel.y, pitch, roll, pitchRate, rollRate}) c.push(v);
    j.set("chassis", c);
    j.set("tipped", tipped);
    j.set("disabled", disabled);
    j.set("rcAlive", rcAlive);
    Json js = Json::array();
    for (auto& g : joints) {
        Json o = Json::array();
        for (double v : {g.w, g.theta, g.wm, g.thetam, g.gap, double(g.engaged)}) o.push(v);
        js.push(o);
    }
    j.set("joints", js);
    Json ps = Json::array();
    for (auto& p : ports) {
        Json o = Json::array();
        for (double v : {double(int(p.mode)), p.power, p.targetVelocity, p.velocityCommand ? 1.0 : 0.0, double(p.targetPosition),
                         p.brake ? 1.0 : 0.0, p.integ, p.prevErr, double(p.encoderOffset), p.kP, p.kI, p.kD, p.kF})
            o.push(v);
        ps.push(o);
    }
    j.set("ports", ps);
    Json sv = Json::array();
    for (auto& s : elec.servos) { Json o = Json::array(); o.push(s.pulseUs); o.push(s.pwmEnabled); sv.push(o); }
    j.set("servoCmd", sv);
    Json hd = Json::array();
    for (int e : held) hd.push(e);
    j.set("held", hd);
    j.set("lastFeed", lastFeedTime);
    j.set("elec", elec.snapshot());
    Json met = Json::array();
    for (double v : {distance, maxSpeed, slipEnergy, double(collisions)}) met.push(v);
    j.set("metrics", met);
    return j;
}

void Robot::restore(const Json& j) {
    const Json& c = j["chassis"];
    pos = {c[0].num(), c[1].num()}; vel = {c[2].num(), c[3].num()}; heading = c[4].num(); omega = c[5].num();
    accel = {c[6].num(), c[7].num()}; pitch = c[8].num(); roll = c[9].num(); pitchRate = c[10].num(); rollRate = c[11].num();
    tipped = j["tipped"].boolean(); disabled = j["disabled"].boolean(); rcAlive = j["rcAlive"].boolean();
    for (size_t i = 0; i < joints.size(); ++i) {
        const Json& o = j["joints"][i];
        joints[i].w = o[0].num(); joints[i].theta = o[1].num(); joints[i].wm = o[2].num(); joints[i].thetam = o[3].num();
        joints[i].gap = o[4].num(); joints[i].engaged = o[5].integer();
    }
    for (size_t i = 0; i < ports.size(); ++i) {
        const Json& o = j["ports"][i];
        auto& p = ports[i];
        p.mode = RunMode(o[0].integer()); p.power = o[1].num(); p.targetVelocity = o[2].num(); p.velocityCommand = o[3].num() != 0;
        p.targetPosition = o[4].integer(); p.brake = o[5].num() != 0; p.integ = o[6].num(); p.prevErr = o[7].num();
        p.encoderOffset = (long long)o[8].num(); p.kP = o[9].num(); p.kI = o[10].num(); p.kD = o[11].num(); p.kF = o[12].num();
    }
    for (size_t i = 0; i < elec.servos.size(); ++i) {
        elec.servos[i].pulseUs = j["servoCmd"][i][0].num();
        elec.servos[i].pwmEnabled = j["servoCmd"][i][1].boolean();
    }
    held.clear();
    for (auto& e : j["held"].items()) held.push_back(e.integer());
    lastFeedTime = j["lastFeed"].num();
    elec.restore(j["elec"]);
    const Json& met = j["metrics"];
    distance = met[0].num(); maxSpeed = met[1].num(); slipEnergy = met[2].num(); collisions = met[3].integer();
}

}  // namespace gp
