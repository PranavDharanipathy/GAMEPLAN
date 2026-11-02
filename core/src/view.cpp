#include "gameplan/view.hpp"

#include <cmath>

namespace gp {

size_t viewFrameLength(const World& w) {
    return size_t(kViewHeader) + w.robots().size() * kViewRobot + w.elements().size() * kViewElement + 4 * kViewCell + kViewTail;
}

void appendViewFrame(const World& w, const std::vector<int>& robotStatus, std::vector<float>& out) {
    const SeasonDef& season = w.season();
    out.push_back(float(w.time()));
    out.push_back(float(int(w.rules().phase())));
    out.push_back(float(w.rules().score(Alliance::Red).total()));
    out.push_back(float(w.rules().score(Alliance::Blue).total()));
    out.push_back(float(w.robots().size()));
    out.push_back(float(w.elements().size()));
    out.push_back(4.0f);
    for (size_t idx = 0; idx < w.robots().size(); ++idx) {
        const Robot& r = *w.robots()[idx].robot;
        for (double v : {r.pos.x, r.pos.y, r.heading, double(int(r.alliance)), double(r.held.size()), r.tipped ? 1.0 : 0.0,
                         r.cfg.length, r.cfg.width, r.cfg.height})
            out.push_back(float(v));
        double ang[3] = {0, 0, 0};
        for (auto& m : r.mechs) {
            if (m.type == MechType::Intake && m.joint >= 0) ang[0] = r.joints[m.joint].theta;
            if (m.type == MechType::Flywheel && m.joint >= 0) ang[1] = r.joints[m.joint].theta;
            if (m.type == MechType::Hood && !m.servos.empty()) {
                const ServoUnit& sv = r.elec.servos[m.servos[0]];
                double span = sv.spec.rangeRad > 0 ? sv.spec.rangeRad : 1.0;
                ang[2] = m.angleAt0 + clampd(sv.angle / span + 0.5, 0.0, 1.0) * (m.angleAt1 - m.angleAt0);
            }
        }
        for (double v : ang) out.push_back(float(v));
        double wa[4] = {0, 0, 0, 0};
        for (auto& wh : r.wheels) {
            int k = (wh.pos.x > 0 ? 0 : 2) + (wh.pos.y > 0 ? 0 : 1);
            if (wh.joint >= 0) wa[k] = r.joints[wh.joint].theta;
        }
        for (double v : wa) out.push_back(float(v));
        out.push_back(float(r.pitch));
        out.push_back(float(idx < robotStatus.size() ? robotStatus[idx] : 0));
        out.push_back(float(r.roll));
    }
    for (auto& e : w.elements()) {
        bool vis = e.state != ElemState::HumanArea && e.state != ElemState::OutOfField;
        double kind = e.type == ElemType::Pollen ? 0 : (e.color == Alliance::Red ? 1 : 2);
        for (double v : {e.p.x, e.p.y, e.p.z, kind, e.radius, vis ? 1.0 : 0.0, e.q[0], e.q[1], e.q[2], e.q[3], e.squish,
                         e.squishN.x, e.squishN.y, e.squishN.z})
            out.push_back(float(v));
    }
    for (int h = 0; h < 2; ++h)
        for (int c = 0; c < 2; ++c) {
            const CellDef& cd = season.hives[h].cells[c];
            Vec3 p{0, 0, 0};
            for (int id : cd.tags) p += w.tagPosition(id);
            if (!cd.tags.empty()) p = p / double(cd.tags.size());
            bool up = w.hive(Alliance(h)).upCell == c && !w.hive(Alliance(h)).tipping;
            for (double v : {p.x, p.y, p.z, up ? 1.0 : 0.0, double(h)}) out.push_back(float(v));
        }
    // HIVE body rotation about its pivot (+x), 0 = audience CELL up (same convention as World::cellSlot)
    for (int h = 0; h < 2; ++h) {
        const HiveState& hs = w.hive(Alliance(h));
        int aud = season.hives[h].cells[0].side == "audience" ? 0 : 1;
        double down = hs.upCell == aud ? 0.0 : 1.0;
        if (hs.tipping) {
            double f = clampd((w.time() - hs.tipStart) / season.tipSettleS, 0.0, 1.0);
            down = hs.upCell == aud ? f : 1.0 - f;
        }
        out.push_back(float(down * hs.stateAngle));
    }
    for (int k = 0; k < 4; ++k) { out.push_back(float(w.wallDisplacement(k))); out.push_back(float(w.wallVelocity(k))); }
}

Json robotViewInfo(const World& w, int i) {
    Json j = Json::object();
    if (i < 0 || i >= int(w.robots().size())) return j;
    const Robot& r = *w.robots()[i].robot;
    j.set("label", r.label); j.set("name", r.cfg.name); j.set("alliance", toString(r.alliance));
    j.set("length", r.cfg.length); j.set("width", r.cfg.width); j.set("height", r.cfg.height);
    Json wh = Json::array();
    for (auto& x : r.wheels) {
        Json o = Json::object();
        o.set("x", x.pos.x); o.set("y", x.pos.y); o.set("r", x.radius);
        o.set("roller", std::atan2(x.n.y, x.n.x));   // traction direction -> roller angle of a mecanum wheel
        wh.push(o);
    }
    j.set("wheels", wh);
    Json path = Json::array();
    for (auto& p : w.ballPath(i)) { Json o = Json::array(); o.push(p.x); o.push(p.y); o.push(p.z); path.push(o); }
    j.set("ballPath", path);
    for (auto& m : r.mechs) {
        Json o = Json::object();
        if (m.type == MechType::Intake) {
            o.set("rollerRadius", m.rollerRadius); o.set("width", m.width); o.set("reach", m.reach);
            o.set("rollerZ", m.rollerZ > 0 ? m.rollerZ : w.season().pollenD + m.rollerRadius - 0.014);
            o.set("mouthHeight", m.mouthHeight);
            j.set("intake", o);
        }
        if (m.type == MechType::Flywheel) {
            o.set("radius", m.flyRadius); o.set("x", m.launchFwd); o.set("z", m.launchHeight);
            j.set("flywheel", o);
        }
        if (m.type == MechType::Hood) { o.set("a0", m.angleAt0); o.set("a1", m.angleAt1); j.set("hood", o); }
    }
    return j;
}

}  // namespace gp
