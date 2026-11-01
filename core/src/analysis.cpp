#include "gameplan/analysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

#include "gameplan/math.hpp"
#include "gameplan/recorder.hpp"

namespace gp {

namespace {
struct Table {
    std::vector<std::string> cols;
    std::vector<std::vector<double>> rows;
    int idx(const std::string& c) const {
        for (size_t i = 0; i < cols.size(); ++i) if (cols[i] == c) return int(i);
        return -1;
    }
    std::vector<double> col(const std::string& c) const {
        std::vector<double> v;
        int k = idx(c);
        if (k < 0) return v;
        for (auto& r : rows) v.push_back(k < int(r.size()) ? r[k] : 0.0);
        return v;
    }
};
bool exists(const std::string& p) { std::ifstream a(p), b(p + ".gz"); return bool(a) || bool(b); }
Table load(const std::string& p) {
    Table t;
    if (!exists(p)) return t;
    t.rows = readGzTable(p, &t.cols);
    return t;
}
std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '&') o += "&amp;"; else if (c == '"') o += "&quot;"; else o += c;
    }
    return o;
}
std::string fmt(double v, int p = 2) { char b[32]; std::snprintf(b, sizeof b, "%.*f", p, v); return b; }
std::vector<double> decimate(const std::vector<double>& v, size_t n) {
    if (v.size() <= n) return v;
    std::vector<double> o;
    double step = double(v.size()) / n;
    for (size_t i = 0; i < n; ++i) o.push_back(v[size_t(i * step)]);
    return o;
}
double std_accumulate_mean(const std::vector<double>& v) {
    double s = 0;
    for (double x : v) s += x;
    return v.empty() ? 0 : s / v.size();
}
double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    double k = p * (v.size() - 1);
    size_t lo = size_t(std::floor(k)), hi = std::min(v.size() - 1, lo + 1);
    return lerp(v[lo], v[hi], k - lo);
}
}  // namespace

std::string svgLineChart(const std::string& title, const std::string& yLabel, const std::vector<double>& xIn,
                         const std::vector<std::pair<std::string, std::vector<double>>>& seriesIn, double yMin, double yMax,
                         const std::vector<std::pair<double, std::string>>& markers) {
    const int W = 720, H = 220, L = 56, R = 12, T = 26, B = 30;
    std::vector<double> x = decimate(xIn, 900);
    std::vector<std::pair<std::string, std::vector<double>>> series;
    for (auto& s : seriesIn) series.push_back({s.first, decimate(s.second, 900)});
    if (x.empty()) return "<p class=muted>no data for " + esc(title) + "</p>";
    double x0 = x.front(), x1 = x.back();
    if (yMin == yMax) {
        yMin = 1e18; yMax = -1e18;
        for (auto& s : series) for (double v : s.second) { yMin = std::min(yMin, v); yMax = std::max(yMax, v); }
        if (yMin == yMax) { yMin -= 1; yMax += 1; }
        double pad = (yMax - yMin) * 0.08;
        yMin -= pad; yMax += pad;
    }
    auto X = [&](double v) { return L + (v - x0) / std::max(x1 - x0, 1e-9) * (W - L - R); };
    auto Y = [&](double v) { return T + (1 - (v - yMin) / (yMax - yMin)) * (H - T - B); };
    std::ostringstream s;
    s << "<figure class=chart><svg viewBox='0 0 " << W << " " << H << "' role=img aria-label='" << esc(title) << "'>";
    s << "<text x='" << L << "' y='16' class=ct>" << esc(title) << "</text>";
    for (int k = 0; k <= 4; ++k) {
        double v = yMin + (yMax - yMin) * k / 4.0;
        s << "<line x1='" << L << "' x2='" << W - R << "' y1='" << Y(v) << "' y2='" << Y(v) << "' class=grid />";
        s << "<text x='" << L - 6 << "' y='" << Y(v) + 4 << "' class=ax text-anchor=end>" << fmt(v, std::fabs(yMax - yMin) < 5 ? 2 : 0) << "</text>";
    }
    for (int k = 0; k <= 6; ++k) {
        double v = x0 + (x1 - x0) * k / 6.0;
        s << "<text x='" << X(v) << "' y='" << H - 10 << "' class=ax text-anchor=middle>" << fmt(v, 0) << "s</text>";
    }
    s << "<text x='12' y='" << (T + H - B) / 2 << "' class=ax transform='rotate(-90 12 " << (T + H - B) / 2 << ")' text-anchor=middle>" << esc(yLabel) << "</text>";
    for (auto& m : markers) {
        if (m.first < x0 || m.first > x1) continue;
        s << "<line x1='" << X(m.first) << "' x2='" << X(m.first) << "' y1='" << T << "' y2='" << H - B << "' class=mk><title>" << esc(m.second) << "</title></line>";
    }
    int si = 0;
    for (auto& ser : series) {
        s << "<polyline class='s s" << (si % 6) << "' points='";
        for (size_t i = 0; i < x.size() && i < ser.second.size(); ++i) s << fmt(X(x[i]), 1) << "," << fmt(Y(clampd(ser.second[i], yMin, yMax)), 1) << " ";
        s << "'><title>" << esc(ser.first) << "</title></polyline>";
        ++si;
    }
    s << "</svg><figcaption>";
    si = 0;
    for (auto& ser : series) s << "<span class='key k" << (si++ % 6) << "'></span>" << esc(ser.first) << " ";
    s << "</figcaption></figure>";
    return s.str();
}

