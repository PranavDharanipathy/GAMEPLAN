#!/usr/bin/env python3
"""Reduce a CAD GLB (from cad_to_glb.py) to a triangle budget for real-time rendering.

    python decimate_glb.py in.glb out.glb --budget 120000 [--crease 35] [--drop-below-mm 4]

Quadric-error decimation per CAD solid (budget shared in proportion to each solid's triangles),
then crease-aware normals: faces meeting at more than --crease degrees get separate vertices so
machined edges stay sharp while curved surfaces shade smoothly. Needs numpy + fast-simplification.
Node names (CAD product paths) are kept so viewers can assign materials by part.
"""
import argparse, json, struct
import numpy as np
import fast_simplification


def read_glb(path):
    b = open(path, "rb").read()
    jl = struct.unpack("<I", b[12:16])[0]
    doc = json.loads(b[20:20 + jl])
    binary = b[20 + jl + 8:]
    nodes = []
    for node in doc["nodes"]:
        prim = doc["meshes"][node["mesh"]]["primitives"][0]
        pa, ia = doc["accessors"][prim["attributes"]["POSITION"]], doc["accessors"][prim["indices"]]
        pv, iv = doc["bufferViews"][pa["bufferView"]], doc["bufferViews"][ia["bufferView"]]
        P = np.frombuffer(binary, np.float32, pa["count"] * 3, pv["byteOffset"]).reshape(-1, 3).astype(np.float64)
        I = np.frombuffer(binary, np.uint32, ia["count"], iv["byteOffset"]).reshape(-1, 3).astype(np.int64)
        nodes.append((node["name"], P, I))
    return nodes


def weld(P, I, tol=1e-6):
    q = np.round(P / tol).astype(np.int64)
    _, first, inv = np.unique(q, axis=0, return_index=True, return_inverse=True)
    I2 = inv.reshape(-1)[I]
    keep = (I2[:, 0] != I2[:, 1]) & (I2[:, 1] != I2[:, 2]) & (I2[:, 0] != I2[:, 2])
    return P[first], I2[keep]


def crease_normals(P, I, crease_deg):
    """Split vertices across sharp edges; returns (positions, normals, indices) with flat arrays."""
    fn = np.cross(P[I[:, 1]] - P[I[:, 0]], P[I[:, 2]] - P[I[:, 0]])
    area = np.linalg.norm(fn, axis=1, keepdims=True)
    fn = fn / np.maximum(area, 1e-20)
    cosT = np.cos(np.radians(crease_deg))
    nV = len(P)
    # per corner: smooth normal = area-weighted sum of incident face normals within the crease angle
    corners = I.reshape(-1)
    faces = np.repeat(np.arange(len(I)), 3)
    order = np.argsort(corners, kind="stable")
    cs, fs = corners[order], faces[order]
    starts = np.searchsorted(cs, np.arange(nV + 1))
    outP, outN, outI = [], [], np.empty_like(I)
    key2idx = {}
    for v in range(nV):
        inc = fs[starts[v]:starts[v + 1]]
        if len(inc) == 0:
            continue
        n_inc = fn[inc]
        w_inc = area[inc, 0]
        # group incident faces: faces whose normals are within the crease angle of each other's seed
        assigned = np.full(len(inc), -1)
        groups = []
        for k in range(len(inc)):
            if assigned[k] >= 0:
                continue
            sel = (n_inc @ n_inc[k] >= cosT) & (assigned < 0)
            assigned[sel] = len(groups)
            groups.append(sel)
        for g, sel in enumerate(groups):
            n = (n_inc[sel] * w_inc[sel, None]).sum(0)
            n /= max(np.linalg.norm(n), 1e-20)
            idx = len(outP)
            outP.append(P[v]); outN.append(n)
            for f in inc[sel]:
                key2idx[(v, f)] = idx
    for f in range(len(I)):
        for c in range(3):
            outI[f, c] = key2idx[(I[f, c], f)]
    return np.array(outP, np.float32), np.array(outN, np.float32), outI.astype(np.uint32)


def write_glb(path, nodes):
    parts, views, accessors, meshes, gnodes = [], [], [], [], []
    off = 0

    def add(data, target):
        nonlocal off
        raw = data.tobytes()
        pad = (4 - len(raw) % 4) % 4
        views.append({"buffer": 0, "byteOffset": off, "byteLength": len(raw), "target": target})
        parts.append(raw + b"\0" * pad)
        off += len(raw) + pad
        return len(views) - 1

    for name, P, N, I in nodes:
        if len(I) == 0:
            continue
        a_p = len(accessors)
        accessors.append({"bufferView": add(P, 34962), "componentType": 5126, "count": len(P), "type": "VEC3",
                          "min": P.min(0).tolist(), "max": P.max(0).tolist()})
        accessors.append({"bufferView": add(N, 34962), "componentType": 5126, "count": len(N), "type": "VEC3"})
        accessors.append({"bufferView": add(I.reshape(-1), 34963), "componentType": 5125, "count": int(I.size), "type": "SCALAR"})
        meshes.append({"name": name, "primitives": [{"attributes": {"POSITION": a_p, "NORMAL": a_p + 1}, "indices": a_p + 2}]})
        gnodes.append({"name": name, "mesh": len(meshes) - 1})
    binary = b"".join(parts)
    doc = {"asset": {"version": "2.0", "generator": "GamePlan decimate_glb"}, "scene": 0,
           "scenes": [{"nodes": list(range(len(gnodes)))}], "nodes": gnodes, "meshes": meshes,
           "accessors": accessors, "bufferViews": views, "buffers": [{"byteLength": len(binary)}]}
    js = json.dumps(doc, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 28 + len(js) + len(binary)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A)); f.write(js)
        f.write(struct.pack("<II", len(binary), 0x004E4942)); f.write(binary)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src"); ap.add_argument("dst")
    ap.add_argument("--budget", type=int, required=True)
    ap.add_argument("--crease", type=float, default=35.0)
    ap.add_argument("--drop-below-mm", type=float, default=0.0, help="omit solids smaller than this (rivets, pins)")
    a = ap.parse_args()
    nodes = read_glb(a.src)
    kept = []
    for name, P, I in nodes:
        if a.drop_below_mm > 0 and np.linalg.norm(P.max(0) - P.min(0)) * 1000 < a.drop_below_mm:
            continue
        kept.append((name, *weld(P, I)))
    total = sum(len(I) for _, _, I in kept)
    out, tris = [], 0
    for name, P, I in kept:
        target = max(8, int(len(I) * min(1.0, a.budget / max(total, 1))))
        if target < len(I) * 0.95 and len(I) > 16:
            P2, I2 = fast_simplification.simplify(P.astype(np.float32), I.astype(np.int32), 1.0 - target / len(I))
            P, I = weld(P2.astype(np.float64), I2.astype(np.int64))
        if len(I) == 0:
            continue
        Pc, Nc, Ic = crease_normals(P, I, a.crease)
        out.append((name, Pc, Nc, Ic))
        tris += len(Ic)
    write_glb(a.dst, out)
    print("%s: %d solids, %d -> %d triangles" % (a.dst, len(out), total, tris))


if __name__ == "__main__":
    main()
