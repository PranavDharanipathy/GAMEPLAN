// Versioned season package (seasons/<id>/season.json). A match records the package id,
// version and content hash so it can be reproduced after newer packages are installed.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "gameplan/json.hpp"
#include "gameplan/math.hpp"

namespace gp {

enum class Alliance { Red = 0, Blue = 1 };
inline const char* toString(Alliance a) { return a == Alliance::Red ? "red" : "blue"; }
inline Alliance other(Alliance a) { return a == Alliance::Red ? Alliance::Blue : Alliance::Red; }
Alliance parseAlliance(const std::string& s);

struct CellDef {
    std::string id, side;
    double offset = 0;               // along +y from pivot when in "up" state reference
    std::vector<int> tags;
    Vec3 upClusterCenter, upClusterNormal;
};
struct HiveDef {
    Alliance alliance;
    Vec3 pivot;
    double stateAngle = 1.0472;
    CellDef cells[2];
    int initialUpCell = 0;
};
struct FlowerDef {
    std::string id;
    Vec2 axis;
    std::string wall;
};
struct LegDef { Vec3 base, apex; };

struct SeasonDef {
    std::string id, name, packageVersion, rulesEngineVersion, manualTU, manualDate, contentHash, path;
    Json raw;
    // timing
    double autoS = 30, transitionS = 8, teleopS = 120, flowerUnlockRemaining = 60, nectarUnlockRemaining = 60;
    double matchLength() const { return autoS + transitionS + teleopS; }
    // geometry
    double half = 1.8288, wallHeight = 0.3048;
    AABB2 allianceArea[2], loadingZone[2], garden[2];
    std::vector<LegDef> legs;
    double legRadius = 0.019;
    HiveDef hives[2];
    double cellCaptureHeight = 1.43, cellHalfX = 0.24, cellHalfY = 0.16, cellOffsetY = 0.36;
    std::vector<FlowerDef> flowers;
    double flowerTopH = 0.546, flowerOpeningD = 0.1015, flowerZMin = 0.09, flowerZMax = 0.546, flowerOuterR = 0.075;
    double flowerRetrievalH = 0.09;
    double tagSize = 0.08255;
    // tip model
    double pollenTipW = 1.0, nectarTipW = 1.6667, tipThreshold = 7.5, tipThresholdSigma = 0.25, impactBonusPerMps = 0.12, tipSettleS = 1.2;
    // elements
    double pollenD = 0.0711, nectarD = 0.0914, pollenMass = 0.024, nectarMass = 0.040, restitution = 0.45, rollingRes = 0.02;
    double pollenMassSigma = 0.004, nectarMassSigma = 0.006;
    std::vector<Vec3> gardenPollen[2];
    std::vector<Vec3> cellNectar[2];  // by alliance (initially up cell)
    int allianceAreaNectar = 5;
    int preloadPerRobot = 4;
    // points
    std::map<std::string, int> pts;   // e.g. "LEAVE.auto"
    int swarmThreshold = 16, poll1Threshold = 4, poll2Threshold = 7;
    // robot rules
    Vec3 startCube, expansion;
    int maxMotors = 8, maxServos = 8, maxExpansionHubs = 1;
    double servoMaxMechW = 8.0, servoMaxStallA = 4.0;
    double momentaryS = 3, continuousS = 10;

    static SeasonDef load(const std::string& path);
    static SeasonDef fromText(const std::string& text, const std::string& path = "");
    int points(const std::string& key, int def = 0) const {
        auto it = pts.find(key);
        return it == pts.end() ? def : it->second;
    }
    // FIELD columns A-C red side (x<0), D-F blue side.
    Alliance sideOf(double x) const { return x < 0 ? Alliance::Red : Alliance::Blue; }
    std::string tileName(Vec2 p) const;
};

}  // namespace gp