Json analyseRobotElectrical(const std::string& dir, int robot) {
    Table t = load(dir + "/robot_" + std::to_string(robot) + ".csv");
    Json j = Json::object();
    if (t.rows.empty()) return j;
    auto bv = t.col("battV"), bi = t.col("battI"), tt = t.col("t");
    double dt = tt.size() > 1 ? tt[1] - tt[0] : 0.005;
    double energy = 0;
    for (size_t i = 0; i < bv.size(); ++i) energy += bv[i] * bi[i] * dt;
    j.set("minBatteryV", *std::min_element(bv.begin(), bv.end()));
    j.set("peakBatteryA", *std::max_element(bi.begin(), bi.end()));
    j.set("energy_J", energy);
    j.set("meanBatteryA", std_accumulate_mean(bi));
    // per-device energy and peaks
    Json dev = Json::array();
    for (auto& c : t.cols) {
        if (c.size() > 2 && c.substr(c.size() - 2) == "_I") {
            std::string name = c.substr(0, c.size() - 2);
            auto I = t.col(c);
            auto V = t.col(name + "_V");
            double e = 0, pk = 0;
            for (size_t i = 0; i < I.size(); ++i) { e += std::fabs(I[i] * (i < V.size() ? V[i] : 0)) * dt; pk = std::max(pk, std::fabs(I[i])); }
            Json o = Json::object();
            o.set("device", name); o.set("energy_J", e); o.set("peak_A", pk);
            auto tw = t.col(name + "_tw");
            if (!tw.empty()) o.set("maxWinding_C", *std::max_element(tw.begin(), tw.end()));
            auto lim = t.col(name + "_limited");
            if (!lim.empty()) { double tl = 0; for (double l : lim) tl += l * dt; o.set("currentLimited_s", tl); }
            o.set("kind", V.empty() ? "unknown" : (t.idx(name + "_duty") >= 0 ? "motor" : "servo"));
            o.set("provenance", "calculated (coupled electrical model)");
            dev.push(o);
        }
    }
    j.set("devices", dev);
    return j;
}


