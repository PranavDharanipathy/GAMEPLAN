#!/usr/bin/env python3
"""Tessellate the official field STEP into GLB assets (needs `pip install gmsh`).

    python cad_to_glb.py field.step OUTDIR [--groups pollen,nectar,...]

One GLB per assembly group, one node per CAD solid (node name = CAD product path), vertex
positions in metres in the GamePlan field frame (CAD mm, mapped gp = (x, -z, y) / 1000).
Also writes OUTDIR/cad_assets.json: per group triangle counts, per element mass properties
(volume, surface area, bounding box) measured on the B-rep, and the source file sha256.
The exact B-rep is used; nothing is re-modelled by hand.
"""
import hashlib, json, math, struct, sys, time
import gmsh

GROUPS = {   # group -> (substring of the CAD path, max element size mm, curvature points per 2pi)
    "pollen":      ("am-5851: Pollen", 1.2, 36),
    "nectar_red":  ("am-5852: Red Nectar", 1.4, 36),
    "nectar_blue": ("am-5852: Blue Nectar", 1.4, 36),
    "hive_red":    ("am-5853-Red Hive", 12.0, 16),
    "hive_blue":   ("am-5853-Blue Hive", 12.0, 16),
    "frame":       ("am-5854: Frame", 15.0, 16),
    "flowers":     ("am-5855: Flower Assembly", 8.0, 20),
    "tiles":       ("am-2499:", 40.0, 8),
    "perimeter":   ("am-0481b FTC Perimeter", 25.0, 10),
    "tape":        ("Gaffer Tape", 40.0, 4),
    "trays":       ("am-5706 Artifact Tray", 10.0, 12),
}

def to_gp(x, y, z):
    return (x / 1000.0, -z / 1000.0, y / 1000.0)

def write_glb(path, nodes):
    """nodes: list of (name, positions[float], indices[int]) -> minimal glTF 2.0 binary."""
    bin_parts, views, accessors, meshes, gnodes = [], [], [], [], []
    off = 0
    def add_view(data, target):
        nonlocal off
        pad = (4 - len(data) % 4) % 4
        views.append({"buffer": 0, "byteOffset": off, "byteLength": len(data), "target": target})
        bin_parts.append(data + b"\0" * pad)
        off += len(data) + pad
        return len(views) - 1
    for name, pos, idx in nodes:
        if not idx:
            continue
        n = len(pos) // 3
        mins = [min(pos[i::3]) for i in range(3)]
        maxs = [max(pos[i::3]) for i in range(3)]
        pv = add_view(struct.pack("<%df" % len(pos), *pos), 34962)
        accessors.append({"bufferView": pv, "componentType": 5126, "count": n, "type": "VEC3", "min": mins, "max": maxs})
        pa = len(accessors) - 1
        iv = add_view(struct.pack("<%dI" % len(idx), *idx), 34963)
        accessors.append({"bufferView": iv, "componentType": 5125, "count": len(idx), "type": "SCALAR"})
        ia = len(accessors) - 1
        meshes.append({"name": name, "primitives": [{"attributes": {"POSITION": pa}, "indices": ia}]})
        gnodes.append({"name": name, "mesh": len(meshes) - 1})
    binary = b"".join(bin_parts)
    doc = {"asset": {"version": "2.0", "generator": "GamePlan cad_to_glb (gmsh/OpenCASCADE)"},
           "scene": 0, "scenes": [{"nodes": list(range(len(gnodes)))}], "nodes": gnodes, "meshes": meshes,
           "accessors": accessors, "bufferViews": views, "buffers": [{"byteLength": len(binary)}]}
    js = json.dumps(doc, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binary)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A)); f.write(js)
        f.write(struct.pack("<II", len(binary), 0x004E4942)); f.write(binary)

