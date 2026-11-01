#include "gameplan/electrical.hpp"

#include <cmath>
#include <functional>
#include <stdexcept>

#include "gameplan/math.hpp"

namespace gp {

namespace {
Provenance readProv(const Json& j) {
    Provenance p;
    p.kind = j["kind"].str("unknown");
    p.confidence = j["confidence"].str("unknown");
    p.source = j["source"].str(j["basis"].str(""));
    return p;
}
template <class M, class T>
const T& find(const M& m, const std::string& id, const char* what) {
    auto it = m.find(id);
    if (it == m.end()) throw std::runtime_error(std::string("unknown ") + what + ": " + id);
    return it->second;
}
}  // namespace

Components Components::load(const std::string& path) {
    std::string text = readTextFile(path);
    Json j = Json::parse(text);
    Components c;
    c.sourceHash = sha256Hex(text);
    for (auto& m : j["motors"].items()) {
        MotorSpec s;
        s.id = m["id"].str();
        s.name = m["name"].str(s.id);
        s.vNom = m["nominalVoltage_V"].num();
        s.freeRpm = m["freeSpeed_rpm"].num();
        s.freeA = m["freeCurrent_A"].num();
        s.stallA = m["stallCurrent_A"].num();
        s.stallNm = m["stallTorque_Nm"].num();
        s.rotorJ = m["rotorInertia_kgm2"].num(2e-6);
        s.encoderCPR = int(m["encoderCPR"].num(28));
        s.integralRatio = m["integralGearRatio"].num(1.0);
        const Json& th = m["thermal"];
        s.rthWC = th["windingToCase_K_per_W"].num(1.6);
        s.rthCA = th["caseToAmbient_K_per_W"].num(6.0);
        s.capW = th["windingCap_J_per_K"].num(9.0);
        s.capC = th["caseCap_J_per_K"].num(95.0);
        s.alpha = th["copperAlpha_per_K"].num(0.00393);
        s.maxWindingC = th["maxWinding_C"].num(150);
        s.prov = readProv(m["provenance"]);
        c.motors_[s.id] = s;
    }
    for (auto& g : j["gearboxes"].items()) {
        GearboxSpec s;
        s.id = g["id"].str();
        s.ratio = g["ratio"].num();
        s.efficiency = g["efficiency"].num(0.9);
        s.backlash = g["backlash_rad"].num(0.0);
        s.prov = readProv(g["provenance"]);
        c.gearboxes_[s.id] = s;
    }
    for (auto& v : j["servos"].items()) {
        ServoSpec s;
        s.id = v["id"].str();
        s.name = v["name"].str(s.id);
        s.continuous = v["continuous"].boolean(false);
        s.vSpec = v["voltage_V"].num(6);
        s.stallNm = v["stallTorque_Nm"].num();
        s.noLoadRadS = v["noLoadSpeed_rad_s"].num();
        s.stallA = v["stallCurrent_A"].num();
        s.idleA = v["idleCurrent_A"].num(0.008);
        s.holdingFrac = v["holdingCurrentFrac"].num(0.05);
        s.rangeRad = v["rangeRad"].num(5.236);
        s.deadbandUs = v["deadband_us"].num(4);
        if (v["pwmRange_us"].isArray()) { s.pwmMinUs = v["pwmRange_us"][0].num(); s.pwmMaxUs = v["pwmRange_us"][1].num(); }
        s.kp = v["controller"]["kp_per_rad"].num(40);
        s.prov = readProv(v["provenance"]);
        c.servos_[s.id] = s;
    }
    for (auto& b : j["batteries"].items()) {
        BatterySpec s;
        s.id = b["id"].str();
        s.name = b["name"].str(s.id);
        s.cells = int(b["cells"].num(10));
        s.capacityAh = b["capacity_Ah"].num(3.0);
        for (auto& x : b["ocvCurveSoC"].items()) s.socPts.push_back(x.num());
        for (auto& x : b["ocvCurveCell_V"].items()) s.ocvCell.push_back(x.num());
        s.r0 = b["r0_ohm"].num();
        s.r1 = b["r1_ohm"].num();
        s.c1 = b["c1_F"].num();
        s.ageFactor = b["r0AgeFactorPerCycle"].num(0.0004);
        s.r0TempCoeff = b["r0TempCoeff_per_K"].num(-0.006);
        s.thermalMass = b["thermalMass_J_per_K"].num(520);
        s.thermalR = b["thermalR_K_per_W"].num(3.5);
        s.fuseA = b["fuse_A"].num(20);
        s.fuseI2t = b["fuseI2t_A2s"].num(1800);
        s.prov = readProv(b["provenance"]);
        c.batteries_[s.id] = s;
    }
    for (auto& h : j["controllers"].items()) {
        HubSpec s;
        s.id = h["id"].str();
        s.name = h["name"].str(s.id);
        s.role = h["role"].str("expansion");
        s.motorPorts = int(h["motorPorts"].num(4));
        s.servoPorts = int(h["servoPorts"].num(6));
        s.hbridgeRon = h["hbridge"]["onResistance_ohm"].num(0.025);
        s.hbridgeLimitA = h["hbridge"]["currentLimit_A"].num(14);
        s.hbridgeRecoverS = h["hbridge"]["currentLimitRecover_s"].num(0.05);
        s.servoRailV = h["servoRail"]["voltage_V"].num(5);
        s.servoPairLimitA = h["servoRail"]["pairLimit_A"].num(2);
        s.servoRailLimitA = h["servoRail"]["railLimit_A"].num(6);
        s.servoFoldbackV = h["servoRail"]["foldbackVoltage_V"].num(4.2);
        s.logicLoadA = h["logicLoad_A"].num(0.3);
        s.brownoutV = h["brownout_V"].num(7.0);
        s.brownoutRecoverV = h["brownoutRecover_V"].num(7.5);
        s.rebootS = h["rebootTime_s"].num(2.5);
        s.velocityLoopHz = h["firmwareVelocityLoop_Hz"].num(1000);
        s.prov = readProv(h["provenance"]);
        c.hubs_[s.id] = s;
    }
    const Json& ic = j["interconnects"];
    c.mainSwitchOhm = ic["mainSwitch_ohm"].num(0.004);
    c.xt30Ohm = ic["xt30Pair_ohm"].num(0.0015);
    c.copperAlpha = ic["copperAlpha_per_K"].num(0.00393);
    for (auto& kv : ic["wire_ohm_per_m"].members()) c.wire_[kv.first] = kv.second.num();
    return c;
}

const MotorSpec& Components::motor(const std::string& id) const { return find<decltype(motors_), MotorSpec>(motors_, id, "motor"); }
const GearboxSpec& Components::gearbox(const std::string& id) const { return find<decltype(gearboxes_), GearboxSpec>(gearboxes_, id, "gearbox"); }
const ServoSpec& Components::servo(const std::string& id) const { return find<decltype(servos_), ServoSpec>(servos_, id, "servo"); }
const BatterySpec& Components::battery(const std::string& id) const { return find<decltype(batteries_), BatterySpec>(batteries_, id, "battery"); }
const HubSpec& Components::hub(const std::string& id) const { return find<decltype(hubs_), HubSpec>(hubs_, id, "controller"); }
double Components::wireOhmPerM(const std::string& awg) const { return find<decltype(wire_), double>(wire_, awg, "wire gauge"); }

// ---------------- Battery ----------------
double Battery::ocv() const {
    const auto& x = spec.socPts;
    const auto& y = spec.ocvCell;
    double s = clampd(st.soc, 0.0, 1.0);
    double v = y.back();
    for (size_t i = 1; i < x.size(); ++i) {
        if (s <= x[i]) { v = lerp(y[i - 1], y[i], (s - x[i - 1]) / (x[i] - x[i - 1])); break; }
    }
    // mild temperature dependence of NiMH OCV (~ -0.4 mV/K/cell, estimate)
    v += -0.0004 * (st.temp - 25.0);
    return v * spec.cells;
}
double Battery::r0() const {
    double r = spec.r0 * r0Scale * (1 + spec.ageFactor * st.cycles) * (1 + spec.r0TempCoeff * (st.temp - 25.0));
    if (st.soc < 0.15) r *= 1 + 1.5 * (0.15 - st.soc) / 0.15;  // resistance rise near depletion
    return std::max(r, 0.002);
}
void Battery::step(double current, double ambient, double dt) {
    if (st.fuseBlown) current = 0;
    st.current = current;
    st.peakCurrent = std::max(st.peakCurrent, std::fabs(current));
    st.soc -= current * dt / (3600.0 * spec.capacityAh * capacityScale);
    st.soc = clampd(st.soc, 0.0, 1.0);
    st.vrc += (current / spec.c1 - st.vrc / (spec.r1 * spec.c1)) * dt;
    double heat = current * current * r0() + st.vrc * st.vrc / spec.r1;
    st.temp += (heat - (st.temp - ambient) / spec.thermalR) / spec.thermalMass * dt;
    // fuse: I^2t accumulation above rating, slow cooling below
    double i2 = current * current, ir2 = spec.fuseA * spec.fuseA;
    if (i2 > ir2) st.fuseHeat += (i2 - ir2) * dt;
    else st.fuseHeat = std::max(0.0, st.fuseHeat - st.fuseHeat * dt / 8.0);
    if (st.fuseHeat > spec.fuseI2t) st.fuseBlown = true;
    st.energyJ += st.vTerm * current * dt;
}

// ---------------- Motor ----------------
double MotorUnit::frictionTorque(double w) const {
    double tf = spec.kt() * spec.freeA * frictionScale;
    double wf = spec.wFree();
    double coul = 0.5 * tf * std::tanh(w / 2.0);  // smoothed Coulomb (rad/s scale 2)
    return coul + 0.5 * tf * w / wf;
}
void MotorUnit::stepThermal(double ambient, double dt) {
    double p = current * current * windingR();
    double qwc = (tWinding - tCase) / spec.rthWC;
    tWinding += (p - qwc) / spec.capW * dt;
    tCase += (qwc - (tCase - ambient) / spec.rthCA) / spec.capC * dt;
    energyJ += std::fabs(terminalV * current) * dt;
    peakCurrent = std::max(peakCurrent, std::fabs(current));
}

// ---------------- Servo ----------------
double ServoUnit::targetAngle() const {
    double mid = 0.5 * (spec.pwmMinUs + spec.pwmMaxUs), span = spec.pwmMaxUs - spec.pwmMinUs;
    return clampd((pulseUs - mid) / span, -0.5, 0.5) * spec.rangeRad;
}
double ServoUnit::cmdSpeed() const {
    double mid = 1500.0, half = 500.0;
    double d = pulseUs - mid;
    if (std::fabs(d) < spec.deadbandUs) return 0.0;
    return clampd(d / half, -1.0, 1.0);
}
void ServoUnit::evaluate(double v, double& tq, double& cur) const {
    if (!pwmEnabled || v < 3.0) {  // unpowered or under-voltage reset: no holding torque
        tq = 0;
        cur = pwmEnabled ? 0.0 : spec.idleA * 0.5;
        return;
    }
    double vr = v / spec.vSpec;
    double stall = spec.stallNm * vr, wnl = spec.noLoadRadS * vr;
    double cmd;
    if (spec.continuous) {
        double wTarget = cmdSpeed() * wnl;
        cmd = 8.0 * stall * (wTarget - omega) / std::max(wnl, 1e-3);
    } else {
        double err = targetAngle() - angle;
        double db = spec.deadbandUs / (spec.pwmMaxUs - spec.pwmMinUs) * spec.rangeRad;
        if (std::fabs(err) < db) err = 0;
        cmd = spec.kp * stall * err - 0.05 * stall * omega;
    }
    // available torque from the internal motor curve at the present speed
    double avail = stall * clampd(1.0 - std::fabs(omega) / std::max(wnl, 1e-3), 0.0, 1.0);
    if (cmd * omega < 0) avail = stall;  // braking against motion
    tq = clampd(cmd, -avail, avail);
    double iStall = spec.stallA * vr;
    cur = spec.idleA + (iStall - spec.idleA) * std::fabs(tq) / std::max(stall, 1e-6) +
          spec.holdingFrac * iStall * (spec.continuous ? 0.0 : 0.1);
}
void ServoUnit::integrate(double dt) {
    double grav = gravityNm * std::cos(angle + gravityPhase);
    double fr = coulomb * std::tanh(omega / 0.05) + viscous * omega;
    double acc = (torque - grav - fr) / std::max(loadJ, 1e-6);
    omega += acc * dt;
    angle += omega * dt;
    if (angle < minAngle) { angle = minAngle; if (omega < 0) omega = 0; }
    if (angle > maxAngle) { angle = maxAngle; if (omega > 0) omega = 0; }
    energyJ += std::fabs(railV * current) * dt;
}

// ---------------- Network ----------------
int ElectricalSystem::hubIndex(const std::string& id) const {
    for (size_t i = 0; i < hubs.size(); ++i) if (hubs[i].id == id) return int(i);
    return -1;
}

void ElectricalSystem::solveOnce(const std::vector<double>& w, const std::vector<int>& limited, std::vector<double>& servoLoad12) {
    const size_t H = hubs.size();
    std::vector<double> G(H, 0.0), Hc(H, 0.0);
    // Local loads.
    for (size_t h = 0; h < H; ++h) {
        Hc[h] -= hubs[h].inReset ? 0.0 : hubs[h].spec.logicLoadA;  // constant current sink
        Hc[h] -= servoLoad12[h];
    }
    for (size_t m = 0; m < motors.size(); ++m) {
        auto& mu = motors[m];
        int h = motorHub[m];
        double R = mu.windingR() + mu.lead.r() + 2 * hubs[h].spec.hbridgeRon;
        mu.emf = mu.ke() * w[m];
        if (!mu.enabled || hubs[h].inReset || (mu.duty == 0 && mu.floatMode)) continue;
        if (limited[m] != 0) {
            Hc[h] -= mu.duty * limited[m] * hubs[h].spec.hbridgeLimitA;
        } else {
            G[h] += mu.duty * mu.duty / R;
            Hc[h] += mu.duty * mu.emf / R;
        }
    }
    // Post-order accumulation of subtree conductances.
    std::vector<double> Gs(H), Hs(H), gEq(H), hEq(H);
    std::function<void(int)> post = [&](int h) {
        Gs[h] = G[h];
        Hs[h] = Hc[h];
        for (size_t c = 0; c < H; ++c)
            if (hubs[c].parent == hubs[h].id) { post(int(c)); Gs[h] += gEq[c]; Hs[h] += hEq[c]; }
        double R = hubs[h].feed.r();
        gEq[h] = Gs[h] / (1 + R * Gs[h]);
        hEq[h] = Hs[h] / (1 + R * Gs[h]);
    };
    double gRoot = 0, hRoot = 0;
    for (size_t h = 0; h < H; ++h)
        if (hubs[h].parent == "battery") { post(int(h)); gRoot += gEq[h]; hRoot += hEq[h]; }
    double Rr = battery.r0() + mainPath.r();
    double voc = battery.ocv();
    battery.st.ocv = voc;
    battery.st.r0Now = battery.r0();
    double vc;
    if (battery.st.fuseBlown) {
        vc = 0;
    } else {
        vc = (voc - battery.st.vrc + Rr * hRoot) / (1 + Rr * gRoot);
    }
    double itot = battery.st.fuseBlown ? 0.0 : gRoot * vc - hRoot;
    battery.st.current = itot;
    battery.st.vTerm = battery.st.fuseBlown ? 0.0 : voc - battery.st.vrc - battery.r0() * itot;
    // Pre-order voltages.
    std::function<void(int, double)> pre = [&](int h, double vp) {
        double R = hubs[h].feed.r();
        double v = (vp + R * Hs[h]) / (1 + R * Gs[h]);
        hubs[h].busV = std::max(v, 0.0);
        hubs[h].feedCurrent = (vp - v) / std::max(R, 1e-9);
        for (size_t c = 0; c < H; ++c) if (hubs[c].parent == hubs[h].id) pre(int(c), v);
    };
    for (size_t h = 0; h < H; ++h) if (hubs[h].parent == "battery") pre(int(h), vc);
    // Motor branch results.
    for (size_t m = 0; m < motors.size(); ++m) {
        auto& mu = motors[m];
        int h = motorHub[m];
        double R = mu.windingR() + mu.lead.r() + 2 * hubs[h].spec.hbridgeRon;
        mu.currentLimited = false;
        if (!mu.enabled || hubs[h].inReset || (mu.duty == 0 && mu.floatMode)) {
            mu.current = 0;
            mu.supplyCurrent = 0;
            mu.terminalV = mu.emf;
        } else if (limited[m] != 0) {
            mu.current = limited[m] * hubs[h].spec.hbridgeLimitA;
            mu.supplyCurrent = mu.duty * mu.current;
            mu.terminalV = mu.emf + mu.current * mu.windingR();
            mu.currentLimited = true;
        } else {
            double vt = mu.duty * hubs[h].busV;
            mu.current = (vt - mu.emf) / R;
            mu.supplyCurrent = mu.duty * mu.current;
            mu.terminalV = mu.emf + mu.current * mu.windingR();
        }
        mu.torque = mu.kt() * mu.current - mu.frictionTorque(w[m]);
    }
}

void ElectricalSystem::solve(const std::vector<double>& w, double t) {
    const size_t H = hubs.size();
    // 1) Servo rails (5 V hub regulators with per-pair and total fold-back).
    std::vector<double> servoLoad12(H, 0.0);
    for (size_t h = 0; h < H; ++h) {
        auto& hub = hubs[h];
        hub.servoRailCurrent = 0;
        double pairV[3] = {hub.spec.servoRailV, hub.spec.servoRailV, hub.spec.servoRailV};
        double pairI[3] = {0, 0, 0};
        for (int it = 0; it < 4; ++it) {
            for (int p = 0; p < 3; ++p) pairI[p] = 0;
            for (size_t s = 0; s < servos.size(); ++s) {
                if (servoHub[s] != int(h) || servos[s].external) continue;
                double tq, cur;
                servos[s].evaluate(hub.inReset ? 0.0 : pairV[servos[s].pairIndex], tq, cur);
                pairI[servos[s].pairIndex] += cur;
            }
            double total = pairI[0] + pairI[1] + pairI[2];
            bool changed = false;
            for (int p = 0; p < 3; ++p) {
                if (pairI[p] > hub.spec.servoPairLimitA * 1.0001) {
                    pairV[p] = std::max(0.0, pairV[p] * hub.spec.servoPairLimitA / pairI[p]);
                    changed = true;
                }
            }
            if (total > hub.spec.servoRailLimitA * 1.0001) {
                double k = hub.spec.servoRailLimitA / total;
                for (int p = 0; p < 3; ++p) pairV[p] *= k;
                changed = true;
            }
            if (!changed) break;
        }
        for (int p = 0; p < 3; ++p) {
            if (pairV[p] < hub.spec.servoRailV - 0.05 && hub.servoPairV[p] >= hub.spec.servoRailV - 0.05)
                events.push_back({t, "servo_foldback", hub.id + ":pair" + std::to_string(p), pairI[p]});
            hub.servoPairV[p] = pairV[p];
            hub.servoPairI[p] = pairI[p];
            hub.servoRailCurrent += pairI[p];
        }
        double pRail = 0;
        for (int p = 0; p < 3; ++p) pRail += pairV[p] * pairI[p];
        servoLoad12[h] = pRail / (0.85 * std::max(hub.busV, 6.0));  // buck converter efficiency (estimate)
    }
    for (size_t s = 0; s < servos.size(); ++s) {
        auto& sv = servos[s];
        double v = sv.external ? sv.extVoltage : hubs[servoHub[s]].servoPairV[sv.pairIndex];
        if (hubs[servoHub[s]].inReset && !sv.external) v = 0;
        sv.railV = v;
        sv.evaluate(v, sv.torque, sv.current);
        if (sv.external) servoLoad12[servoHub[s]] += 0.0;  // power module is fed separately from battery path (modelled as hub-side tap)
    }
    // 2) Motor network with H-bridge current limit fixed point.
    std::vector<int> limited(motors.size(), 0);
    std::vector<bool> wasLimited(motors.size());
    for (size_t m = 0; m < motors.size(); ++m) wasLimited[m] = motors[m].currentLimited;
    for (int it = 0; it < 4; ++it) {
        solveOnce(w, limited, servoLoad12);
        bool changed = false;
        for (size_t m = 0; m < motors.size(); ++m) {
            double lim = hubs[motorHub[m]].spec.hbridgeLimitA;
            if (limited[m] == 0 && std::fabs(motors[m].current) > lim) {
                limited[m] = motors[m].current > 0 ? 1 : -1;
                changed = true;
                if (!wasLimited[m]) events.push_back({t, "current_limit", motors[m].name, motors[m].current});
            }
        }
        if (!changed) break;
    }
}

void ElectricalSystem::step(double dt, double t) {
    double itot = battery.st.current;
    battery.step(itot, ambient, dt);
    mainPath.step(itot, ambient, dt);
    if (battery.st.fuseBlown) {
        bool already = false;
        for (auto& e : events) if (e.kind == "fuse_blown") already = true;
        if (!already) events.push_back({t, "fuse_blown", battery.spec.id, itot});
    }
    for (auto& h : hubs) {
        h.feed.step(h.feedCurrent, ambient, dt);
        h.minBusV = std::min(h.minBusV, h.busV);
        if (!h.inReset && h.busV < h.spec.brownoutV) {
            h.inReset = true;
            h.resetUntil = t + h.spec.rebootS;
            h.brownouts++;
            events.push_back({t, "brownout", h.id, h.busV});
        } else if (h.inReset && t >= h.resetUntil && h.busV > h.spec.brownoutRecoverV) {
            h.inReset = false;
            events.push_back({t, "hub_recovered", h.id, h.busV});
        }
    }
    for (size_t m = 0; m < motors.size(); ++m) {
        auto& mu = motors[m];
        double before = mu.tWinding;
        mu.lead.step(mu.current, ambient, dt);
        mu.stepThermal(ambient, dt);
        if (before < 100.0 && mu.tWinding >= 100.0) events.push_back({t, "thermal_warning", mu.name, mu.tWinding});
    }
}

Json ElectricalSystem::snapshot() const {
    Json j = Json::object();
    auto& b = battery.st;
    Json bj = Json::object();
    bj.set("soc", b.soc); bj.set("vrc", b.vrc); bj.set("temp", b.temp); bj.set("fuseHeat", b.fuseHeat);
    bj.set("fuseBlown", b.fuseBlown); bj.set("energyJ", b.energyJ); bj.set("vTerm", b.vTerm); bj.set("current", b.current);
    bj.set("peak", b.peakCurrent);
    j.set("battery", bj);
    j.set("mainPathTemp", mainPath.temp);
    Json hs = Json::array();
    for (auto& h : hubs) {
        Json o = Json::object();
        o.set("feedTemp", h.feed.temp); o.set("inReset", h.inReset); o.set("resetUntil", h.resetUntil);
        o.set("brownouts", h.brownouts); o.set("busV", h.busV); o.set("minBusV", h.minBusV);
        hs.push(o);
    }
    j.set("hubs", hs);
    Json ms = Json::array();
    for (auto& m : motors) {
        Json o = Json::object();
        o.set("tw", m.tWinding); o.set("tc", m.tCase); o.set("lead", m.lead.temp); o.set("e", m.energyJ); o.set("pk", m.peakCurrent);
        ms.push(o);
    }
    j.set("motors", ms);
    Json ss = Json::array();
    for (auto& s : servos) {
        Json o = Json::object();
        o.set("a", s.angle); o.set("w", s.omega); o.set("e", s.energyJ);
        ss.push(o);
    }
    j.set("servos", ss);
    return j;
}

void ElectricalSystem::restore(const Json& j) {
    auto& b = battery.st;
    const Json& bj = j["battery"];
    b.soc = bj["soc"].num(); b.vrc = bj["vrc"].num(); b.temp = bj["temp"].num(); b.fuseHeat = bj["fuseHeat"].num();
    b.fuseBlown = bj["fuseBlown"].boolean(); b.energyJ = bj["energyJ"].num(); b.vTerm = bj["vTerm"].num();
    b.current = bj["current"].num(); b.peakCurrent = bj["peak"].num();
    mainPath.temp = j["mainPathTemp"].num();
    for (size_t i = 0; i < hubs.size(); ++i) {
        const Json& o = j["hubs"][i];
        hubs[i].feed.temp = o["feedTemp"].num(); hubs[i].inReset = o["inReset"].boolean();
        hubs[i].resetUntil = o["resetUntil"].num(); hubs[i].brownouts = o["brownouts"].integer();
        hubs[i].busV = o["busV"].num(); hubs[i].minBusV = o["minBusV"].num();
    }
    for (size_t i = 0; i < motors.size(); ++i) {
        const Json& o = j["motors"][i];
        motors[i].tWinding = o["tw"].num(); motors[i].tCase = o["tc"].num(); motors[i].lead.temp = o["lead"].num();
        motors[i].energyJ = o["e"].num(); motors[i].peakCurrent = o["pk"].num();
    }
    for (size_t i = 0; i < servos.size(); ++i) {
        const Json& o = j["servos"][i];
        servos[i].angle = o["a"].num(); servos[i].omega = o["w"].num(); servos[i].energyJ = o["e"].num();
    }
}

}  // namespace gp
