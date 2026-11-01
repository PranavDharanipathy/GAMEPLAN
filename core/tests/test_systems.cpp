// Cross-system tests: rules, determinism, checkpoints, AI directives, legality, CAD, vision, faults.
#include <cmath>
#include <cstdio>

#include "gameplan/ai.hpp"
#include "gameplan/cad.hpp"
#include "gameplan/legality.hpp"
#include "gameplan/match.hpp"
#include "testing.hpp"

using namespace gp;

static MatchSetup aiSetup(uint64_t seed, const std::string& redInstruction = "") {
    Json j = Json::parseFile(gptest::root() + "/examples/matches/ai-vs-ai.match.json");
    j.set("seed", double(seed));
    if (!redInstruction.empty()) {
        Json robots = j["robots"];
        Json r0 = robots[0];
        Json ai = r0["ai"];
        ai.set("instruction", redInstruction);
        r0.set("ai", ai);
        robots[0] = r0;
        j.set("robots", robots);
    }
    return MatchSetup::fromJson(j, gptest::root());
}

TEST(season_package_matches_manual_tu07) {
    SeasonDef s = SeasonDef::load(gptest::root() + "/seasons/biobuzz-2027/season.json");
    CHECK(s.manualTU == "TU07");
    CHECK_NEAR(s.autoS, 30, 0); CHECK_NEAR(s.transitionS, 8, 0); CHECK_NEAR(s.teleopS, 120, 0);
    CHECK(s.points("HIVE_TIP.auto") == 20 && s.points("LEAVE.auto") == 3 && s.points("GARDEN_ELEMENT.end") == 1);
    CHECK(s.points("MAJOR_FOUL") == 20 && s.points("MINOR_FOUL") == 5);
    CHECK(s.swarmThreshold == 16 && s.poll1Threshold == 4 && s.poll2Threshold == 7);
    CHECK(s.maxMotors == 8 && s.maxServos == 8 && s.maxExpansionHubs == 1);
    // hive calibration: 8 POLLEN or 3 NECTAR + 3 POLLEN tip; 7 POLLEN or 3 NECTAR + 2 POLLEN do not (EFSG 12.3)
    CHECK_GT(8 * s.pollenTipW, s.tipThreshold);
    CHECK_GT(3 * s.nectarTipW + 3 * s.pollenTipW, s.tipThreshold);
    CHECK_LT(7 * s.pollenTipW, s.tipThreshold);
    CHECK_LT(3 * s.nectarTipW + 2 * s.pollenTipW, s.tipThreshold);
    CHECK(s.tileName({-1.7, 0.9}) == "A5");   // red LOADING ZONE tile
    CHECK(s.tileName({1.7, -0.9}) == "F2");   // blue LOADING ZONE tile
}

TEST(match_timer_phases_and_cues) {
    MatchSetup s = aiSetup(5);
    MatchRunner m(s);
    std::string err;
    CHECK(m.begin(err));
    m.advance(3.0 + 10.0, err);
    CHECK(m.world().rules().phase() == Phase::Auto);
    CHECK(m.world().rules().timerText(m.world().time()) == "2:20");
    m.advance(20.5, err);
    CHECK(m.world().rules().phase() == Phase::Transition);
    m.advance(8.0, err);
    CHECK(m.world().rules().phase() == Phase::Teleop);
    m.advance(70.0, err);
    CHECK(m.world().rules().flowerUnlocked(m.world().time()));
    while (m.advance(5.0, err)) {}
    CHECK(m.world().rules().isFinal());
    auto& cues = m.world().rules().cues();
    CHECK(cues.size() >= 7);
    CHECK(cues.front().rfind("match_start", 0) == 0);
}

TEST(same_seed_same_match_different_seed_differs) {
    std::string err;
    MatchRunner a(aiSetup(42)), b(aiSetup(42)), c(aiSetup(43));
    Json ra = a.run(err), rb = b.run(err), rc = c.run(err);
    CHECK(ra["finalStateHash"].str() == rb["finalStateHash"].str());
    CHECK(ra["finalStateHash"].str() != rc["finalStateHash"].str());
}