def main():
    step, outdir = sys.argv[1], sys.argv[2]
    only = sys.argv[sys.argv.index("--groups") + 1].split(",") if "--groups" in sys.argv else list(GROUPS)
    t0 = time.time()
    gmsh.initialize()
    gmsh.option.setNumber("General.Terminal", 0)
    gmsh.option.setNumber("Geometry.OCCImportLabels", 1)
    gmsh.model.occ.importShapes(step, highestDimOnly=True)
    gmsh.model.occ.synchronize()
    vols = [(t, gmsh.model.getEntityName(3, t)) for _, t in gmsh.model.getEntities(3)]
    print("imported %d solids in %.0f s" % (len(vols), time.time() - t0), flush=True)
    report = {"source": step, "sha256": hashlib.sha256(open(step, "rb").read()).hexdigest(), "groups": {}, "elements": {}}
    gmsh.option.setNumber("Mesh.MeshOnlyVisible", 1)
    gmsh.option.setNumber("Mesh.Algorithm", 6)
    for g in only:
        key, hmax, curv = GROUPS[g]
        tags = [t for t, n in vols if key in n]
        if g in ("pollen", "nectar_red", "nectar_blue"):
            tags = tags[:1]          # one instance is enough: all are the same part
            t = tags[0]
            bb = gmsh.model.getBoundingBox(3, t)
            surf = gmsh.model.getBoundary([(3, t)], oriented=False)
            area = sum(gmsh.model.occ.getMass(2, s) for _, s in surf)
            report["elements"][g] = {"cadTag": t, "volume_mm3": gmsh.model.occ.getMass(3, t), "area_mm2": area,
                                     "faces": len(surf), "bbox_mm": [bb[3] - bb[0], bb[4] - bb[1], bb[5] - bb[2]],
                                     "centre_mm": [(bb[0] + bb[3]) / 2, (bb[1] + bb[4]) / 2, (bb[2] + bb[5]) / 2]}
        gmsh.model.setVisibility(gmsh.model.getEntities(), 0, recursive=True)
        gmsh.model.setVisibility([(3, t) for t in tags], 1, recursive=True)
        gmsh.option.setNumber("Mesh.MeshSizeMax", hmax)
        gmsh.option.setNumber("Mesh.MeshSizeMin", hmax / 20)
        gmsh.option.setNumber("Mesh.MeshSizeFromCurvature", curv)
        try:
            gmsh.model.mesh.generate(2)
        except Exception as e:
            print("  %s: mesher reported %s (continuing with what was meshed)" % (g, e), flush=True)
        nodes, tris = [], 0
        for t in tags:
            name = dict(vols)[t]
            pos, idx, remap = [], [], {}
            for _, s in gmsh.model.getBoundary([(3, t)], oriented=False):
                etypes, _, enodes = gmsh.model.mesh.getElements(2, s)
                ntags, coords, _ = gmsh.model.mesh.getNodes(2, s, includeBoundary=True)
                cmap = {int(nt): (coords[3 * i], coords[3 * i + 1], coords[3 * i + 2]) for i, nt in enumerate(ntags)}
                for et, en in zip(etypes, enodes):
                    if et != 2:
                        continue
                    for nt in en:
                        nt = int(nt)
                        if nt not in remap:
                            remap[nt] = len(pos) // 3
                            pos.extend(to_gp(*cmap[nt]))
                        idx.append(remap[nt])
            if g in ("pollen", "nectar_red", "nectar_blue") and pos:   # element assets centred on the ball
                c = [sum(pos[i::3]) / (len(pos) // 3) for i in range(3)]
                bbm = [(min(pos[i::3]) + max(pos[i::3])) / 2 for i in range(3)]
                pos = [p - bbm[i % 3] for i, p in enumerate(pos)]
            tris += len(idx) // 3
            nodes.append((name, pos, idx))
        write_glb("%s/%s.glb" % (outdir, g), nodes)
        report["groups"][g] = {"solids": len(tags), "triangles": tris}
        print("  %s: %d solids, %d triangles (%.0f s)" % (g, len(tags), tris, time.time() - t0), flush=True)
        gmsh.model.mesh.clear()
        json.dump(report, open("%s/cad_assets.json" % outdir, "w"), indent=1)
    gmsh.finalize()

if __name__ == "__main__":
    main()