std::vector<SlowdownExplanation> explainDrivetrainSlowdowns(const std::string& dir, int robot, int maxResults) {
    std::vector<SlowdownExplanation> out;
    Table t = load(dir + "/robot_" + std::to_string(robot) + ".csv");
    Table fr = load(dir + "/frames.csv");
    if (t.rows.empty() || fr.rows.empty()) return out;
    Json header = Json::parseFile(dir + "/header.json");
    // drivetrain motors = those referenced as wheels in the robot config
    std::vector<std::string> drive;
    try {
        Json rc = Json::parseFile(header["robots"][robot]["robotConfig"].str());
        for (auto& w : rc["drivetrain"]["wheels"].items()) drive.push_back(w["motor"].str());
    } catch (...) { return out; }
    auto tt = t.col("t"), ocv = t.col("ocv"), vrc = t.col("vrc"), r0 = t.col("r0"), bI = t.col("battI"), bV = t.col("battV");
    std::string busCol;
    for (auto& c : t.cols) if (c.size() > 5 && c.substr(c.size() - 5) == "_busV") { busCol = c; break; }
    auto bus = t.col(busCol);
    if (ocv.empty() || bus.empty()) return out;
    double ocv0 = ocv.front();
    // windows where all drive duties are high
    std::vector<std::vector<double>> duty, cur, tw, rpm;
    for (auto& m : drive) { duty.push_back(t.col(m + "_duty")); cur.push_back(t.col(m + "_I")); tw.push_back(t.col(m + "_tw")); rpm.push_back(t.col(m + "_rpm")); }
    struct Cand { size_t i; double loss; };
    std::vector<Cand> cands;
    for (size_t i = 0; i < tt.size(); i += 20) {
        double dmin = 1;
        for (auto& d : duty) dmin = std::min(dmin, d.empty() ? 0.0 : std::fabs(d[i]));
        if (dmin < 0.85) continue;
        double vLoss = (ocv0 - bus[i]) / ocv0;
        if (vLoss > 0.08) cands.push_back({i, vLoss});
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.loss > b.loss; });
    std::vector<size_t> picked;
    for (auto& c : cands) {
        bool near = false;
        for (size_t p : picked) if (std::fabs(tt[p] - tt[c.i]) < 2.0) near = true;
        if (near) continue;
        picked.push_back(c.i);
        if (int(picked.size()) >= maxResults) break;
    }
    for (size_t i : picked) {
        SlowdownExplanation e;
        e.tStart = tt[i] - 0.25;
        e.tEnd = tt[i] + 0.25;
        double soc = ocv0 - ocv[i];
        double pol = vrc[i];
        double ir = bI[i] * r0[i];
        double wiring = bV[i] - bus[i];
        double heatLoss = 0;  // winding resistance rise reduces torque at given voltage
        for (auto& w : tw) if (!w.empty()) heatLoss += 0.00393 * std::max(0.0, w[i] - 25.0) / tw.size();
        double total = soc + pol + ir + wiring;
        // split I*R0 by who drew the current
        double dI = 0, other = 0;
        for (size_t k = 0; k < drive.size(); ++k) if (!cur[k].empty()) dI += std::fabs(cur[k][i] * duty[k][i]);
        other = std::max(0.0, bI[i] - dI);
        e.causes.push_back({"battery internal resistance (drivetrain's own current)", total > 0 ? ir * (dI / std::max(bI[i], 1e-6)) / total : 0});
        e.causes.push_back({"battery internal resistance (other subsystems' current)", total > 0 ? ir * (other / std::max(bI[i], 1e-6)) / total : 0});
        e.causes.push_back({"wiring / switch / connector drop", total > 0 ? wiring / total : 0});
        e.causes.push_back({"battery polarisation (recent load history)", total > 0 ? pol / total : 0});
        e.causes.push_back({"state-of-charge decline since match start", total > 0 ? soc / total : 0});
        std::ostringstream s;
        s << "At t=" << fmt(tt[i], 1) << " s the drivetrain was commanded >=85% duty while the hub bus sat at " << fmt(bus[i], 2)
          << " V versus " << fmt(ocv0, 2) << " V open-circuit at match start (" << fmt(100 * (ocv0 - bus[i]) / ocv0, 0)
          << "% less voltage => proportionally less top speed and torque). Battery current " << fmt(bI[i], 1) << " A, of which "
          << fmt(other, 1) << " A was drawn by non-drivetrain devices.";
        if (heatLoss > 0.02) s << " Motor windings were warm enough to raise resistance by " << fmt(heatLoss * 100, 1) << "% (less torque per volt).";
        e.text = s.str();
        e.commanded = 1.0;
        out.push_back(e);
    }
    return out;
}

