#include "gameplan/sensors.hpp"

#include <cmath>

#include "gameplan/robot.hpp"
#include "gameplan/world.hpp"

namespace gp {

// Device field codes shared with the Java runtime (see runtime/protocol.md).
namespace field {
enum : uint8_t { MotorPos = 1, MotorVel = 2, MotorCurrent = 3, MotorBusy = 4, MotorOverCurrent = 5 };
enum : uint8_t { ImuYaw = 1, ImuPitch = 2, ImuRoll = 3, ImuRateZ = 4, ImuRateY = 5, ImuRateX = 6 };
enum : uint8_t { Volt = 1 };
enum : uint8_t { PpX = 1, PpY = 2, PpH = 3, PpVx = 4, PpVy = 5, PpVh = 6, PpEncX = 7, PpEncY = 8 };
enum : uint8_t { Dist = 1 };
enum : uint8_t { ColR = 1, ColG = 2, ColB = 3, ColA = 4, ColDist = 5 };
enum : uint8_t { Pressed = 1 };
enum : uint8_t { AnalogV = 1 };
enum : uint8_t { HubBusV = 1, HubInReset = 2 };
}  // namespace field

SensorSuite::SensorSuite(Robot& r, UncertaintySampler& us, uint64_t seed, const std::string& pre)
    : robot_(r), rng_(Rng::derive(seed, pre + "sensors")), rngVision_(Rng::derive(seed, pre + "vision")) {
    sensorLevel_ = us.config().level("sensors");
    visionLevel_ = us.config().level("vision");
    auto P = [&](const std::string& n, double nom, double ideal, Dist d, double spread, int adv) {
        ParamSpec p;
        p.name = pre + n; p.subsystem = n.substr(0, n.find('.')); p.nominal = nom; p.ideal = ideal; p.dist = d; p.spread = spread; p.adverse = adv;
        return us.sample(p);
    };
    imuBias_ = P("sensors.imu_bias_rad_s", 0.0, 0.0, Dist::Normal, 0.002, 0);
    imuScale_ = P("sensors.imu_scale", 1.0, 1.0, Dist::Normal, 0.003, 0);
    gyroNoise_ = P("sensors.imu_noise_rad_s", 0.0012, 0.0, Dist::LogNormal, 0.3, +1);
    biasWalk_ = P("sensors.imu_bias_walk", 2e-5, 0.0, Dist::LogNormal, 0.5, +1);
    ppScaleX_ = P("sensors.pinpoint_scale_x", 1.0, 1.0, Dist::Normal, 0.004, 0);
    ppScaleY_ = P("sensors.pinpoint_scale_y", 1.0, 1.0, Dist::Normal, 0.004, 0);
    ppSlipSigma_ = P("sensors.pinpoint_slip", 0.002, 0.0, Dist::LogNormal, 0.5, +1);
    voltNoise_ = P("sensors.voltage_noise_V", 0.02, 0.0, Dist::LogNormal, 0.3, +1);
    // Corruption: occasional outliers / garbage values, never device failure.
    double cscale = sensorLevel_ == ULevel::Disabled ? 0.0 : sensorLevel_ == ULevel::Nominal ? 0.2 : sensorLevel_ == ULevel::Realistic ? 1.0 : 4.0;
    corruption.outlierProb = 0.0015 * cscale;
    corruption.outlierScale = 0.25;
    corruption.garbageProb = 0.0002 * cscale;
    corruption.staleProb = 0.003 * cscale;
    encoderGlitchProb_ = 0.00005 * cscale;
    if (sensorLevel_ == ULevel::Disabled) { gyroNoise_ = 0; biasWalk_ = 0; voltNoise_ = 0; ppSlipSigma_ = 0; }
    cams_.resize(r.sensors.size());
    for (size_t i = 0; i < r.sensors.size(); ++i) {
        cams_[i].latencyS = P("vision.latency_s." + r.sensors[i].name, 0.045, 0.0, Dist::LogNormal, 0.25, +1);
        cams_[i].calibFx = P("vision.calib_fx_scale." + r.sensors[i].name, 1.0, 1.0, Dist::Normal, 0.01, 0);
    }
    imuLastHeading_ = r.heading;
}

double SensorSuite::corrupt(double v, double prev, double scale, Rng& r) {
    if (r.bernoulli(corruption.garbageProb)) return (r.bernoulli(0.5) ? 1 : -1) * scale * r.uniform(5, 50);  // nonsense spike
    if (r.bernoulli(corruption.outlierProb)) return v + r.normal() * corruption.outlierScale * scale;
    if (r.bernoulli(corruption.staleProb)) return prev;
    return v;
}

double SensorSuite::pinpointError() const {
    return std::hypot(ppX_ - robot_.pos.x, ppY_ - robot_.pos.y);
}

void SensorSuite::step(const World& w, double dt) {
    // IMU: integrate true yaw rate with bias, bias random walk, scale error and white noise.
    double dh = wrapAngle(robot_.heading - imuLastHeading_);
    imuLastHeading_ = robot_.heading;
    imuBias_ += biasWalk_ * std::sqrt(dt) * rng_.normal();
    double rate = dh / dt;
    double measRate = rate * imuScale_ + imuBias_ + gyroNoise_ / std::sqrt(dt) * rng_.normal() * 0.02;
    imuYaw_ = wrapAngle(imuYaw_ + measRate * dt);
    if (w.time() - imuSampleT_ >= 0.01) {  // 100 Hz fused output
        imuSampleT_ = w.time();
        imuYawSampled_ = imuYaw_;
        imuRateSampled_ = measRate;
    }
    // Pinpoint: dead-wheel pods integrate body-frame displacement; pods slip under acceleration.
    if (!ppInit_) { ppX_ = robot_.pos.x; ppY_ = robot_.pos.y; ppH_ = robot_.heading; ppLastPos_ = robot_.pos; ppLastHeading_ = robot_.heading; ppInit_ = true; }
    Vec2 dW = robot_.pos - ppLastPos_;
    double dHt = wrapAngle(robot_.heading - ppLastHeading_);
    ppLastPos_ = robot_.pos;
    ppLastHeading_ = robot_.heading;
    Vec2 dB = dW.rotated(-robot_.heading);
    double accel = robot_.accel.norm();
    double slip = ppSlipSigma_ * (1.0 + accel / 3.0);
    dB.x *= ppScaleX_ * (1 + slip * rng_.normal() * 0.1);
    dB.y *= ppScaleY_ * (1 + slip * rng_.normal() * 0.1);
    ppEncX_ += dB.x / 0.0001 * 1.0;   // 1 tick = 0.1 mm abstraction for raw encoder fields
    ppEncY_ += dB.y / 0.0001 * 1.0;
    double hMeas = dHt * imuScale_ + (imuBias_ * 0.2) * dt;
    ppH_ = wrapAngle(ppH_ + hMeas);
    Vec2 dWm = dB.rotated(ppH_);
    ppX_ += dWm.x;
    ppY_ += dWm.y;
}

void SensorSuite::fill(const World& w, int ri, double t, uint16_t motorBase, uint16_t sensorBase, uint16_t hubBase, std::vector<SensorValue>& out) {
    const Robot& r = robot_;
    if (lastValues_.empty()) lastValues_.assign(1024, 0.0);
    auto put = [&](uint16_t dev, uint8_t f, double v, double scale, bool corruptible) {
        size_t k = (size_t(dev) * 16 + f) % lastValues_.size();
        double outV = corruptible ? corrupt(v, lastValues_[k], scale, rng_) : v;
        lastValues_[k] = outV;
        out.push_back({dev, f, outV});
    };
    for (size_t m = 0; m < r.ports.size(); ++m) {
        uint16_t dev = uint16_t(motorBase + m);
        int hub = r.elec.motorHub[m];
        bool hubDown = r.elec.hubs[hub].inReset;
        double ticks = std::floor(r.encoderTicks(int(m))) - double(r.ports[m].encoderOffset);
        if (!hubDown && rng_.bernoulli(encoderGlitchProb_)) ticks += (rng_.bernoulli(0.5) ? 1 : -1) * std::floor(rng_.uniform(1, 40));
        double vel = std::round(r.encoderVelocity(int(m)));
        // During hub loss the SDK returns stale/zero bulk data: model as last value.
        size_t kp = (size_t(dev) * 16 + field::MotorPos) % lastValues_.size();
        if (hubDown) { out.push_back({dev, field::MotorPos, lastValues_[kp]}); out.push_back({dev, field::MotorVel, 0}); }
        else { put(dev, field::MotorPos, ticks, 1, false); put(dev, field::MotorVel, vel, 50, true); }
        double cur = std::fabs(r.elec.motors[m].current);
        put(dev, field::MotorCurrent, hubDown ? 0.0 : cur + 0.03 * rng_.normal(), 1.0, true);
        put(dev, field::MotorBusy, r.ports[m].busy ? 1 : 0, 0, false);
        put(dev, field::MotorOverCurrent, cur > 8.0 ? 1 : 0, 0, false);
    }
    for (size_t s = 0; s < r.sensors.size(); ++s) {
        const SensorDef& sd = r.sensors[s];
        uint16_t dev = uint16_t(sensorBase + s);
        switch (sd.type) {
            case SensorType::Imu:
                put(dev, field::ImuYaw, imuYawSampled_, 0.05, true);
                put(dev, field::ImuPitch, r.pitch + 0.002 * rng_.normal(), 0.02, false);
                put(dev, field::ImuRoll, r.roll + 0.002 * rng_.normal(), 0.02, false);
                put(dev, field::ImuRateZ, imuRateSampled_, 0.1, true);
                put(dev, field::ImuRateY, r.pitchRate, 0.1, false);
                put(dev, field::ImuRateX, r.rollRate, 0.1, false);
                break;
            case SensorType::Voltage: {
                int hi = r.elec.hubIndex(sd.hub);
                double v = r.elec.hubs[hi < 0 ? 0 : hi].busV + voltNoise_ * rng_.normal();
                put(dev, field::Volt, v, 0.3, true);
                break;
            }
            case SensorType::Pinpoint: {
                Vec2 v = r.vel.rotated(-r.heading);
                put(dev, field::PpX, ppX_ * 1000.0, 20, true);
                put(dev, field::PpY, ppY_ * 1000.0, 20, true);
                put(dev, field::PpH, ppH_, 0.05, true);
                put(dev, field::PpVx, v.x * 1000.0, 50, false);
                put(dev, field::PpVy, v.y * 1000.0, 50, false);
                put(dev, field::PpVh, r.omega, 0.1, false);
                put(dev, field::PpEncX, std::floor(ppEncX_), 0, false);
                put(dev, field::PpEncY, std::floor(ppEncY_), 0, false);
                break;
            }
            case SensorType::Distance: {
                // ray cast in the field plane against walls, robots, legs and flowers
                Vec2 o = r.bodyToWorld(sd.pos.xy());
                Vec2 d = Vec2{std::cos(r.heading + sd.yaw), std::sin(r.heading + sd.yaw)};
                double best = 8.19;  // out-of-range sentinel (REV 2m sensor style)
                double half = w.season().half;
                for (int axis = 0; axis < 2; ++axis) {
                    double dc = axis == 0 ? d.x : d.y, oc = axis == 0 ? o.x : o.y;
                    if (std::fabs(dc) > 1e-9) {
                        double tt = (sgn(dc) * half - oc) / dc;
                        if (tt > 0) best = std::min(best, tt);
                    }
                }
                for (size_t k = 0; k < w.robots().size(); ++k) {
                    if (int(k) == ri) continue;
                    OBB2 b = w.robots()[k].robot->footprint();
                    for (double tt = 0.02; tt < best; tt += 0.01) if (b.contains(o + d * tt)) { best = tt; break; }
                }
                for (auto& fl : w.season().flowers) {
                    Vec2 rel = fl.axis - o;
                    double proj = rel.dot(d);
                    if (proj > 0 && proj < best && (rel - d * proj).norm() < w.season().flowerOuterR) best = proj - 0.02;
                }
                double noise = 0.008 * (1 + best);
                double meas = best >= 2.0 ? 8.19 : best + noise * rng_.normal();
                put(dev, field::Dist, meas, 0.3, true);
                break;
            }
            case SensorType::Color: {
                Vec2 o = r.bodyToWorld(sd.pos.xy());
                double rr = 0.25, gg = 0.25, bb = 0.25, dist = 0.2;  // grey tile
                for (auto& e : w.elements()) {
                    if (e.state != ElemState::Free && !(e.state == ElemState::Held && e.holder == ri)) continue;
                    double dd = (e.p.xy() - o).norm() - e.radius;
                    if (e.state == ElemState::Held) dd = 0.02;
                    if (dd < 0.05 && dd < dist) {
                        dist = std::max(0.005, dd);
                        if (e.type == ElemType::Pollen) { rr = 0.9; gg = 0.8; bb = 0.1; }
                        else if (e.color == Alliance::Red) { rr = 0.85; gg = 0.1; bb = 0.1; }
                        else { rr = 0.1; gg = 0.2; bb = 0.9; }
                    }
                }
                put(dev, field::ColR, rr * 1024 * (1 + 0.03 * rng_.normal()), 30, true);
                put(dev, field::ColG, gg * 1024 * (1 + 0.03 * rng_.normal()), 30, true);
                put(dev, field::ColB, bb * 1024 * (1 + 0.03 * rng_.normal()), 30, true);
                put(dev, field::ColA, (rr + gg + bb) / 3 * 1024, 30, true);
                put(dev, field::ColDist, dist, 0.02, true);
                break;
            }
            case SensorType::Touch: {
                Vec2 o = r.bodyToWorld(sd.pos.xy());
                double half = w.season().half;
                bool pressed = std::fabs(o.x) > half - 0.005 || std::fabs(o.y) > half - 0.005;
                put(dev, field::Pressed, pressed ? 1 : 0, 0, false);
                break;
            }
            case SensorType::AnalogEncoder: {
                double ang = 0;
                if (sd.mechanism >= 0) {
                    const Mechanism& m = r.mechs[sd.mechanism];
                    if (m.joint >= 0) ang = r.joints[m.joint].theta / m.turretGear;
                    else if (!m.servos.empty()) ang = r.elec.servos[m.servos[0]].angle / m.turretGear;
                }
                double v = (wrapAngle(ang) + kPi) / (2 * kPi) * 3.3;
                put(dev, field::AnalogV, v + 0.003 * rng_.normal(), 0.1, true);
                break;
            }
            default:
                break;
        }
    }
    for (size_t h = 0; h < r.elec.hubs.size(); ++h) {
        uint16_t dev = uint16_t(hubBase + h);
        put(dev, field::HubBusV, r.elec.hubs[h].busV + voltNoise_ * rng_.normal(), 0.3, false);
        put(dev, field::HubInReset, r.elec.hubs[h].inReset ? 1 : 0, 0, false);
    }
}

std::vector<AprilTagDetection> SensorSuite::renderVisionFrame(const World& w, int ri, int si, double frameT) {
    std::vector<AprilTagDetection> out;
    const Robot& r = robot_;
    const SensorDef& sd = r.sensors[si];
    CamState& cs = cams_[si];
    // camera pose in world
    Vec2 cxy = r.bodyToWorld(sd.pos.xy());
    Vec3 cam{cxy.x, cxy.y, sd.pos.z};
    double yaw = r.heading + sd.yaw, pitch = sd.pitch;
    Vec3 fwd{std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch)};
    Vec3 right{std::sin(yaw), -std::cos(yaw), 0};
    Vec3 up = right.cross(fwd);
    double fx = sd.resX / (2 * std::tan(sd.hfov / 2)) * cs.calibFx;
    double fy = sd.resY / (2 * std::tan(sd.vfov / 2));
    double tagSize = w.season().tagSize;
    bool noisy = visionLevel_ != ULevel::Disabled;
    double angRate = std::fabs(r.omega);
    for (int id : w.allTagIds()) {
        Vec3 n;
        Vec3 tp = w.tagPosition(id, &n);
        Vec3 d = tp - cam;
        double z = d.dot(fwd);
        if (z < 0.1) continue;
        double xr = d.dot(right), yu = d.dot(up);
        double u = fx * xr / z, v = fy * yu / z;
        if (std::fabs(u) > sd.resX / 2.0 || std::fabs(v) > sd.resY / 2.0) continue;
        double range = d.norm();
        double facing = -(d.normalized()).dot(n.normalized());
        if (facing < 0.26) continue;  // > ~75 deg off-normal: decoder fails
        // occlusion by other robots (extruded footprints)
        bool occluded = false;
        for (size_t k = 0; k < w.robots().size() && !occluded; ++k) {
            if (int(k) == ri) continue;
            const Robot& o = *w.robots()[k].robot;
            OBB2 b = o.footprint();
            for (double s = 0.05; s < 0.95; s += 0.05) {
                Vec3 q = cam + d * s;
                if (q.z < o.cfg.height && b.contains(q.xy())) { occluded = true; break; }
            }
        }
        if (occluded) continue;
        double px = fx * tagSize * facing / z;  // apparent tag width in pixels
        double blurPx = angRate * 0.012 * fx;   // ~12 ms exposure (estimate)
        double pDetect = 1.0 / (1.0 + std::exp(-(px - 14.0) / 3.0));
        pDetect *= 1.0 / (1.0 + std::exp((blurPx - 6.0) / 1.5));
        if (!noisy) pDetect = px > 12 ? 1.0 : 0.0;
        if (!rngVision_.bernoulli(pDetect)) continue;
        AprilTagDetection det;
        det.id = id;
        det.frameTime = frameT;
        det.availableTime = frameT + cs.latencyS;
        double sigPx = noisy ? 0.35 + blurPx * 0.2 : 0.0;
        double sigRange = noisy ? range * range * sigPx / (fx * tagSize) * 1.5 : 0.0;
        double meas = range + sigRange * rngVision_.normal();
        double bearing = std::atan2(-xr, z) + (noisy ? sigPx / fx * rngVision_.normal() : 0);
        double elevation = std::atan2(yu, z) + (noisy ? sigPx / fy * rngVision_.normal() : 0);
        det.range = meas;
        det.bearing = bearing;
        det.elevation = elevation;
        det.y = meas * std::cos(elevation) * std::cos(bearing);
        det.x = -meas * std::cos(elevation) * std::sin(bearing);
        det.z = meas * std::sin(elevation);
        double yawTrue = std::atan2(n.dot(right), -n.dot(fwd));
        det.yaw = yawTrue + (noisy ? (0.02 + 0.08 * (1 - facing)) * rngVision_.normal() : 0);
        det.centerPx[0] = sd.resX / 2.0 + u + sigPx * rngVision_.normal();
        det.centerPx[1] = sd.resY / 2.0 - v + sigPx * rngVision_.normal();
        double hpx = px / 2;
        double c8[8] = {-hpx, hpx, hpx, hpx, hpx, -hpx, -hpx, -hpx};
        for (int k = 0; k < 8; ++k) det.corners[k] = det.centerPx[k % 2] + c8[k];
        det.decisionMargin = std::max(0.0, 20.0 + 3.0 * (px - 14.0) - 4.0 * blurPx);
        det.truthRange = range;
        out.push_back(det);
    }
    // rare false positive (decoder hallucination) - realistic level and above only
    renderFalsePositive(out, frameT, cs.latencyS);
    // Cluster solves: the SDK groups every detected tag that belongs to a library cluster and solves
    // one pose for the cluster frame from all of their corners. Accuracy grows with the number of
    // members and their spread (the PnP baseline). Grouping by the team's library happens in the
    // runtime; the host provides the per-cell solve for the physical member set it saw.
    std::vector<AprilTagDetection> clusters;
    for (int cellIdx = 0; cellIdx < 4; ++cellIdx) {
        std::vector<const AprilTagDetection*> mem;
        bool fpOnly = true;
        for (auto& dt : out) {
            int h, c, k;
            if (dt.id < 0 || !w.tagCell(dt.id, h, c, k) || h * 2 + c != cellIdx) continue;
            mem.push_back(&dt);
            if (!dt.falsePositive) fpOnly = false;
        }
        if (mem.empty()) continue;
        AprilTagDetection cl;
        cl.id = -100 - cellIdx;
        cl.frameTime = frameT;
        cl.availableTime = frameT + cs.latencyS;
        for (auto* m : mem) cl.clusterIds.push_back(m->id);
        if (fpOnly) {   // a hallucinated member alone yields a garbage cluster pose, as on the robot
            const AprilTagDetection& fp = *mem[0];
            cl.x = fp.x; cl.y = fp.y; cl.z = fp.z; cl.range = fp.range; cl.bearing = fp.bearing; cl.elevation = fp.elevation;
            cl.yaw = fp.yaw; cl.decisionMargin = fp.decisionMargin; cl.falsePositive = true;
            clusters.push_back(cl);
            continue;
        }
        World::ClusterFrame f = w.clusterFrame(cellIdx / 2, cellIdx % 2);
        Vec3 d = f.origin - cam;
        double xr = d.dot(right), yf = d.dot(fwd), zu = d.dot(up);
        double range = d.norm();
        double minX = 1e9, maxX = -1e9, facing = 0, margin = 0;
        int real = 0;
        for (auto* m : mem) {
            if (m->falsePositive) continue;
            int h, c, k;
            w.tagCell(m->id, h, c, k);
            double mx = size_t(k) < w.season().clusterMembers.size() ? w.season().clusterMembers[size_t(k)].x : 0;
            minX = std::min(minX, mx); maxX = std::max(maxX, mx);
            margin += m->decisionMargin;
            ++real;
        }
        Vec3 nOut = f.za * -1.0;
        facing = std::max(0.05, -(d.normalized()).dot(nOut));
        double effSize = tagSize + (maxX - minX) * facing;      // corner spread used by the PnP solve
        double outlier = real < int(mem.size()) ? 3.0 : 1.0;    // a false member corrupts the solve
        double sigPx = noisy ? (0.35 + angRate * 0.012 * fx * 0.2) * outlier : 0.0;
        double sigRange = noisy ? range * range * sigPx / (fx * effSize) * 1.5 / std::sqrt(double(real)) : 0.0;
        double meas = range + sigRange * rngVision_.normal();
        double bearing = std::atan2(-xr, yf) + (noisy ? sigPx / fx / std::sqrt(double(real)) * rngVision_.normal() : 0);
        double elevation = std::atan2(zu, std::sqrt(xr * xr + yf * yf)) + (noisy ? sigPx / fy / std::sqrt(double(real)) * rngVision_.normal() : 0);
        cl.range = meas; cl.bearing = bearing; cl.elevation = elevation;
        cl.y = meas * std::cos(elevation) * std::cos(bearing);
        cl.x = -meas * std::cos(elevation) * std::sin(bearing);
        cl.z = meas * std::sin(elevation);
        double yawTrue = std::atan2(nOut.dot(right), -nOut.dot(fwd));
        cl.yaw = yawTrue + (noisy ? (0.02 + 0.08 * (1 - facing)) * tagSize / effSize * outlier * rngVision_.normal() : 0);
        cl.decisionMargin = margin / std::max(1, real);
        cl.truthRange = range;
        clusters.push_back(cl);
    }
    for (auto& cl : clusters) out.push_back(cl);
    return out;
}

