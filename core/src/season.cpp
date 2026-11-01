#include "gameplan/error.hpp"
#include "gameplan/season.hpp"

#include <stdexcept>

namespace gp {

Alliance parseAlliance(const std::string& s) {
    if (s == "red") return Alliance::Red;
    if (s == "blue") return Alliance::Blue;
    GP_THROW(std::runtime_error("unknown alliance: " + s));
}

namespace {
AABB2 box(const Json& j) {
    AABB2 b;
    b.xMin = j["xMin"].num(); b.xMax = j["xMax"].num(); b.yMin = j["yMin"].num(); b.yMax = j["yMax"].num();
    return b;
}
Vec3 v3(const Json& j) { return {j[0].num(), j[1].num(), j[2].num()}; }
}  // namespace

std::string SeasonDef::tileName(Vec2 p) const {
    int col = int((p.x + half) / (2 * half / 6));
    int row = int((p.y + half) / (2 * half / 6));
    col = std::max(0, std::min(5, col));
    row = std::max(0, std::min(5, row));
    return std::string(1, char('A' + col)) + std::to_string(row + 1);
}

SeasonDef SeasonDef::load(const std::string& path) { return fromText(readTextFile(path), path); }

SeasonDef SeasonDef::fromText(const std::string& text, const std::string& path) {
    SeasonDef s;
    s.raw = Json::parse(text);
    const Json& j = s.raw;
    s.path = path;
    s.contentHash = sha256Hex(text);
    s.id = j["id"].str();
    s.name = j["name"].str();
    s.packageVersion = j["packageVersion"].str();
    s.rulesEngineVersion = j["rulesEngineVersion"].str();
    s.manualTU = j["manual"]["teamUpdate"].str();
    s.manualDate = j["manual"]["date"].str();
    const Json& t = j["timing"];
    s.autoS = t["auto_s"].num(); s.transitionS = t["transition_s"].num(); s.teleopS = t["teleop_s"].num();
    s.flowerUnlockRemaining = t["flowerUnlockRemaining_s"].num();
    s.nectarUnlockRemaining = t["nectarUnlockRemaining_s"].num();
    const Json& f = j["field"];
    s.half = f["halfSize_m"].num();
    s.wallHeight = f["perimeter"]["wallHeight_m"].num(0.3048);
    s.allianceArea[0] = box(f["allianceAreas"]["red"]); s.allianceArea[1] = box(f["allianceAreas"]["blue"]);
    s.loadingZone[0] = box(f["loadingZones"]["red"]); s.loadingZone[1] = box(f["loadingZones"]["blue"]);
    s.garden[0] = box(f["gardens"]["red"]); s.garden[1] = box(f["gardens"]["blue"]);
    for (auto& l : f["hiveFrame"]["legs"].items()) s.legs.push_back({v3(l["base"]), v3(l["apex"])});
    s.legRadius = f["hiveFrame"]["legRadius_m"].num(0.019);
    for (auto& h : f["hives"].items()) {
        HiveDef hd;
        hd.alliance = parseAlliance(h["alliance"].str());
        hd.pivot = v3(h["pivot"]);
        hd.stateAngle = h["stateAngle_rad"].num(1.0472);
        for (int c = 0; c < 2; ++c) {
            const Json& cj = h["cells"][c];
            hd.cells[c].id = cj["id"].str();
            hd.cells[c].side = cj["side"].str();
            hd.cells[c].offset = cj["offset_m"].num();
            for (auto& tg : cj["aprilTags"].items()) hd.cells[c].tags.push_back(tg.integer());
            hd.cells[c].upClusterCenter = v3(cj["upClusterCenter"]);
            hd.cells[c].upClusterNormal = v3(cj["upClusterNormal"]);
            if (cj["id"].str() == h["initialUpCell"].str()) hd.initialUpCell = c;
        }
        s.hives[int(hd.alliance)] = hd;
    }
    const Json& cc = f["cellCapture"];
    s.cellCaptureHeight = cc["upOpeningCenterHeight_m"].num(1.43);
    s.cellHalfX = cc["openingHalfX_m"].num(0.24);
    s.cellHalfY = cc["openingHalfY_m"].num(0.16);
    s.cellOffsetY = cc["openingCenterOffsetY_m"].num(0.36);
    for (auto& fl : f["flowers"].items())
        s.flowers.push_back({fl["id"].str(), {fl["axis"][0].num(), fl["axis"][1].num()}, fl["wall"].str()});
    const Json& fg = f["flowerGeometry"];
    s.flowerTopH = fg["topHeight_m"].num(); s.flowerOpeningD = fg["topOpeningDiameter_m"].num();
    s.flowerZMin = fg["scoringVolume"]["zMin_m"].num(); s.flowerZMax = fg["scoringVolume"]["zMax_m"].num();
    s.flowerOuterR = fg["outerRadius_m"].num(0.075);
    s.flowerRetrievalH = fg["retrievalOpeningHeight_m"].num(0.09);
    s.tagSize = f["aprilTags"]["size_m"].num(0.08255);
    for (auto& m : f["aprilTags"]["clusterMemberPositions_in"].items()) s.clusterMembers.push_back(v3(m) * 0.0254);
    const Json& tip = f["hiveTip"];
    s.pollenTipW = tip["pollenTipWeight"].num(); s.nectarTipW = tip["nectarTipWeight"].num();
    s.tipThreshold = tip["threshold"].num(); s.tipThresholdSigma = tip["thresholdSigma"].num(0.25);
    s.impactBonusPerMps = tip["impactBonusPerMps"].num(0.12); s.tipSettleS = tip["settleTime_s"].num(1.2);
    const Json& se = j["scoringElements"];
    s.pollenD = se["POLLEN"]["diameter_m"].num(); s.nectarD = se["NECTAR"]["diameter_m"].num();
    s.pollenMass = se["POLLEN"]["mass_kg"].num(); s.nectarMass = se["NECTAR"]["mass_kg"].num();
    s.pollenMassSigma = se["POLLEN"]["massSigma_kg"].num(0.004); s.nectarMassSigma = se["NECTAR"]["massSigma_kg"].num(0.006);
    s.restitution = se["restitution"].num(0.45); s.rollingRes = se["rollingResistance"].num(0.02);
    for (auto& st : j["staging"]["pollen"].items()) {
        std::string w = st["where"].str();
        if (w == "garden_red" || w == "garden_blue")
            for (auto& p : st["positions"].items()) s.gardenPollen[w == "garden_red" ? 0 : 1].push_back(v3(p));
        if (w == "robot_preload") s.preloadPerRobot = st["countPerRobot"].integer();
    }
    for (auto& st : j["staging"]["nectar"].items()) {
        std::string w = st["where"].str();
        int a = int(parseAlliance(st["alliance"].str()));
        if (w == "cell") for (auto& p : st["positions"].items()) s.cellNectar[a].push_back(v3(p));
        if (w == "alliance_area") s.allianceAreaNectar = st["count"].integer();
    }
    for (auto& kv : j["points"].members()) {
        if (kv.second.isNumber()) s.pts[kv.first] = kv.second.integer();
        else for (auto& p : kv.second.members()) if (p.second.isNumber()) s.pts[kv.first + "." + p.first] = p.second.integer();
    }
    const Json& rp = j["rankingPoints"];
    s.swarmThreshold = rp["SWARM"]["threshold"].integer();
    s.poll1Threshold = rp["POLLINATOR_1"]["threshold"].integer();
    s.poll2Threshold = rp["POLLINATOR_2"]["threshold"].integer();
    const Json& rr = j["robotRules"];
    s.startCube = v3(rr["startingConfiguration_m"]);
    s.expansion = v3(rr["expansionLimit_m"]);
    s.maxMotors = rr["maxMotors"].integer(); s.maxServos = rr["maxServos"].integer();
    s.maxExpansionHubs = rr["maxAdditionalExpansionHubs"].integer();
    s.servoMaxMechW = rr["servoLimits"]["servo"]["maxMechPower_W"].num();
    s.servoMaxStallA = rr["servoLimits"]["servo"]["maxStallCurrent_A"].num();
    s.momentaryS = j["officiating"]["MOMENTARY_s"].num(3);
    s.continuousS = j["officiating"]["CONTINUOUS_s"].num(10);
    return s;
}

}  // namespace gp