Json analyseLoopTiming(const std::string& dir, int robot) {
    Table t = load(dir + "/loops_" + std::to_string(robot) + ".csv");
    Json j = Json::object();
    if (t.rows.empty()) return j;
    auto ts = t.col("tStart"), te = t.col("tEnd"), hw = t.col("hwUs"), i2c = t.col("i2cUs"), cp = t.col("computeUs"), ov = t.col("overheadUs"), tr = t.col("transactions");
    std::vector<double> dur;
    for (size_t i = 0; i < ts.size(); ++i) dur.push_back((te[i] - ts[i]) * 1000.0);
    j.set("loops", double(dur.size()));
    j.set("meanMs", std_accumulate_mean(dur));
    j.set("p50Ms", percentile(dur, 0.5));
    j.set("p95Ms", percentile(dur, 0.95));
    j.set("p99Ms", percentile(dur, 0.99));
    j.set("maxMs", dur.empty() ? 0 : *std::max_element(dur.begin(), dur.end()));
    j.set("meanHz", 1000.0 / std::max(std_accumulate_mean(dur), 1e-6));
    double sh = 0, si = 0, sc = 0, so = 0;
    for (size_t i = 0; i < hw.size(); ++i) { sh += hw[i]; si += i2c[i]; sc += cp[i]; so += ov[i]; }
    double tot = sh + si + sc + so;
    Json br = Json::object();
    br.set("lynxTransactions", tot > 0 ? sh / tot : 0); br.set("i2c", tot > 0 ? si / tot : 0);
    br.set("userCompute", tot > 0 ? sc / tot : 0); br.set("frameworkOverhead", tot > 0 ? so / tot : 0);
    j.set("timeShare", br);
    j.set("meanTransactionsPerLoop", std_accumulate_mean(tr));
    int over = 0;
    double p50 = percentile(dur, 0.5);
    for (double d : dur) if (d > 2.5 * p50 && d > 20) ++over;
    j.set("overruns", over);
    j.set("provenance", "calculated by the runtime's per-call cost model (estimated costs, see data/hardware/timing_profiles.json)");
    return j;
}

Json analyseDriverVsRobot(const std::string& dir, int robot) {
    Table fr = load(dir + "/frames.csv");
    Table rt = load(dir + "/robot_" + std::to_string(robot) + ".csv");
    Json j = Json::object();
    if (fr.rows.empty()) return j;
    std::string p = "r" + std::to_string(robot) + "_";
    auto t = fr.col("t"), vx = fr.col(p + "vx"), vy = fr.col(p + "vy"), held = fr.col(p + "held"), ph = fr.col("phase");
    double dt = t.size() > 1 ? t[1] - t[0] : 0.02;
    double teleop = 0, idleHolding = 0, idleEmpty = 0, moving = 0, sumSpeed = 0;
    for (size_t i = 0; i < t.size(); ++i) {
        if (int(ph[i]) != 3) continue;
        teleop += dt;
        double sp = std::hypot(vx[i], vy[i]);
        sumSpeed += sp * dt;
        if (sp < 0.05) { if (held[i] > 0) idleHolding += dt; else idleEmpty += dt; } else moving += dt;
    }
    j.set("teleop_s", teleop);
    j.set("moving_s", moving);
    j.set("stationaryHolding_s", idleHolding);
    j.set("stationaryEmpty_s", idleEmpty);
    j.set("meanSpeed_mps", teleop > 0 ? sumSpeed / teleop : 0);
    // robot capability reference: achievable top speed from drive duty vs speed
    double vmax = 0;
    for (size_t i = 0; i < t.size(); ++i) vmax = std::max(vmax, std::hypot(vx[i], vy[i]));
    j.set("peakSpeed_mps", vmax);
    j.set("speedUtilisation", vmax > 0 && teleop > 0 ? (sumSpeed / teleop) / vmax : 0);
    j.set("interpretation",
          "stationaryHolding_s is time the robot held elements without moving (driver hesitation or waiting to launch); "
          "low speedUtilisation with high peak speed points to driver behaviour rather than robot limits; "
          "high current-limit or brownout time points to robot electrical limits (see electrical section).");
    return j;
}

