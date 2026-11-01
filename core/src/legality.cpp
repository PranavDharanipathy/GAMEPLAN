#include "gameplan/legality.hpp"

#include <cmath>

namespace gp {

Json LegalityResult::toJson() const {
    Json j = Json::object();
    j.set("status", status);
    Json a = Json::array();
    for (auto& i : issues) {
        Json o = Json::object();
        o.set("rule", i.rule); o.set("severity", i.severity); o.set("message", i.message);
        a.push(o);
    }
    j.set("issues", a);
    j.set("note", "Automated checks cover the rules expressible from the robot description. Inspection-only rules (materials, sharp edges, ROBOT SIGN, wiring practice) are not evaluated.");
    return j;
}

LegalityResult checkLegality(const RobotConfig& rc, const SeasonDef& s, const Components& comps) {
    LegalityResult r;
    auto add = [&](const std::string& rule, const std::string& sev, const std::string& msg) { r.issues.push_back({rule, sev, msg}); };
    int motors = int(rc.motorDefs.size());
    int servos = int(rc.servoDefs.size());
    if (motors > s.maxMotors) add("R503", "illegal", std::to_string(motors) + " motors exceeds the limit of " + std::to_string(s.maxMotors));
    if (servos > s.maxServos) add("R503", "illegal", std::to_string(servos) + " servos exceeds the limit of " + std::to_string(s.maxServos));
    int controllers = 0, expansions = 0;
    for (auto& h : rc.hubDefs) {
        try {
            const HubSpec& hs = comps.hub(h["component"].str());
            if (hs.role == "robot-controller") ++controllers; else ++expansions;
        } catch (std::exception& e) { add("R701", "illegal", std::string("unknown control device: ") + e.what()); }
    }
    if (controllers != 1) add("R701", "illegal", "exactly one ROBOT CONTROLLER (REV Control Hub) is required; found " + std::to_string(controllers));
    if (expansions > s.maxExpansionHubs) add("R701", "illegal", std::to_string(expansions) + " additional Expansion Hubs; at most " + std::to_string(s.maxExpansionHubs) + " allowed");
    // motor port loading (2 motors per port allowed, R505) and port range
    std::map<std::string, int> portUse;
    for (auto& m : rc.motorDefs) {
        std::string key = m["hub"].str("control") + ":" + std::to_string(m["port"].integer());
        if (++portUse[key] > 2) add("R505", "illegal", "more than 2 motors on motor port " + key);
        if (m["port"].integer() < 0 || m["port"].integer() > 3) add("R505", "illegal", "motor port out of range on " + m["name"].str());
        try {
            comps.motor(m["component"].str());
        } catch (...) { add("R501", "illegal", "motor component not in the FTC-legal library: " + m["component"].str()); }
    }
    for (auto& sv : rc.servoDefs) {
        try {
            const ServoSpec& ss = comps.servo(sv["component"].str());
            double mech = 0.25 * ss.stallNm * ss.noLoadRadS;
            if (!ss.continuous && mech > s.servoMaxMechW + 1e-9)
                add("R502", "illegal", sv["name"].str() + ": mechanical output " + std::to_string(mech).substr(0, 4) + " W > " + std::to_string(int(s.servoMaxMechW)) + " W @6V");
            if (ss.stallA > s.servoMaxStallA + 1e-9) add("R502", "illegal", sv["name"].str() + ": stall current " + std::to_string(ss.stallA) + " A > 4 A @6V");
            if (ss.prov.confidence == "low") add("R502", "warning", sv["name"].str() + ": servo electrical data is a low-confidence estimate; verify against the manufacturer sheet");
        } catch (...) { add("R502", "illegal", "servo component not in the FTC-legal library: " + sv["component"].str()); }
    }
    // starting configuration (18 in cube) from chassis envelope
    double lim = s.startCube.x + 1e-6;
    if (rc.length > lim || rc.width > lim || rc.height > lim)
        add("R102", "illegal", "chassis envelope " + std::to_string(rc.length) + " x " + std::to_string(rc.width) + " x " + std::to_string(rc.height) + " m exceeds the 18 in STARTING CONFIGURATION");
    if (rc.batteryDef["component"].isString()) {
        try { comps.battery(rc.batteryDef["component"].str()); } catch (...) { add("R601", "illegal", "battery is not an approved 12 V NiMH pack"); }
    }
    // experimental overrides
    for (auto& kv : rc.overrides.members()) {
        double v = kv.second.num(0);
        bool absurd = std::fabs(v) > 1e4;
        add("R(EXPERIMENT)", absurd ? "illegal" : "experimental",
            "override " + kv.first + " = " + kv.second.dump() + (absurd ? " is physically unrealistic for FTC hardware" : " is not a standard component value"));
    }
    if (!rc.raw["mechanisms"].isArray()) add("info", "info", "no mechanisms declared; game interactions limited to driving");
    add("R105", "info", "expansion limit (18 x 24 x 29 in) is checked live during matches only for declared mechanism extents; inspection must verify mechanical limits");
    for (auto& i : r.issues) {
        if (i.severity == "illegal") r.status = "illegal";
        else if (i.severity == "experimental" && r.status != "illegal") r.status = "experimental";
    }
    return r;
}

}  // namespace gp