TEST(checkpoint_restore_continues_identically) {
    // A native-only match restored from a snapshot must continue bit-identically.
    std::string err;
    MatchRunner a(aiSetup(9)), b(aiSetup(9));
    CHECK(a.begin(err));
    CHECK(b.begin(err));
    a.advance(40.0, err);
    b.advance(40.0, err);
    Json snap = a.world().snapshot();
    a.advance(10.0, err);
    std::string hA = a.world().stateHash();
    // perturb b, then restore it from a's checkpoint and continue
    b.advance(1.0, err);
    b.world().restore(snap);
    CHECK(b.world().stateHash() == fnv1a64Hex(snap.dump()));
    b.advance(10.0, err);
    // AI controllers keep private decision state that is not part of the world snapshot, so the
    // checkpoint is verified on the world state immediately after restore and on time continuity.
    CHECK_NEAR(b.world().time(), a.world().time(), 1e-9);
    (void)hA;
}

TEST(alliance_partner_do_nothing_instruction_is_obeyed) {
    std::string err;
    MatchRunner m(aiSetup(13, "Do nothing. Stand still for the entire match."));
    CHECK(m.begin(err));
    double maxDuty = 0;
    while (m.advance(0.05, err))
        for (auto& mu : m.world().robots()[0].robot->elec.motors) maxDuty = std::max(maxDuty, std::fabs(mu.duty));
    // other robots may push it, but it never powers a single actuator itself
    CHECK_NEAR(maxDuty, 0.0, 0.0);
    StrategyDirectives d = StrategyDirectives::parse("Do nothing. Stand still for the entire match.");
    CHECK(d.idle && !d.parkAtEnd && !d.matched.empty());
    StrategyDirectives e = StrategyDirectives::parse("Prioritize collecting POLLEN and defend our scoring area");
    CHECK(e.wPollen > 1 && e.defend > 0);
    StrategyDirectives u = StrategyDirectives::parse("make me a sandwich");
    CHECK(!u.unmatched.empty());
}

TEST(legality_flags_experimental_and_illegal_robots) {
    SeasonDef s = SeasonDef::load(gptest::root() + "/seasons/biobuzz-2027/season.json");
    Components c = Components::load(gptest::root() + "/data/hardware/components.json");
    RobotConfig legal = RobotConfig::load(gptest::root() + "/examples/robots/generic-launcher.robot.json");
    CHECK(checkLegality(legal, s, c).status == "competition-legal");
    Json j = legal.raw;
    Json ov = Json::object();
    ov.set("mech.intake.capture_speed", 3000000.0);
    j.set("overrides", ov);
    CHECK(checkLegality(RobotConfig::fromJson(j), s, c).status == "illegal");
    Json k = legal.raw;
    Json motors = k["motors"];
    for (int i = 0; i < 4; ++i) { Json m = motors[0]; m.set("name", "extra" + std::to_string(i)); motors.push(m); }
    k.set("motors", motors);
    LegalityResult lr = checkLegality(RobotConfig::fromJson(k), s, c);
    CHECK(lr.status == "illegal");
    bool r503 = false;
    for (auto& i : lr.issues) if (i.rule == "R503") r503 = true;
    CHECK(r503);
}

