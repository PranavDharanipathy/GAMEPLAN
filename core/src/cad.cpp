#include "gameplan/error.hpp"
#include "gameplan/cad.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace gp {

Json MeshProps::toJson() const {
    Json j = Json::object();
    j.set("volume_m3", volume); j.set("density_kg_m3", density); j.set("mass_kg", mass);
    Json c = Json::array(); c.push(com.x); c.push(com.y); c.push(com.z); j.set("com_m", c);
    Json I = Json::array();
    for (int r = 0; r < 3; ++r) { Json row = Json::array(); for (int k = 0; k < 3; ++k) row.push(inertia[r][k]); I.push(row); }
    j.set("inertia_kgm2_about_com", I);
    Json bb = Json::array(); bb.push(bboxMax.x - bboxMin.x); bb.push(bboxMax.y - bboxMin.y); bb.push(bboxMax.z - bboxMin.z);
    j.set("bbox_m", bb);
    j.set("closed", closed); j.set("openEdges", openEdges); j.set("confidence", confidence);
    return j;
}

Mesh loadMesh(const std::string& path, double unitScale) {
    Mesh m;
    m.source = path;
    std::string data = readTextFile(path);
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto addVert = [&](Vec3 p) { m.v.push_back(p); return int(m.v.size()) - 1; };
    if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".obj") {
        std::istringstream ss(data);
        std::string line;
        while (std::getline(ss, line)) {
            std::istringstream ls(line);
            std::string tag;
            ls >> tag;
            if (tag == "v") { Vec3 p; ls >> p.x >> p.y >> p.z; m.v.push_back(p); }
            else if (tag == "f") {
                std::vector<int> idx;
                std::string tok;
                while (ls >> tok) { int i = std::stoi(tok.substr(0, tok.find('/'))); idx.push_back(i < 0 ? int(m.v.size()) + i : i - 1); }
                for (size_t k = 1; k + 1 < idx.size(); ++k) m.f.push_back({idx[0], idx[k], idx[k + 1]});
            }
        }
    } else {
        bool ascii = data.compare(0, 5, "solid") == 0 && data.find("facet") != std::string::npos;
        std::map<std::tuple<long long, long long, long long>, int> weld;
        auto weldV = [&](Vec3 p) {
            auto k = std::make_tuple((long long)std::llround(p.x * 1e6), (long long)std::llround(p.y * 1e6), (long long)std::llround(p.z * 1e6));
            auto it = weld.find(k);
            if (it != weld.end()) return it->second;
            int i = addVert(p);
            weld[k] = i;
            return i;
        };
        if (ascii) {
            std::istringstream ss(data);
            std::string w;
            std::vector<int> tri;
            while (ss >> w) {
                if (w == "vertex") { Vec3 p; ss >> p.x >> p.y >> p.z; tri.push_back(weldV(p)); if (tri.size() == 3) { m.f.push_back({tri[0], tri[1], tri[2]}); tri.clear(); } }
            }
        } else {
            if (data.size() < 84) GP_THROW(std::runtime_error("STL too short: " + path));
            uint32_t n;
            std::memcpy(&n, data.data() + 80, 4);
            if (data.size() < 84 + size_t(n) * 50) GP_THROW(std::runtime_error("truncated binary STL: " + path));
            for (uint32_t t = 0; t < n; ++t) {
                const char* p = data.data() + 84 + size_t(t) * 50 + 12;
                int ids[3];
                for (int k = 0; k < 3; ++k) { float xyz[3]; std::memcpy(xyz, p + 12 * k, 12); ids[k] = weldV({xyz[0], xyz[1], xyz[2]}); }
                m.f.push_back({ids[0], ids[1], ids[2]});
            }
        }
    }
    if (unitScale == 0) {
        double ext = 0;
        for (auto& p : m.v) ext = std::max({ext, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)});
        unitScale = ext > 5.0 ? 0.001 : 1.0;  // CAD exports are usually millimetres
    }
    for (auto& p : m.v) p *= unitScale;
    return m;
}