void writeMatchReport(const std::string& dir, const std::string& outHtml) {
    Json header = Json::parseFile(dir + "/header.json");
    Json result = Json::parseFile(dir + "/result.json");
    std::vector<std::pair<double, std::string>> markers;
    {
        std::ifstream f(dir + "/events.jsonl");
        std::string line;
        while (std::getline(f, line)) {
            try {
                Json e = Json::parse(line);
                std::string k = e["kind"].str();
                if (k == "fault" || k == "tip_start" || k == "driver_station") markers.push_back({e["t"].num(), k + " " + e["detail"].dump()});
            } catch (...) {}
        }
    }
    std::ostringstream h;
    h << "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>Match Report</title><style>"
         ":root{--bg:#fbfbf9;--fg:#1d1f21;--muted:#6b6f75;--line:#e3e3df;--card:#fff;--s0:#2563eb;--s1:#dc2626;--s2:#059669;--s3:#d97706;--s4:#7c3aed;--s5:#0891b2;--red:#c0392b;--blue:#1f5fbf}"
         "@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#121416;--fg:#e8e8e6;--muted:#9aa0a6;--line:#2a2e33;--card:#181b1e;--s0:#60a5fa;--s1:#f87171;--s2:#34d399;--s3:#fbbf24;--s4:#a78bfa;--s5:#22d3ee}}"
         "body{background:var(--bg);color:var(--fg);font:14px/1.5 system-ui,-apple-system,Segoe UI,sans-serif;margin:0 auto;max-width:980px;padding:24px 16px}"
         "h1{font-size:22px;margin:0 0 4px}h2{font-size:16px;margin:32px 0 8px;border-bottom:1px solid var(--line);padding-bottom:4px}"
         "h3{font-size:14px;margin:18px 0 6px}.muted{color:var(--muted)}table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}"
         "td,th{border-bottom:1px solid var(--line);padding:4px 6px;text-align:left;vertical-align:top}th{color:var(--muted);font-weight:500}"
         ".score{font-size:28px;font-weight:600}.red{color:var(--red)}.blue{color:var(--blue)}"
         "svg{width:100%;height:auto}.grid{stroke:var(--line);stroke-width:1}.ax{fill:var(--muted);font-size:10px}.ct{fill:var(--fg);font-size:12px;font-weight:600}"
         ".s{fill:none;stroke-width:1.4}.s0{stroke:var(--s0)}.s1{stroke:var(--s1)}.s2{stroke:var(--s2)}.s3{stroke:var(--s3)}.s4{stroke:var(--s4)}.s5{stroke:var(--s5)}"
         ".mk{stroke:var(--muted);stroke-dasharray:2 3}figure{margin:8px 0 16px}figcaption{font-size:12px;color:var(--muted)}"
         ".key{display:inline-block;width:10px;height:3px;margin:0 4px 3px 8px}.k0{background:var(--s0)}.k1{background:var(--s1)}.k2{background:var(--s2)}.k3{background:var(--s3)}.k4{background:var(--s4)}.k5{background:var(--s5)}"
         "code{font-size:12px}.tag{font-size:11px;color:var(--muted);border:1px solid var(--line);border-radius:3px;padding:0 4px;margin-left:6px}"
         "</style></head><body>";
    h << "<h1>Match report</h1><div class=muted>" << esc(header["season"]["id"].str()) << " · rules " << esc(header["season"]["rulesEngine"].str())
      << " · manual " << esc(header["season"]["manual"].str()) << " · seed " << header["world"]["seed"].dump() << " · " << esc(header["mode"].str()) << "</div>";
    h << "<p><span class='score red'>" << result["red"]["total"].integer() << "</span> &nbsp;–&nbsp; <span class='score blue'>"
      << result["blue"]["total"].integer() << "</span></p>";
    h << "<table><tr><th></th><th>AUTO</th><th>TELEOP</th><th>END</th><th>Foul credit</th><th>TIPS</th><th>RP</th></tr>";
    for (const char* a : {"red", "blue"}) {
        const Json& s = result[a];
        h << "<tr><td class=" << a << ">" << a << "</td><td>" << s["auto"].integer() << "</td><td>" << s["teleop"].integer() << "</td><td>" << s["end"].integer()
          << "</td><td>" << s["foulCredit"].integer() << "</td><td>" << s["tips"].integer() << "</td><td>" << s["rp"].integer() << " ";
        for (auto& d : s["rpDetail"].items()) h << "<span class=tag>" << esc(d.str()) << "</span>";
        h << "</td></tr>";
    }
    h << "</table>";
    h << "<h2>Scoring events</h2><table><tr><th>Match time</th><th>Alliance</th><th>Achievement</th><th>Points</th><th>Detail</th></tr>";
    for (auto& e : result["scoreEvents"].items())
        h << "<tr><td>" << fmt(e["matchTime"].num(), 1) << " s</td><td class=" << esc(e["alliance"].str()) << ">" << esc(e["alliance"].str()) << "</td><td>"
          << esc(e["kind"].str()) << "</td><td>" << e["points"].integer() << "</td><td class=muted>" << esc(e["detail"].str()) << "</td></tr>";
    h << "</table>";
    h << "<h2>Violations</h2>";
    if (result["violations"].size() == 0) h << "<p class=muted>None assessed.</p>";
    else {
        h << "<table><tr><th>Time</th><th>Rule</th><th>Robot</th><th>Penalty</th><th>Basis</th></tr>";
        for (auto& v : result["violations"].items())
            h << "<tr><td>" << fmt(v["matchTime"].num(), 1) << " s</td><td>" << esc(v["rule"].str()) << (v["judgment"].boolean() ? "<span class=tag>judgment</span>" : "<span class=tag>objective</span>")
              << "</td><td>" << v["robot"].integer() << "</td><td>" << esc(v["penalty"].str()) << " (" << v["foulPoints"].integer() << ")</td><td class=muted>" << esc(v["rationale"].str()) << "</td></tr>";
        h << "</table>";
    }
    h << "<h2>Robot faults</h2>";
    if (result["faults"].size() == 0) h << "<p class=muted>No ESD discharges, communication losses or controller resets.</p>";
    else {
        h << "<table><tr><th>Time</th><th>Robot</th><th>Event</th><th>Target</th><th>Duration</th><th>Cause</th></tr>";
        for (auto& f : result["faults"].items())
            h << "<tr><td>" << fmt(f["t"].num(), 2) << "</td><td>" << f["robot"].integer() << "</td><td>" << esc(f["kind"].str()) << "</td><td>" << esc(f["target"].str())
              << "</td><td>" << (f["duration"].num() < 0 ? std::string("no recovery") : fmt(f["duration"].num(), 2) + " s") << "</td><td class=muted>" << esc(f["cause"].str()) << "</td></tr>";
        h << "</table>";
    }
    for (size_t r = 0; r < header["robots"].size(); ++r) {
        const Json& rh = header["robots"][r];
        h << "<h2>" << esc(rh["label"].str()) << " — " << esc(rh["name"].str()) << " <span class=tag>" << esc(rh["controller"].str()) << "</span></h2>";
        Table t = load(dir + "/robot_" + std::to_string(r) + ".csv");
        if (!t.rows.empty()) {
            auto tt = t.col("t");
            std::vector<std::pair<std::string, std::vector<double>>> volts = {{"battery terminal", t.col("battV")}, {"open-circuit", t.col("ocv")}};
            for (auto& c : t.cols) if (c.size() > 5 && c.substr(c.size() - 5) == "_busV") volts.push_back({c.substr(0, c.size() - 5) + " hub bus", t.col(c)});
            h << svgLineChart("Battery and hub voltage", "V", tt, volts, 0, 0, markers);
            std::vector<std::pair<std::string, std::vector<double>>> cur = {{"battery total", t.col("battI")}};
            for (auto& c : t.cols) if (c.size() > 2 && c.substr(c.size() - 2) == "_I" && t.idx(c.substr(0, c.size() - 2) + "_duty") >= 0) cur.push_back({c.substr(0, c.size() - 2), t.col(c)});
            h << svgLineChart("Current (battery and motors)", "A", tt, cur);
            std::vector<std::pair<std::string, std::vector<double>>> sv;
            for (auto& c : t.cols) if (c.size() > 2 && c.substr(c.size() - 2) == "_I" && t.idx(c.substr(0, c.size() - 2) + "_cmd") >= 0) sv.push_back({c.substr(0, c.size() - 2), t.col(c)});
            if (!sv.empty()) h << svgLineChart("Servo current (model estimate)", "A", tt, sv);
            std::vector<std::pair<std::string, std::vector<double>>> temps;
            for (auto& c : t.cols) if (c.size() > 3 && c.substr(c.size() - 3) == "_tw") temps.push_back({c.substr(0, c.size() - 3), t.col(c)});
            h << svgLineChart("Estimated motor winding temperature", "°C", tt, temps);
            Json el = analyseRobotElectrical(dir, int(r));
            h << "<h3>Energy and peaks <span class=tag>calculated</span></h3><p>Minimum battery terminal voltage <b>" << fmt(el["minBatteryV"].num(), 2)
              << " V</b>, peak battery current <b>" << fmt(el["peakBatteryA"].num(), 1) << " A</b>, energy drawn <b>" << fmt(el["energy_J"].num() / 1000.0, 2) << " kJ</b>.</p>";
            h << "<table><tr><th>Device</th><th>Kind</th><th>Energy (J)</th><th>Peak (A)</th><th>Max winding (°C)</th><th>Current-limited (s)</th></tr>";
            for (auto& d : el["devices"].items())
                h << "<tr><td>" << esc(d["device"].str()) << "</td><td>" << esc(d["kind"].str()) << "</td><td>" << fmt(d["energy_J"].num(), 0) << "</td><td>" << fmt(d["peak_A"].num(), 2)
                  << "</td><td>" << (d["maxWinding_C"].isNumber() ? fmt(d["maxWinding_C"].num(), 1) : "–") << "</td><td>" << (d["currentLimited_s"].isNumber() ? fmt(d["currentLimited_s"].num(), 2) : "–") << "</td></tr>";
            h << "</table>";
            auto ex = explainDrivetrainSlowdowns(dir, int(r));
            h << "<h3>Why did the drivetrain slow down? <span class=tag>numerical attribution</span></h3>";
            if (ex.empty()) h << "<p class=muted>No interval where the drivetrain was commanded near full power while the bus voltage was more than 8% below its match-start open-circuit value.</p>";
            for (auto& e : ex) {
                h << "<p>" << esc(e.text) << "</p><table><tr><th>Cause of the voltage deficit</th><th>Share</th></tr>";
                for (auto& c : e.causes) h << "<tr><td>" << esc(c.first) << "</td><td>" << fmt(100 * c.second, 0) << "%</td></tr>";
                h << "</table>";
            }
        }
        Json lt = analyseLoopTiming(dir, int(r));
        if (lt.has("loops")) {
            Table lp = load(dir + "/loops_" + std::to_string(r) + ".csv");
            auto ts = lp.col("tStart"), te = lp.col("tEnd");
            std::vector<double> ms;
            for (size_t i = 0; i < ts.size(); ++i) ms.push_back((te[i] - ts[i]) * 1000);
            h << "<h3>Robot program loop timing <span class=tag>estimated cost model</span></h3>";
            h << svgLineChart("Loop duration", "ms", ts, {{"loop", ms}}, 0, 0, markers);
            h << "<p>" << int(lt["loops"].num()) << " loops, mean " << fmt(lt["meanMs"].num(), 1) << " ms (" << fmt(lt["meanHz"].num(), 0) << " Hz), p95 "
              << fmt(lt["p95Ms"].num(), 1) << " ms, max " << fmt(lt["maxMs"].num(), 1) << " ms, " << lt["overruns"].integer() << " overruns; "
              << fmt(lt["meanTransactionsPerLoop"].num(), 1) << " Lynx transactions per loop. Time share: Lynx "
              << fmt(100 * lt["timeShare"]["lynxTransactions"].num(), 0) << "%, I2C " << fmt(100 * lt["timeShare"]["i2c"].num(), 0) << "%, user code "
              << fmt(100 * lt["timeShare"]["userCompute"].num(), 0) << "%, framework " << fmt(100 * lt["timeShare"]["frameworkOverhead"].num(), 0) << "%.</p>";
        }
        for (auto& rt : result["runtimes"].items()) {
            if (rt["robot"].integer() != int(r)) continue;
            const Json& hs = rt["hwStats"];
            if (hs["ops"].isArray() && hs["ops"].size()) {
                h << "<table><tr><th>SDK call</th><th>Calls</th><th>Transactions</th><th>Total time (ms)</th><th>Mean (µs)</th></tr>";
                for (auto& o : hs["ops"].items())
                    h << "<tr><td><code>" << esc(o["op"].str()) << "</code></td><td>" << o["calls"].integer() << "</td><td>" << o["transactions"].integer() << "</td><td>"
                      << fmt(o["totalUs"].num() / 1000.0, 1) << "</td><td>" << fmt(o["meanUs"].num(), 0) << "</td></tr>";
                h << "</table>";
            }
            h << "<p class=muted>Runtime determinism: " << (rt["deterministic"].boolean() ? "deterministic (no user threads / measured compute)" : "NOT deterministic") << "</p>";
        }
        Json dv = analyseDriverVsRobot(dir, int(r));
        if (dv.has("teleop_s"))
            h << "<h3>Driver vs robot</h3><p>TELEOP " << fmt(dv["teleop_s"].num(), 0) << " s: moving " << fmt(dv["moving_s"].num(), 0) << " s, stationary while holding elements "
              << fmt(dv["stationaryHolding_s"].num(), 0) << " s, stationary empty " << fmt(dv["stationaryEmpty_s"].num(), 0) << " s; mean speed "
              << fmt(dv["meanSpeed_mps"].num(), 2) << " m/s vs peak " << fmt(dv["peakSpeed_mps"].num(), 2) << " m/s.</p><p class=muted>" << esc(dv["interpretation"].str()) << "</p>";
    }
    h << "<h2>Reproducibility</h2><table>";
    h << "<tr><td>Season package</td><td><code>" << esc(header["season"]["sha256"].str()) << "</code></td></tr>";
    h << "<tr><td>Component library</td><td><code>" << esc(header["componentsSha256"].str()) << "</code></td></tr>";
    for (auto& rh : header["robots"].items()) h << "<tr><td>" << esc(rh["label"].str()) << " config</td><td><code>" << esc(rh["robotConfigSha256"].str()) << "</code></td></tr>";
    h << "<tr><td>Final state hash</td><td><code>" << esc(result["finalStateHash"].str()) << "</code></td></tr>";
    h << "<tr><td>Note</td><td class=muted>" << esc(header["numericNote"].str()) << "</td></tr></table>";
    h << "</body></html>";
    writeTextFile(outHtml, h.str());
}

}  // namespace gp