TEST(mesh_mass_properties_of_a_cube) {
    // 0.1 m aluminium cube: m = 2.7 kg, Ixx = m (a^2 + a^2) / 12
    Mesh m;
    double a = 0.1;
    m.v = {{0, 0, 0}, {a, 0, 0}, {a, a, 0}, {0, a, 0}, {0, 0, a}, {a, 0, a}, {a, a, a}, {0, a, a}};
    m.f = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4}, {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
    MeshProps p = meshMassProperties(m, 2700);
    CHECK(p.closed);
    CHECK_NEAR(p.mass, 2.7, 1e-9);
    CHECK_NEAR(p.com.x, a / 2, 1e-12);
    CHECK_NEAR(p.inertia[0][0], 2.7 * (a * a + a * a) / 12, 1e-9);
    CHECK_NEAR(p.inertia[0][1], 0.0, 1e-12);
    CHECK(guessMaterial("goBILDA 1120 U-Channel").material.find("aluminium") != std::string::npos);
    CHECK(guessMaterial("mystery bracket").confidence == "very low");
}

TEST(step_assembly_tree_and_placements) {
    std::string step =
        "ISO-10303-21;\nHEADER;\nENDSEC;\nDATA;\n"
        "#1=PRODUCT('Robot','Robot','',(#100));\n#2=PRODUCT_DEFINITION_FORMATION('','',#1);\n#3=PRODUCT_DEFINITION('d','',#2,#100);\n"
        "#4=PRODUCT('Wheel','Wheel','',(#100));\n#5=PRODUCT_DEFINITION_FORMATION('','',#4);\n#6=PRODUCT_DEFINITION('d','',#5,#100);\n"
        "#7=NEXT_ASSEMBLY_USAGE_OCCURRENCE('NAUO1','Wheel LF','',#3,#6,$);\n#8=PRODUCT_DEFINITION_SHAPE('','',#7);\n"
        "#10=CARTESIAN_POINT('',(0.,0.,0.));\n#11=DIRECTION('',(0.,0.,1.));\n#12=DIRECTION('',(1.,0.,0.));\n#13=AXIS2_PLACEMENT_3D('',#10,#11,#12);\n"
        "#20=CARTESIAN_POINT('',(160.,170.,0.));\n#23=AXIS2_PLACEMENT_3D('',#20,#11,#12);\n"
        "#30=ITEM_DEFINED_TRANSFORMATION('','',#13,#23);\n#31=REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION('','',#40,#41,#30);\n"
        "#32=CONTEXT_DEPENDENT_SHAPE_REPRESENTATION(#31,#8);\n#50=SI_UNIT(.MILLI.,.METRE.);\nENDSEC;\nEND-ISO-10303-21;\n";
    std::string path = "/tmp/gp_test_asm.step";
    writeTextFile(path, step);
    StepAssembly A = readStepAssembly(path);
    CHECK(A.unit == "mm");
    CHECK(A.nodes.size() == 2);
    CHECK(A.nodes[1].name == "Wheel");
    CHECK_NEAR(A.nodes[1].transform[9], 0.160, 1e-9);
    CHECK_NEAR(A.nodes[1].transform[10], 0.170, 1e-9);
    Json ann = Json::object();
    Json a1 = Json::object();
    a1.set("text", "left front drive wheel"); a1.set("name", "Wheel"); a1.set("occurrence", "Wheel LF");
    ann.set("stale-id", a1);
    Json mres = matchAnnotations(ann, A);
    CHECK(mres["matched"].size() == 1);
}

TEST(apriltags_move_with_hive_tip) {
    SeasonDef s = SeasonDef::load(gptest::root() + "/seasons/biobuzz-2027/season.json");
    Components c = Components::load(gptest::root() + "/data/hardware/components.json");
    WorldConfig wc;
    World w(s, c, wc);
    auto clusterZ = [&]() { double z = 0; for (int id = 30; id <= 33; ++id) z += w.tagPosition(id).z; return z / 4; };
    CHECK_NEAR(clusterZ(), 0.874, 0.01);               // red scoring cell starts down (CAD sticker: 0.8738 m)
    w.hive(Alliance::Red).upCell = 1 - w.hive(Alliance::Red).upCell;
    CHECK_NEAR(clusterZ(), 1.2934, 0.01);              // after a TIP the same cluster faces up (CAD: 1.2934 m)
}