void SensorSuite::renderFalsePositive(std::vector<AprilTagDetection>& out, double frameT, double latencyS) {
    if (visionLevel_ == ULevel::Realistic || visionLevel_ == ULevel::Extreme) {
        double pf = visionLevel_ == ULevel::Extreme ? 0.004 : 0.0008;
        if (rngVision_.bernoulli(pf)) {
            AprilTagDetection det;
            det.id = 30 + int(rngVision_.uniform(0, 16));
            det.frameTime = frameT;
            det.availableTime = frameT + latencyS;
            det.range = rngVision_.uniform(0.5, 3.0);
            det.bearing = rngVision_.uniform(-0.5, 0.5);
            det.y = det.range * std::cos(det.bearing);
            det.x = -det.range * std::sin(det.bearing);
            det.decisionMargin = rngVision_.uniform(1, 8);
            det.falsePositive = true;
            out.push_back(det);
        }
    }
}

std::vector<AprilTagDetection> SensorSuite::pollVision(const World& w, int ri, int si, double t) {
    CamState& cs = cams_[si];
    // publish complete frames whose processing finished
    while (!cs.pending.empty() && cs.pending.front().availableTime <= t) {
        if (cs.pending.front().id == -1) {
            double ft = cs.pending.front().frameTime;
            cs.pending.pop_front();
            cs.latest.clear();
            while (!cs.pending.empty() && cs.pending.front().id != -1 && cs.pending.front().frameTime == ft) {
                cs.latest.push_back(cs.pending.front());
                cs.pending.pop_front();
            }
        } else {
            cs.pending.pop_front();
        }
    }
    return cs.latest;
}

void SensorSuite::captureFrames(const World& w, int ri) {
    // Frames are exposed at the camera frame rate from the scene state at that instant (not when polled).
    double t = w.time();
    for (size_t si = 0; si < robot_.sensors.size(); ++si) {
        if (robot_.sensors[si].type != SensorType::Webcam) continue;
        CamState& cs = cams_[si];
        double period = 1.0 / std::max(1.0, robot_.sensors[si].fps);
        if (cs.lastFrame >= 0 && t - cs.lastFrame < period) continue;
        cs.lastFrame = cs.lastFrame < 0 ? t : cs.lastFrame + period;
        AprilTagDetection marker;
        marker.id = -1;
        marker.frameTime = cs.lastFrame;
        marker.availableTime = cs.lastFrame + cs.latencyS;
        cs.pending.push_back(marker);
        for (auto& d : renderVisionFrame(w, ri, int(si), cs.lastFrame)) cs.pending.push_back(d);
        while (cs.pending.size() > 400) cs.pending.pop_front();
    }
}

}  // namespace gp