MeshProps meshMassProperties(const Mesh& m, double density) {
    MeshProps r;
    r.density = density;
    // divergence theorem over signed tetrahedra (origin, a, b, c)
    double V = 0, cx = 0, cy = 0, cz = 0;
    double xx = 0, yy = 0, zz = 0, xy = 0, yz = 0, zx = 0;
    for (auto& f : m.f) {
        Vec3 a = m.v[f[0]], b = m.v[f[1]], c = m.v[f[2]];
        double d = a.dot(b.cross(c)) / 6.0;
        V += d;
        cx += d * (a.x + b.x + c.x) / 4; cy += d * (a.y + b.y + c.y) / 4; cz += d * (a.z + b.z + c.z) / 4;
        auto sq = [&](double p, double q, double s) { return p * p + q * q + s * s + p * q + q * s + s * p; };
        auto pr = [&](double p1, double q1, double s1, double p2, double q2, double s2) {
            return 2 * (p1 * p2 + q1 * q2 + s1 * s2) + (p1 * q2 + q1 * p2 + p1 * s2 + s1 * p2 + q1 * s2 + s1 * q2);
        };
        xx += d * sq(a.x, b.x, c.x) / 10; yy += d * sq(a.y, b.y, c.y) / 10; zz += d * sq(a.z, b.z, c.z) / 10;
        xy += d * pr(a.x, b.x, c.x, a.y, b.y, c.y) / 20; yz += d * pr(a.y, b.y, c.y, a.z, b.z, c.z) / 20; zx += d * pr(a.z, b.z, c.z, a.x, b.x, c.x) / 20;
    }
    if (V < 0) { V = -V; cx = -cx; cy = -cy; cz = -cz; xx = -xx; yy = -yy; zz = -zz; xy = -xy; yz = -yz; zx = -zx; }
    r.volume = V;
    r.mass = V * density;
    if (V > 0) r.com = {cx / V, cy / V, cz / V};
    // inertia about origin then shift to COM (parallel axis)
    double Ixx = density * (yy + zz), Iyy = density * (xx + zz), Izz = density * (xx + yy);
    double Ixy = -density * xy, Iyz = -density * yz, Izx = -density * zx;
    double M = r.mass;
    Vec3 c = r.com;
    r.inertia[0][0] = Ixx - M * (c.y * c.y + c.z * c.z);
    r.inertia[1][1] = Iyy - M * (c.x * c.x + c.z * c.z);
    r.inertia[2][2] = Izz - M * (c.x * c.x + c.y * c.y);
    r.inertia[0][1] = r.inertia[1][0] = Ixy + M * c.x * c.y;
    r.inertia[1][2] = r.inertia[2][1] = Iyz + M * c.y * c.z;
    r.inertia[0][2] = r.inertia[2][0] = Izx + M * c.z * c.x;
    r.bboxMin = {1e18, 1e18, 1e18};
    r.bboxMax = {-1e18, -1e18, -1e18};
    for (auto& p : m.v) {
        r.bboxMin = {std::min(r.bboxMin.x, p.x), std::min(r.bboxMin.y, p.y), std::min(r.bboxMin.z, p.z)};
        r.bboxMax = {std::max(r.bboxMax.x, p.x), std::max(r.bboxMax.y, p.y), std::max(r.bboxMax.z, p.z)};
    }
    std::map<std::pair<int, int>, int> edges;
    for (auto& f : m.f)
        for (int k = 0; k < 3; ++k) {
            int a = f[k], b = f[(k + 1) % 3];
            edges[{std::min(a, b), std::max(a, b)}]++;
        }
    for (auto& e : edges) if (e.second != 2) r.openEdges++;
    r.closed = r.openEdges == 0;
    r.confidence = r.closed ? "geometry exact for the mesh; mass depends on density (see material confidence)"
                            : "LOW: mesh is not closed (" + std::to_string(r.openEdges) + " non-manifold edges); volume and inertia unreliable";
    return r;
}