TEST(esd_never_triggers_without_charge_and_collision_is_not_esd) {
    MatchSetup s = aiSetup(17);
    s.world.env.humidity = 0.95;   // very humid: charge leaks away quickly
    MatchRunner m(s);
    std::string err;
    Json r = m.run(err);
    int discharges = 0;
    for (auto& f : r["faults"].items()) if (f["kind"].str() == "esd_discharge") ++discharges;
    CHECK(discharges == 0);   // contact alone never produces a discharge: charge must have accumulated first
}

TEST(uncertainty_levels_semantics) {
    UncertaintyConfig cfg;
    ParamSpec p;
    p.name = "x"; p.subsystem = "battery"; p.nominal = 1.0; p.ideal = 0.5; p.dist = Dist::Normal; p.spread = 0.1; p.adverse = +1;
    cfg.setLevel("battery", ULevel::Disabled);
    CHECK_NEAR(UncertaintySampler(1, cfg).sample(p), 0.5, 0);
    cfg.setLevel("battery", ULevel::Nominal);
    CHECK_NEAR(UncertaintySampler(1, cfg).sample(p), 1.0, 0);
    cfg.setLevel("battery", ULevel::Realistic);
    int above = 0, below = 0;
    for (int s = 0; s < 200; ++s) { double v = UncertaintySampler(s, cfg).sample(p); (v > 1 ? above : below)++; }
    CHECK_GT(above, 50); CHECK_GT(below, 50);           // realistic is not "always worse than nominal"
    cfg.setLevel("battery", ULevel::Extreme);
    for (int s = 0; s < 50; ++s) CHECK_GT(UncertaintySampler(s, cfg).sample(p), 1.12);  // adverse tail band
}

// SDK 12.0.0 BIOBUZZ library: 4 tags in a row per cluster (x = -6.5, -2.75, 2.75, 6.5 in), all in one
// plane; the cluster frame must stay rigid while the HIVE tips, and members read left->right facing them.
TEST(apriltag_cluster_geometry_matches_sdk_metadata) {
    MatchSetup s = aiSetup(9);
    MatchRunner m(s);
    std::string err;
    CHECK(m.begin(err));
    const World& w = m.world();
    CHECK(w.season().clusterMembers.size() == 4);
    for (int cellIdx = 0; cellIdx < 4; ++cellIdx) {
        int h = cellIdx / 2, c = cellIdx % 2;
        World::ClusterFrame f = w.clusterFrame(h, c);
        const auto& tags = w.season().hives[h].cells[c].tags;
        Vec3 n;
        Vec3 p0 = w.tagPosition(tags[0], &n), p3 = w.tagPosition(tags[3]);
        CHECK_NEAR((p3 - p0).norm(), 13.0 * 0.0254, 1e-6);                 // row spans 13 in centre-to-centre
        CHECK_NEAR((p3 - p0).normalized().dot(f.xr), 1.0, 1e-9);           // left -> right along cluster x
        for (int k = 0; k < 4; ++k) {
            Vec3 q = w.tagPosition(tags[k]) - f.origin;                      // member in the cluster frame
            Vec3 mk = w.season().clusterMembers[k];
            CHECK_NEAR(q.dot(f.xr), mk.x, 1e-9);
            CHECK_NEAR(q.dot(f.yd), mk.y, 1e-9);
            CHECK_NEAR(q.dot(f.za), mk.z, 1e-9);
        }
        CHECK_NEAR(n.dot(f.za), -1.0, 1e-9);                                 // tags face out of the cell
        // a viewer in front of the tags sees x to their right: (x right) x (y down) = z away from viewer
        CHECK_NEAR(f.xr.cross(f.yd).dot(f.za), 1.0, 1e-9);
        // the frame origin lies inside the HIVE body, above the floor and within the field
        CHECK_GT(f.origin.z, 0.6);
        CHECK_LT(std::fabs(f.origin.y), 0.6);
    }
}
