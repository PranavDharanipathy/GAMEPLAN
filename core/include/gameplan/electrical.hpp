// Coupled electrical + actuator models.
//
// Causal chain implemented here (ELECTRICAL_MODEL.md):
//   mechanical speed -> back-EMF -> motor current (given bus voltage and duty)
//   all branch currents -> battery terminal voltage (R0 + RC + OCV(SoC,T)) and distribution drops
//   bus voltage -> motor current -> torque -> mechanical acceleration (next step)
// The network is solved every physics step as a resistive tree (battery -> common path ->
// hub feeds -> hub buses) with motors as Thevenin branches (duty*V - EMF)/R. Nonlinear limits
// (H-bridge current limit, servo rail fold-back) are handled by fixed-point re-solves.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gameplan/json.hpp"
#include "gameplan/rng.hpp"

namespace gp {

struct Provenance {
    std::string kind, confidence, source;
};

struct MotorSpec {
    std::string id, name;
    double vNom = 12, freeRpm = 6000, freeA = 0.25, stallA = 9.2, stallNm = 0.124, rotorJ = 2e-6;
    int encoderCPR = 28;
    double integralRatio = 1.0;
    double rthWC = 1.6, rthCA = 6.0, capW = 9.0, capC = 95.0, alpha = 0.00393, maxWindingC = 150;
    Provenance prov;
    // derived
    double R20() const { return vNom / stallA; }
    double kt() const { return stallNm / (stallA - freeA); }
    double wFree() const { return freeRpm * 2 * 3.14159265358979 / 60.0; }
    double ke() const { return (vNom - freeA * R20()) / wFree(); }
};

struct GearboxSpec {
    std::string id;
    double ratio = 1, efficiency = 0.95, backlash = 0;
    Provenance prov;
};

struct ServoSpec {
    std::string id, name;
    bool continuous = false;
    double vSpec = 6, stallNm = 2.45, noLoadRadS = 4.36, stallA = 2.5, idleA = 0.008, holdingFrac = 0.08;
    double rangeRad = 5.236, deadbandUs = 4, pwmMinUs = 500, pwmMaxUs = 2500, kp = 40;
    Provenance prov;
};

struct BatterySpec {
    std::string id, name;
    int cells = 10;
    double capacityAh = 3.0;
    std::vector<double> socPts, ocvCell;
    double r0 = 0.02, r1 = 0.012, c1 = 900, ageFactor = 0.0004, r0TempCoeff = -0.006, thermalMass = 520, thermalR = 3.5;
    double fuseA = 20, fuseI2t = 1800;
    Provenance prov;
};

struct HubSpec {
    std::string id, name, role;
    int motorPorts = 4, servoPorts = 6;
    double hbridgeRon = 0.025, hbridgeLimitA = 14, hbridgeRecoverS = 0.05;
    double servoRailV = 5.0, servoPairLimitA = 2.0, servoRailLimitA = 6.0, servoFoldbackV = 4.2;
    double logicLoadA = 0.3, brownoutV = 7.0, brownoutRecoverV = 7.5, rebootS = 2.5, velocityLoopHz = 1000;
    Provenance prov;
};

class Components {
public:
    static Components load(const std::string& path);
    static Components fromText(const std::string& text);
    const MotorSpec& motor(const std::string& id) const;
    const GearboxSpec& gearbox(const std::string& id) const;
    const ServoSpec& servo(const std::string& id) const;
    const BatterySpec& battery(const std::string& id) const;
    const HubSpec& hub(const std::string& id) const;
    double wireOhmPerM(const std::string& awg) const;
    double mainSwitchOhm = 0.004, xt30Ohm = 0.0015, copperAlpha = 0.00393;
    std::string sourceHash;

private:
    std::map<std::string, MotorSpec> motors_;
    std::map<std::string, GearboxSpec> gearboxes_;
    std::map<std::string, ServoSpec> servos_;
    std::map<std::string, BatterySpec> batteries_;
    std::map<std::string, HubSpec> hubs_;
    std::map<std::string, double> wire_;
};

// Resistive element whose resistance follows its own temperature (I^2R heating with a
// thermal time constant), so resistance never jumps with current.
struct ThermalResistor {
    double r20 = 0;       // ohm at 20 C
    double temp = 25;     // C
    double alpha = 0.00393;
    double heatCap = 2.0; // J/K
    double rth = 40;      // K/W to ambient
    double scale = 1.0;   // manufacturing / contact variation (uncertainty)
    double faultExtra = 0.0;  // contact degradation injected by fault system (ohm)
    double r() const { return r20 * scale * (1 + alpha * (temp - 20)) + faultExtra; }
    void step(double current, double ambient, double dt) {
        double p = current * current * r();
        temp += (p - (temp - ambient) / rth) / heatCap * dt;
    }
};

struct BatteryState {
    double soc = 1.0, vrc = 0.0, temp = 25.0, fuseHeat = 0.0, cycles = 20;
    bool fuseBlown = false;
    double vTerm = 0, current = 0, ocv = 0, r0Now = 0, energyJ = 0, peakCurrent = 0;
};

class Battery {
public:
    BatterySpec spec;
    BatteryState st;
    double r0Scale = 1.0, capacityScale = 1.0;
    double ocv() const;
    double r0() const;
    void step(double current, double ambient, double dt);  // advances SoC, RC, temperature, fuse
};

// DC motor (electrical + thermal) attached to a geared joint.
struct MotorUnit {
    std::string name;
    MotorSpec spec;
    GearboxSpec gear;
    double rScale = 1, ktScale = 1, frictionScale = 1;   // per-unit variation (hidden state)
    ThermalResistor lead;                                 // lead wires + connector
    double tWinding = 25, tCase = 25;
    // electrical outputs of the last solve
    double duty = 0;           // signed applied duty [-1,1]
    bool floatMode = false;    // FLOAT at zero power => open circuit
    bool enabled = true;       // false when hub reset / disabled
    double current = 0, supplyCurrent = 0, terminalV = 0, torque = 0, emf = 0;
    bool currentLimited = false;
    double energyJ = 0, peakCurrent = 0;
    // helpers
    double windingR() const { return spec.R20() * rScale * (1 + spec.alpha * (tWinding - 20)); }
    double kt() const { return spec.kt() * ktScale; }
    double ke() const { return spec.ke() * ktScale; }
    double frictionTorque(double wMotor) const;
    void stepThermal(double ambient, double dt);
};

struct ServoUnit {
    std::string name;
    ServoSpec spec;
    int pairIndex = 0;
    std::string hubId;
    bool external = false;     // powered by a servo power module (6 V) instead of hub rail
    double extVoltage = 6.0;
    // command
    double pulseUs = 1500;     // from SDK position/power
    bool pwmEnabled = false;
    // mechanical load attached to the output
    double loadJ = 0.0005, gravityNm = 0.0, gravityPhase = 0.0, viscous = 0.002, coulomb = 0.01;
    double minAngle = -1e9, maxAngle = 1e9;
    // state
    double angle = 0, omega = 0;
    double railV = 5.0, current = 0, torque = 0, energyJ = 0;
    double targetAngle() const;
    double cmdSpeed() const;    // continuous servo
    // compute torque & current for supply voltage v (no state change)
    void evaluate(double v, double& torqueOut, double& currentOut) const;
    void integrate(double dt);
};

struct HubState {
    std::string id;
    HubSpec spec;
    std::string parent;            // "battery" or another hub id
    ThermalResistor feed;          // wire + XT30 pair(s) from parent
    double busV = 12, feedCurrent = 0, servoRailCurrent = 0;
    double servoPairV[3] = {5, 5, 5}, servoPairI[3] = {0, 0, 0};
    bool inReset = false;
    double resetUntil = -1;
    int brownouts = 0;
    double minBusV = 99;
};

struct ElectricalEvent {
    double t;
    std::string kind;    // brownout, hub_recovered, current_limit, fuse_blown, servo_foldback, thermal_warning
    std::string device;
    double value;
};

class ElectricalSystem {
public:
    Battery battery;
    ThermalResistor mainPath;      // fuse + main switch + main leads
    std::vector<HubState> hubs;
    std::vector<MotorUnit> motors;     // indexed by motor index
    std::vector<int> motorHub;         // hub index for each motor
    std::vector<ServoUnit> servos;
    std::vector<int> servoHub;
    double ambient = 25.0;
    std::vector<ElectricalEvent> events;

    int hubIndex(const std::string& id) const;
    // Solve network given joint motor-side speeds (rad/s at motor shaft). Fills currents/torques.
    void solve(const std::vector<double>& motorOmega, double t);
    // Advance all slow states (battery SoC/RC/temperature, thermal, fuse, resets).
    void step(double dt, double t);
    double totalCurrent() const { return battery.st.current; }
    Json snapshot() const;
    void restore(const Json& j);

private:
    void solveOnce(const std::vector<double>& w, const std::vector<int>& limited, std::vector<double>& servoLoad12);
};

}  // namespace gp