MaterialGuess guessMaterial(const std::string& name) {
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
    MaterialGuess g;
    if (has("6061") || has("alum") || has("channel") || has("extrusion") || has("u-channel") || has("gobilda 1120") || has("1120-"))
        g = {"aluminium 6061", 2700, 2650, 2750, "medium", "name keyword (aluminium structural part)"};
    else if (has("steel") || has("shaft") || has("screw") || has("bolt") || has("nut"))
        g = {"steel", 7850, 7750, 8000, "medium", "name keyword (fastener/shaft => steel)"};
    else if (has("polycarb") || has("lexan")) g = {"polycarbonate", 1200, 1190, 1220, "high", "name keyword"};
    else if (has("pla")) g = {"PLA (3D printed, infill unknown)", 900, 400, 1240, "low", "printed part: solid density 1240 scaled by unknown infill"};
    else if (has("petg")) g = {"PETG (3D printed, infill unknown)", 900, 400, 1270, "low", "printed part"};
    else if (has("nylon") || has("pa12")) g = {"nylon", 1150, 1000, 1150, "medium", "name keyword"};
    else if (has("motor")) g = {"motor assembly (lumped)", 3300, 2500, 4500, "low", "use manufacturer mass instead of geometry"};
    else if (has("wheel") || has("mecanum")) g = {"wheel assembly (lumped)", 1400, 900, 2200, "low", "mixed materials; prefer measured mass"};
    else if (has("battery")) g = {"NiMH pack (lumped)", 2400, 2200, 2700, "low", "prefer measured mass (~0.6 kg)"};
    else g = {"unknown (default polymer)", 1200, 900, 2700, "very low", "no identifying keyword; ask the user to annotate"};
    return g;
}

// ------------------------------------------------------------------ STEP
namespace {
std::vector<int> refs(const std::string& s) {
    std::vector<int> out;
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] == '#') {
            size_t j = i + 1;
            while (j < s.size() && isdigit((unsigned char)s[j])) ++j;
            if (j > i + 1) out.push_back(std::stoi(s.substr(i + 1, j - i - 1)));
            i = j - 1;
        }
    return out;
}
std::string type(const std::string& s) {
    size_t p = s.find('(');
    std::string t = s.substr(0, p);
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t;
}
std::string firstString(const std::string& s) {
    size_t a = s.find('\'');
    if (a == std::string::npos) return "";
    size_t b = s.find('\'', a + 1);
    return b == std::string::npos ? "" : s.substr(a + 1, b - a - 1);
}
std::string secondString(const std::string& s) {
    size_t a = s.find('\'');
    if (a == std::string::npos) return "";
    size_t b = s.find('\'', a + 1);
    size_t c = s.find('\'', b + 1);
    size_t d = c == std::string::npos ? c : s.find('\'', c + 1);
    return d == std::string::npos ? "" : s.substr(c + 1, d - c - 1);
}
std::vector<double> nums(const std::string& s) {
    std::vector<double> o;
    size_t p = s.find("(");
    std::string body = s.substr(p + 1);
    size_t q = body.find(',');
    if (q != std::string::npos) body = body.substr(q + 1);
    const char* c = body.c_str();
    while (*c) {
        if (isdigit((unsigned char)*c) || *c == '-' || *c == '.') { char* e; o.push_back(std::strtod(c, &e)); c = e; }
        else ++c;
    }
    return o;
}
}  // namespace

