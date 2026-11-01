// CAD import foundation for the digital twin.
//  - STEP (AP203/214/242) assembly structure: products, instance hierarchy, placements, units.
//    B-rep geometry tessellation is NOT done here (needs a kernel such as OpenCascade; see
//    DEEP_ANALYSIS.md). Mesh formats carry the geometry.
//  - STL (ascii/binary) and OBJ meshes: exact polyhedral mass properties (volume, centre of mass,
//    inertia tensor) by the divergence theorem, with a closed-mesh check that drives confidence.
//  - Material inference from part names with explicit confidence and density range.
//  - Annotations bound to stable component ids that survive re-import.
#pragma once
#include <array>
#include <string>
#include <vector>

#include "gameplan/json.hpp"
#include "gameplan/math.hpp"

namespace gp {

struct Mesh {
    std::vector<Vec3> v;
    std::vector<std::array<int, 3>> f;
    std::string source;
};

struct MeshProps {
    double volume = 0, mass = 0, density = 0;
    Vec3 com, bboxMin, bboxMax;
    double inertia[3][3] = {{0}};   // about COM (kg m^2)
    bool closed = false;
    int openEdges = 0;
    std::string confidence;
    Json toJson() const;
};

Mesh loadMesh(const std::string& path, double unitScale = 0);   // unitScale 0 => auto (mm if extent > 5)
MeshProps meshMassProperties(const Mesh& m, double density);

struct MaterialGuess {
    std::string material;
    double density = 1200, densityLo = 900, densityHi = 1500;
    std::string confidence, basis;
};
MaterialGuess guessMaterial(const std::string& partName);

struct StepNode {
    std::string id;          // stable id: hash of the instance path (product names + occurrence names)
    std::string name, product, occurrence;
    double transform[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};  // world rotation (row-major 3x3) + translation (m)
    std::vector<int> children;
    int parent = -1;
    MaterialGuess material;
};
struct StepAssembly {
    std::vector<StepNode> nodes;
    std::string unit = "m";
    double unitScale = 1.0;
    std::string originatingSystem, file;
    Json toJson() const;
};
StepAssembly readStepAssembly(const std::string& path);

// Annotation store: {componentId: {text, overrides:{mass_kg, material, ...}, name, bbox}}
Json matchAnnotations(const Json& annotations, const StepAssembly& reimported);

}  // namespace gp
