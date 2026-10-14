"""
Step 0 of the Map.gltf authoring pipeline (docs/TODO.md): writes a tiny probe glTF whose vertices carry the per-vertex fields
the pipeline needs, so that a round trip through the modelling tool shows which of them survive. Read the result with tools/gltfAttrCheck.py.

    python3 tools/gltfAttrWrite.py [--out DIR/probe.gltf]        default: probe.gltf in the current directory

Writes <out> (JSON), <out stem>.bin and <out stem>.expected.json (the values the checker compares against). No disc data in any of them.

Content: two nodes, ProbeA (5 vertices, 2 triangles; vertices 1 and 4 share a position but have different source vertex indices) and
ProbeB (3 vertices, 1 triangle, node translation set). Per vertex, the same four fields in two encodings:
  custom attributes   _SRC (uint) + _SRCFLAG (uint) + _SRCIDX (uint), _ORIG (vec3 float)
  fallback            TEXCOORD_1 = (SRC, FLAG), TEXCOORD_2 = (ORIG.x, ORIG.y), TEXCOORD_3 = (ORIG.z, SRCIDX), float32
TEXCOORD_0 is a real UV so the fallback sets start at 1. SRC values include 255, 65535, 2^24-1, 2^24, 2^24+1 (the last is expected to be
inexact in float32: a documented bound, not a failure). ORIG values stress float32 exactness (0.1, -123.456789, 1e-7, 12345.678).

For the colleague: run 1 = import the file and re-export it with no edits. Run 2 = move vertex 2 of ProbeA, delete one triangle of
ProbeA, add one new triangle, export; then export again with any "custom / extra attributes" option enabled. Send the files and the
export settings (tool name, version, options).
"""
import argparse
import json
import os
import struct

UINT, FLOAT = 5125, 5126
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963

NODES = [
    {
        "name": "ProbeA",
        "translation": [0.0, 0.0, 0.0],
        "positions": [[0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 1, 0], [1, 0, 0]],
        "normals": [[0, 0, 1]] * 4 + [[0, 1, 0]],
        "uv0": [[0, 0], [1, 0], [0, 1], [1, 1], [0.5, 0.25]],
        "indices": [0, 1, 2, 4, 3, 2],
        "SRC": [255, 65535, 16777215, 16777217, 70000],
        "FLAG": [0, 1, 2, 3, 1],
        "IDX": [0, 1, 2, 3, 4],
        "ORIG": [[0.1, -123.456789, 1e-7], [1, 0, 0], [0, 1, 0], [12345.678, 1, 1], [1, 0, 0]],
    },
    {
        "name": "ProbeB",
        "translation": [10.0, 0.0, -5.0],
        "positions": [[0, 0, 0], [2, 0, 0], [0, 2, 0]],
        "normals": [[0, 0, 1]] * 3,
        "uv0": [[0, 0], [1, 0], [0, 1]],
        "indices": [0, 1, 2],
        "SRC": [1, 2, 16777216],
        "FLAG": [0, 2, 3],
        "IDX": [0, 1, 2],
        "ORIG": [[5, 5, 5], [-0.25, 0.5, 1024.125], [3.14159265, 2.71828183, 1.41421356]],
    },
]


def f32(v):
    return struct.unpack("<f", struct.pack("<f", v))[0]


class Buffer:
    """One binary buffer; every add() is a 4-byte aligned bufferView with its accessor."""

    def __init__(self):
        self.data = bytearray()
        self.views = []
        self.accessors = []

    def add(self, fmt, rows, comp_type, type_name, target, with_bounds=False):
        flat = [x for r in rows for x in (r if isinstance(r, (list, tuple)) else [r])]
        raw = struct.pack("<%d%s" % (len(flat), fmt), *flat)
        while len(self.data) % 4:
            self.data.append(0)
        self.views.append({"buffer": 0, "byteOffset": len(self.data), "byteLength": len(raw), "target": target})
        self.data += raw
        acc = {"bufferView": len(self.views) - 1, "componentType": comp_type, "count": len(rows), "type": type_name}
        if with_bounds:
            cols = list(zip(*rows))
            acc["min"] = [min(c) for c in cols]
            acc["max"] = [max(c) for c in cols]
        self.accessors.append(acc)
        return len(self.accessors) - 1


def build():
    buf = Buffer()
    meshes, nodes, expected = [], [], []
    for n in NODES:
        orig = [[f32(c) for c in o] for o in n["ORIG"]]
        pos = [[f32(c) for c in p] for p in n["positions"]]
        attrs = {
            "POSITION": buf.add("f", pos, FLOAT, "VEC3", ARRAY_BUFFER, True),
            "NORMAL": buf.add("f", n["normals"], FLOAT, "VEC3", ARRAY_BUFFER),
            "TEXCOORD_0": buf.add("f", n["uv0"], FLOAT, "VEC2", ARRAY_BUFFER),
            "TEXCOORD_1": buf.add("f", [[s, f] for s, f in zip(n["SRC"], n["FLAG"])], FLOAT, "VEC2", ARRAY_BUFFER),
            "TEXCOORD_2": buf.add("f", [[o[0], o[1]] for o in orig], FLOAT, "VEC2", ARRAY_BUFFER),
            "TEXCOORD_3": buf.add("f", [[o[2], i] for o, i in zip(orig, n["IDX"])], FLOAT, "VEC2", ARRAY_BUFFER),
            "_SRC": buf.add("I", n["SRC"], UINT, "SCALAR", ARRAY_BUFFER),
            "_SRCFLAG": buf.add("I", n["FLAG"], UINT, "SCALAR", ARRAY_BUFFER),
            "_SRCIDX": buf.add("I", n["IDX"], UINT, "SCALAR", ARRAY_BUFFER),
            "_ORIG": buf.add("f", orig, FLOAT, "VEC3", ARRAY_BUFFER),
        }
        ind = buf.add("H", n["indices"], 5123, "SCALAR", ELEMENT_ARRAY_BUFFER)
        meshes.append({"name": n["name"], "primitives": [{"attributes": attrs, "indices": ind, "mode": 4}]})
        nodes.append({"name": n["name"], "mesh": len(meshes) - 1, "translation": n["translation"]})
        expected.append({
            "name": n["name"], "translation": n["translation"], "positions": pos, "indices": n["indices"],
            "SRC": n["SRC"], "FLAG": n["FLAG"], "IDX": n["IDX"], "ORIG": orig,
        })
    return buf, meshes, nodes, expected


def main():
    ap = argparse.ArgumentParser(description="Writes the glTF custom-attribute round-trip probe.")
    ap.add_argument("--out", default="probe.gltf")
    out = ap.parse_args().out
    stem = os.path.splitext(out)[0]
    buf, meshes, nodes, expected = build()
    bin_name = os.path.basename(stem) + ".bin"
    doc = {
        "asset": {"version": "2.0", "generator": "tools/gltfAttrWrite.py"},
        "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": meshes,
        "accessors": buf.accessors,
        "bufferViews": buf.views,
        "buffers": [{"uri": bin_name, "byteLength": len(buf.data)}],
    }
    with open(out, "w") as f:
        json.dump(doc, f, indent=1)
    with open(stem + ".bin", "wb") as f:
        f.write(buf.data)
    with open(stem + ".expected.json", "w") as f:
        json.dump({"nodes": expected}, f)
    print("wrote %s, %s.bin, %s.expected.json" % (out, stem, stem))


main()