StepAssembly readStepAssembly(const std::string& path) {
    StepAssembly A;
    A.file = path;
    std::string text = readTextFile(path);
    size_t ds = text.find("DATA;");
    if (ds == std::string::npos) GP_THROW(std::runtime_error("not a STEP file: " + path));
    std::smatch m;
    std::regex orig("/\\*\\s*originating_system\\s*\\*/\\s*'([^']*)'");
    if (std::regex_search(text, m, orig)) A.originatingSystem = m[1];
    std::map<int, std::string> E;
    size_t i = ds + 5;
    while (i < text.size()) {
        size_t h = text.find('#', i);
        if (h == std::string::npos) break;
        size_t eq = text.find('=', h);
        size_t semi = text.find(';', eq);
        if (eq == std::string::npos || semi == std::string::npos) break;
        // skip ';' inside quoted strings
        bool inq = false;
        size_t k = eq;
        for (; k < text.size(); ++k) { if (text[k] == '\'') inq = !inq; if (text[k] == ';' && !inq) break; }
        semi = k;
        int id = std::atoi(text.c_str() + h + 1);
        std::string body = text.substr(eq + 1, semi - eq - 1);
        std::string clean;
        for (char c : body) if (c != '\n' && c != '\r') clean += c;
        size_t st = clean.find_first_not_of(' ');
        E[id] = st == std::string::npos ? "" : clean.substr(st);
        i = semi + 1;
        if (text.compare(i, 8, "\nENDSEC;") == 0 || text.compare(i, 7, "ENDSEC;") == 0) break;
    }
    for (auto& kv : E)
        if (kv.second.find("SI_UNIT(.MILLI.,.METRE.)") != std::string::npos) { A.unit = "mm"; A.unitScale = 0.001; }
    std::map<int, std::string> prodName;
    std::map<int, int> pdfToProd, pdToPdf;
    for (auto& kv : E) {
        std::string t = type(kv.second);
        if (t == "PRODUCT") prodName[kv.first] = firstString(kv.second);
        else if (t.rfind("PRODUCT_DEFINITION_FORMATION", 0) == 0) { auto r = refs(kv.second); if (!r.empty()) pdfToProd[kv.first] = r[0]; }
        else if (t == "PRODUCT_DEFINITION") { auto r = refs(kv.second); if (!r.empty()) pdToPdf[kv.first] = r[0]; }
    }
    auto pdName = [&](int pd) { auto a = pdToPdf.find(pd); if (a == pdToPdf.end()) return std::string("?"); auto b = pdfToProd.find(a->second); return b == pdfToProd.end() ? std::string("?") : prodName[b->second]; };
    struct Occ { int parent, child; std::string name; };
    std::map<int, Occ> nauo;
    for (auto& kv : E)
        if (type(kv.second) == "NEXT_ASSEMBLY_USAGE_OCCURRENCE") {
            auto r = refs(kv.second);
            if (r.size() >= 2) nauo[kv.first] = {r[0], r[1], secondString(kv.second)};
        }
    std::map<int, int> pdsToNauo;
    for (auto& kv : E)
        if (type(kv.second) == "PRODUCT_DEFINITION_SHAPE") { auto r = refs(kv.second); if (!r.empty() && nauo.count(r[0])) pdsToNauo[kv.first] = r[0]; }
    auto axis = [&](int a, double R[9], double T[3]) {
        auto r = refs(E[a]);
        auto loc = nums(E[r[0]]);
        Vec3 z{0, 0, 1}, x{1, 0, 0};
        if (r.size() > 1) { auto v = nums(E[r[1]]); if (v.size() >= 3) z = {v[0], v[1], v[2]}; }
        if (r.size() > 2) { auto v = nums(E[r[2]]); if (v.size() >= 3) x = {v[0], v[1], v[2]}; }
        z = z.normalized();
        x = (x - z * x.dot(z)).normalized();
        Vec3 y = z.cross(x);
        R[0] = x.x; R[1] = y.x; R[2] = z.x; R[3] = x.y; R[4] = y.y; R[5] = z.y; R[6] = x.z; R[7] = y.z; R[8] = z.z;
        for (int k = 0; k < 3; ++k) T[k] = (k < int(loc.size()) ? loc[k] : 0) * A.unitScale;
    };
    // local transform per occurrence: placement2 * inverse(placement1)
    std::map<int, std::array<double, 12>> occXf;
    for (auto& kv : E) {
        if (type(kv.second) != "CONTEXT_DEPENDENT_SHAPE_REPRESENTATION") continue;
        auto r = refs(kv.second);
        if (r.size() < 2 || !pdsToNauo.count(r[1])) continue;
        auto rr = refs(E[r[0]]);
        int idt = -1;
        for (int x : rr) if (type(E[x]) == "ITEM_DEFINED_TRANSFORMATION") idt = x;
        if (idt < 0) continue;
        auto ax = refs(E[idt]);
        if (ax.size() < 2) continue;
        double R1[9], T1[3], R2[9], T2[3];
        axis(ax[0], R1, T1);
        axis(ax[1], R2, T2);
        std::array<double, 12> X;
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b) {
                double s = 0;
                for (int k = 0; k < 3; ++k) s += R2[a * 3 + k] * R1[b * 3 + k];  // R2 * R1^T
                X[a * 3 + b] = s;
            }
        for (int a = 0; a < 3; ++a) {
            double s = 0;
            for (int k = 0; k < 3; ++k) s += X[a * 3 + k] * T1[k];
            X[9 + a] = T2[a] - s;
        }
        occXf[pdsToNauo[r[1]]] = X;
    }
    // roots: product definitions never used as a child
    std::set<int> children;
    for (auto& kv : nauo) children.insert(kv.second.child);
    std::set<int> parents;
    for (auto& kv : nauo) parents.insert(kv.second.parent);
    std::function<void(int, int, const double*, const std::string&, int)> build = [&](int pd, int parentNode, const double* W, const std::string& path, int occ) {
        StepNode n;
        n.product = pdName(pd);
        n.occurrence = occ >= 0 ? nauo[occ].name : "";
        n.name = n.product;
        std::string p = path + "/" + n.product + "|" + n.occurrence;
        n.id = fnv1a64Hex(p);
        std::memcpy(n.transform, W, sizeof(double) * 12);
        n.parent = parentNode;
        n.material = guessMaterial(n.product);
        int idx = int(A.nodes.size());
        A.nodes.push_back(n);
        if (parentNode >= 0) A.nodes[parentNode].children.push_back(idx);
        if (A.nodes.size() > 200000) return;
        for (auto& kv : nauo) {
            if (kv.second.parent != pd) continue;
            double L[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
            if (occXf.count(kv.first)) std::memcpy(L, occXf[kv.first].data(), sizeof(L));
            double C[12];
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b) { double s = 0; for (int k = 0; k < 3; ++k) s += W[a * 3 + k] * L[k * 3 + b]; C[a * 3 + b] = s; }
            for (int a = 0; a < 3; ++a) { double s = W[9 + a]; for (int k = 0; k < 3; ++k) s += W[a * 3 + k] * L[9 + k]; C[9 + a] = s; }
            build(kv.second.child, idx, C, p, kv.first);
        }
    };
    double I[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    for (int pd : parents) if (!children.count(pd)) build(pd, -1, I, "", -1);
    if (A.nodes.empty()) for (auto& kv : pdToPdf) build(kv.first, -1, I, "", -1);
    return A;
}

Json StepAssembly::toJson() const {
    Json j = Json::object();
    j.set("file", file); j.set("unit", unit); j.set("originatingSystem", originatingSystem);
    j.set("nodes", double(nodes.size()));
    j.set("geometryNote", "assembly structure and placements only; B-rep tessellation requires a CAD kernel (export STL/OBJ/glTF for geometry)");
    Json a = Json::array();
    for (size_t i = 0; i < nodes.size() && i < 5000; ++i) {
        const StepNode& n = nodes[i];
        Json o = Json::object();
        o.set("id", n.id); o.set("name", n.name); o.set("occurrence", n.occurrence); o.set("parent", n.parent);
        Json t = Json::array(); t.push(n.transform[9]); t.push(n.transform[10]); t.push(n.transform[11]);
        o.set("position_m", t);
        Json m = Json::object();
        m.set("material", n.material.material); m.set("density", n.material.density); m.set("densityRange", Json::array());
        m.set("confidence", n.material.confidence); m.set("basis", n.material.basis);
        o.set("materialGuess", m);
        a.push(o);
    }
    j.set("components", a);
    return j;
}

Json matchAnnotations(const Json& ann, const StepAssembly& re) {
    Json out = Json::object();
    Json matched = Json::array(), orphaned = Json::array();
    std::map<std::string, int> byId, byName;
    for (size_t i = 0; i < re.nodes.size(); ++i) { byId[re.nodes[i].id] = int(i); byName[re.nodes[i].product + "|" + re.nodes[i].occurrence] = int(i); }
    for (auto& kv : ann.members()) {
        Json o = Json::object();
        o.set("oldId", kv.first);
        o.set("text", kv.second["text"]);
        if (byId.count(kv.first)) { o.set("newId", kv.first); o.set("match", "exact id"); matched.push(o); continue; }
        std::string key = kv.second["name"].str("") + "|" + kv.second["occurrence"].str("");
        if (byName.count(key)) { o.set("newId", re.nodes[byName[key]].id); o.set("match", "name+occurrence (assembly path changed)"); matched.push(o); continue; }
        orphaned.push(o);
    }
    out.set("matched", matched);
    out.set("needsUserConfirmation", orphaned);
    return out;
}

}  // namespace gp
