"""
Step 0 of the Map.gltf authoring pipeline (docs/TODO.md): reads the file a modelling tool re-exported from the probe written by
tools/gltfAttrWrite.py and reports which per-vertex fields (source id, flag, source vertex index, original position) survived and with
which values, for both encodings (custom attributes _SRC / _SRCFLAG / _SRCIDX / _ORIG, and the TEXCOORD_1..3 fallback).

    python3 tools/gltfAttrCheck.py FILE.gltf|FILE.glb [--expected probe.expected.json] [--tool "name version, export options"] [--ai]

--expected defaults to probe.expected.json next to FILE. --tool is free text echoed in the report (the colleague's tool is unknown).
--ai prints the compact form: one line per node and per channel, then verdict= and notes=.

Matching: a node is found by name (exact, case-insensitive, or prefix such as ProbeA.001). With the same vertex count the vertices are
paired by index and those whose POSITION differs are listed as moved; otherwise by rounded POSITION (a moved vertex is then missing).
A POSITION axis conversion ((x,z,-y) or (x,-z,y)) is detected. Each channel is searched by value over every attribute of the file,
whatever its name, so renamed or retyped fields are found. TEXCOORD y is also tested as 1-v. Expected values that float32 cannot
hold (2^24+1) are left out of the fallback verdict and listed. Draco and sparse accessors are not decoded and are reported as such.
Status: EXACT (same name, type), RETYPED (same name, other component type), RENAMED / MOVED (values found under another name /
TEXCOORD slot), PARTIAL n/m, CHANGED (present, values differ), DROPPED.
"""
import argparse
import json
import os
import struct
import sys

CTYPES = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
CNAME = {5120: "i8", 5121: "u8", 5122: "i16", 5123: "u16", 5125: "u32", 5126: "f32"}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT2": 4, "MAT3": 9, "MAT4": 16}
NORM_MAX = {5120: 127, 5121: 255, 5122: 32767, 5123: 65535}
AXES = [("identity", lambda p: p), ("(x,z,-y)", lambda p: (p[0], p[2], -p[1])), ("(x,-z,y)", lambda p: (p[0], -p[2], p[1]))]

CUSTOM = {"SRC": ("_SRC", 0), "FLAG": ("_SRCFLAG", 0), "IDX": ("_SRCIDX", 0),
          "ORIG.x": ("_ORIG", 0), "ORIG.y": ("_ORIG", 1), "ORIG.z": ("_ORIG", 2)}
FALLBACK = {"SRC": ("TEXCOORD_1", 0), "FLAG": ("TEXCOORD_1", 1),
            "ORIG.x": ("TEXCOORD_2", 0), "ORIG.y": ("TEXCOORD_2", 1), "ORIG.z": ("TEXCOORD_3", 0), "IDX": ("TEXCOORD_3", 1)}
ORDER = ["SRC", "FLAG", "IDX", "ORIG.x", "ORIG.y", "ORIG.z"]
INT_CHANNELS = ("SRC", "FLAG", "IDX")


def load(path):
    """(json doc, [buffer bytes]) from a .gltf (uri, data URI) or .glb."""
    raw = open(path, "rb").read()
    if raw[:4] == b"glTF":
        off, doc, glb = 12, None, None
        while off < len(raw):
            ln, kind = struct.unpack_from("<II", raw, off)
            body = raw[off + 8:off + 8 + ln]
            if kind == 0x4E4F534A:
                doc = json.loads(body)
            elif kind == 0x004E4942 and glb is None:
                glb = body
            off += 8 + ln
        bufs = [glb]
        for b in doc.get("buffers", [])[1:]:
            bufs.append(read_uri(b.get("uri"), path))
        return doc, bufs
    doc = json.loads(raw)
    return doc, [read_uri(b.get("uri"), path) for b in doc.get("buffers", [])]


def read_uri(uri, path):
    if uri is None:
        return None
    if uri.startswith("data:"):
        import base64
        return base64.b64decode(uri.split(",", 1)[1])
    from urllib.parse import unquote
    p = os.path.join(os.path.dirname(os.path.abspath(path)), unquote(uri))
    return open(p, "rb").read() if os.path.exists(p) else None


def read_accessor(doc, bufs, idx):
    """([tuple per element], (componentType, type, normalized)) or (None, reason)."""
    a = doc["accessors"][idx]
    meta = (a["componentType"], a["type"], bool(a.get("normalized")))
    if "sparse" in a:
        return None, "sparse accessor"
    if "bufferView" not in a:
        return [tuple([0.0] * NCOMP[a["type"]])] * a["count"], meta
    bv = doc["bufferViews"][a["bufferView"]]
    buf = bufs[bv["buffer"]]
    if buf is None:
        return None, "buffer missing"
    fmt, size = CTYPES[a["componentType"]]
    n = NCOMP[a["type"]]
    stride = bv.get("byteStride") or size * n
    base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    out = []
    for i in range(a["count"]):
        v = struct.unpack_from("<%d%s" % (n, fmt), buf, base + i * stride)
        if meta[2] and a["componentType"] in NORM_MAX:
            m = NORM_MAX[a["componentType"]]
            v = tuple(max(x / m, -1.0) for x in v)
        out.append(tuple(v))
    return out, meta


def collect(doc, bufs, problems):
    """Nodes that carry a mesh: [{name, translation, prims, verts: [{attr: tuple}]}], and {attr: (ctype, type, normalized)}."""
    nodes, meta = [], {}
    for ni, node in enumerate(doc.get("nodes", [])):
        if "mesh" not in node:
            continue
        mesh = doc["meshes"][node["mesh"]]
        entry = {"name": node.get("name") or mesh.get("name") or "node%d" % ni,
                 "translation": node.get("translation"), "prims": len(mesh["primitives"]), "verts": []}
        for p in mesh["primitives"]:
            if "KHR_draco_mesh_compression" in p.get("extensions", {}):
                problems.append("%s: Draco primitive not decoded" % entry["name"])
                continue
            cols = {}
            for name, ai in p["attributes"].items():
                vals, m = read_accessor(doc, bufs, ai)
                if vals is None:
                    problems.append("%s.%s: %s not decoded" % (entry["name"], name, m))
                    continue
                cols[name] = vals
                meta.setdefault(name, m)
            n = len(cols.get("POSITION", []))
            for i in range(n):
                entry["verts"].append({k: v[i] for k, v in cols.items()})
        nodes.append(entry)
    return nodes, meta


def find_node(nodes, name):
    for test in (lambda n: n == name, lambda n: n.lower() == name.lower(), lambda n: n.lower().startswith(name.lower())):
        for i, n in enumerate(nodes):
            if test(n["name"]):
                return i
    return None


def near(a, b):
    return all(abs(x - y) <= 1e-5 for x, y in zip(a, b))


def pair_vertices(exp_nodes, out_nodes):
    """pairs [(exp node, exp vertex, out vertex dict, moved)], per-node report, and the out vertices nobody claimed."""
    claimed, report, pairs = set(), [], []
    stray = []
    for ei, e in enumerate(exp_nodes):
        oi = find_node(out_nodes, e["name"])
        r = {"name": e["name"], "found": None, "n_exp": len(e["positions"]), "n_out": 0, "mode": "none", "moved": [],
             "missing": 0, "axis": "identity"}
        report.append(r)
        if oi is None:
            stray.append(ei)
            continue
        claimed.add(oi)
        o = out_nodes[oi]
        r["found"], r["n_out"] = o["name"], len(o["verts"])
        if r["n_out"] == r["n_exp"]:
            best = max(AXES, key=lambda ax: sum(near(ax[1](v["POSITION"]), p) for v, p in zip(o["verts"], e["positions"])))
            r["mode"], r["axis"] = "index", best[0]
            for vi, (v, p) in enumerate(zip(o["verts"], e["positions"])):
                moved = not near(best[1](v["POSITION"]), p)
                if moved:
                    r["moved"].append(vi)
                pairs.append((ei, vi, v, moved))
        else:
            stray.append(ei)
            claimed.discard(oi)
    pool = [v for oi, o in enumerate(out_nodes) if oi not in claimed for v in o["verts"]]
    used = set()
    for ei in stray:
        e, r = exp_nodes[ei], report[ei]
        r["mode"] = "position"
        if not pool:
            r["missing"] = r["n_exp"]
            continue
        best = max(AXES, key=lambda ax: sum(any(near(ax[1](v["POSITION"]), p) for v in pool) for p in e["positions"]))
        r["axis"] = best[0]
        for vi, p in enumerate(e["positions"]):
            hit = next((k for k, v in enumerate(pool) if k not in used and near(best[1](v["POSITION"]), p)), None)
            if hit is None:
                r["missing"] += 1
            else:
                used.add(hit)
                pairs.append((ei, vi, pool[hit], False))
        r["n_out"] = r["n_exp"] - r["missing"]
    paired_ids = {id(p[2]) for p in pairs}
    extras = [v for o in out_nodes for v in o["verts"] if id(v) not in paired_ids]
    return pairs, report, extras


def f32(v):
    return struct.unpack("<f", struct.pack("<f", v))[0]


def expected_values(exp_nodes, pairs, chan):
    """Expected value of channel chan for every pair."""
    out = []
    for ei, vi, _, _ in pairs:
        e = exp_nodes[ei]
        if chan in INT_CHANNELS:
            out.append(e[chan][vi])
        else:
            out.append(e["ORIG"][vi]["xyz".index(chan[-1])])
    return out


def same(a, b, is_int):
    if a is None:
        return False
    return a == b if is_int else abs(a - b) <= 1e-6 * max(1.0, abs(b))


def evaluate(group, table, exp_nodes, pairs, meta):
    """Per channel: status line fields for one encoding group ('custom' or 'fallback')."""
    results = {}
    names = set()
    for _, _, v, _ in pairs:
        names.update(v)
    want_tex = group == "fallback"
    cols = sorted((n, c) for n in names if n.startswith("TEXCOORD_") == want_tex and n != "POSITION" and n != "NORMAL"
                  and n != "TEXCOORD_0" for c in range(len(next(v[n] for _, _, v, _ in pairs if n in v))))
    for chan in ORDER:
        is_int = chan in INT_CHANNELS
        exp = expected_values(exp_nodes, pairs, chan)
        target = table[chan]
        if want_tex and is_int:
            keep = [i for i, e in enumerate(exp) if f32(e) == e]
            excluded = [(pairs[i][0], pairs[i][1], exp[i]) for i in range(len(exp)) if i not in keep]
        else:
            keep, excluded = list(range(len(exp))), []
        want = [f32(e) if want_tex and is_int else e for e in exp]
        best = None
        for col in cols:
            for flip in ((False, True) if want_tex and col[1] == 1 else (False,)):
                vals = []
                for _, _, v, _ in pairs:
                    t = v.get(col[0])
                    x = t[col[1]] if t is not None and col[1] < len(t) else None
                    vals.append(1.0 - x if flip and x is not None else x)
                ok = sum(same(vals[i], want[i], is_int) for i in keep)
                score = (ok, col == target and not flip)
                if best is None or score > best[0]:
                    best = (score, col, flip, vals)
        total = len(keep)
        r = {"chan": chan, "target": target, "total": total, "excluded": excluded, "col": None, "flip": False,
             "moved_ok": None, "moved_n": 0, "ok": 0}
        if best is not None and total:
            (ok, _), col, flip, vals = best
            r.update(ok=ok, col=col, flip=flip)
            ctype = meta.get(col[0], (None,))[0]
            want_type = 5125 if (is_int and not want_tex) else 5126
            mv = [i for i in keep if pairs[i][3]]
            r["moved_n"] = len(mv)
            r["moved_ok"] = sum(same(vals[i], want[i], is_int) for i in mv)
            if ok == total:
                if col == target:
                    r["status"] = "EXACT" if ctype == want_type else "RETYPED"
                else:
                    r["status"] = "MOVED" if want_tex else "RENAMED"
                r["ctype"] = ctype
            elif ok > 0:
                r["status"] = "PARTIAL"
            else:
                r["status"] = "CHANGED" if target[0] in names else "DROPPED"
                if r["status"] == "CHANGED":
                    tv = [v.get(target[0]) for _, _, v, _ in pairs]
                    err = [abs(t[target[1]] - w) for t, w in zip(tv, want) if t is not None and target[1] < len(t)]
                    r["max_err"] = max(err) if err else None
        else:
            r["status"] = "DROPPED"
        results[chan] = r
    return results


def axis_note(exp_nodes, pairs, meta):
    """Does a custom vec3 attribute equal the expected ORIG under a POSITION axis conversion?"""
    notes = []
    for name, m in meta.items():
        if m[1] != "VEC3" or name in ("POSITION", "NORMAL"):
            continue
        for label, fn in AXES[1:]:
            ok = sum(1 for ei, vi, v, _ in pairs if name in v and near(fn(v[name]), exp_nodes[ei]["ORIG"][vi]))
            if ok == len(pairs) and ok:
                notes.append("%s equals ORIG converted by %s (axis conversion applied to a custom attribute)" % (name, label))
    return notes


def describe(r, meta):
    col = r["col"]
    if col is None:
        return ""
    s = "%s.%d" % col
    if r.get("ctype"):
        s += " " + CNAME.get(r["ctype"], "?")
    if r["flip"]:
        s += " v-flipped(1-v)"
    return s


def main():
    ap = argparse.ArgumentParser(description="Reports which probe fields survived a round trip.")
    ap.add_argument("file")
    ap.add_argument("--expected")
    ap.add_argument("--tool", default="unknown")
    ap.add_argument("--ai", action="store_true")
    a = ap.parse_args()
    exp_path = a.expected or os.path.join(os.path.dirname(os.path.abspath(a.file)), "probe.expected.json")
    exp_nodes = json.load(open(exp_path))["nodes"]
    doc, bufs = load(a.file)
    problems = []
    out_nodes, meta = collect(doc, bufs, problems)
    pairs, report, extras = pair_vertices(exp_nodes, out_nodes)
    if not pairs:
        print("no vertex could be paired with the probe (nodes in file: %s)" % [n["name"] for n in out_nodes])
        sys.exit(1)

    groups = {"custom": evaluate("custom", CUSTOM, exp_nodes, pairs, meta),
              "fallback": evaluate("fallback", FALLBACK, exp_nodes, pairs, meta)}
    notes = list(problems) + axis_note(exp_nodes, pairs, meta)
    for r in report:
        if r["axis"] != "identity":
            notes.append("%s: POSITION converted by %s" % (r["name"], r["axis"]))
        if r["found"] is None and r["mode"] == "position":
            notes.append("%s: node not found by name, vertices matched by position" % r["name"])
    for e in exp_nodes:
        i = find_node(out_nodes, e["name"])
        if i is not None and out_nodes[i]["translation"] != e["translation"] and not (
                out_nodes[i]["translation"] is None and e["translation"] == [0.0, 0.0, 0.0]):
            notes.append("%s: translation %s -> %s" % (e["name"], e["translation"], out_nodes[i]["translation"]))
    if len(out_nodes) != len(exp_nodes) or any(n["prims"] != 1 for n in out_nodes):
        notes.append("nodes %d (expected %d), primitives per node %s" % (len(out_nodes), len(exp_nodes), [n["prims"] for n in out_nodes]))
    for g, res in groups.items():
        for chan, r in res.items():
            for ei, vi, val in r["excluded"]:
                notes.append("fallback %s of %s vertex %d (%d) not exact in float32, left out of the verdict" % (chan, exp_nodes[ei]["name"], vi, val))
    if extras:
        has_custom = sum(any(k in v for k in ("_SRC", "_SRCFLAG", "_SRCIDX", "_ORIG")) for v in extras)
        has_tex = sum(any(k in v for k in ("TEXCOORD_1", "TEXCOORD_2", "TEXCOORD_3")) for v in extras)
        notes.append("%d vertices not in the probe (new geometry): %d carry custom attributes, %d carry TEXCOORD_1..3"
                     % (len(extras), has_custom, has_tex))
    sets = sorted({k for n in out_nodes for v in n["verts"] for k in v if k.startswith("TEXCOORD_")})
    ext = doc.get("extensionsUsed", [])

    def full(res):
        return all(r["status"] in ("EXACT", "RETYPED", "RENAMED", "MOVED") for r in res.values())

    def moved_state(res):
        mv = [r for r in res.values() if r["moved_n"]]
        if not mv:
            return "n/a"
        return "kept" if all(r["moved_ok"] == r["moved_n"] for r in mv) else "lost"

    verdict = "NONE"
    for g, label in (("custom", "PRIMARY=custom"), ("fallback", "FALLBACK=texcoord")):
        res = groups[g]
        if full(res) and moved_state(res) != "lost":
            renamed = any(r["status"] in ("RENAMED", "MOVED") for r in res.values())
            verdict = label + (" (names moved: see channels)" if renamed else "")
            break
    moved_all = {g: moved_state(r) for g, r in groups.items()}
    if verdict == "NONE" and any(full(r) for r in groups.values()):
        notes.append("fields survive but the moved vertex lost its original position")

    if a.ai:
        print("file=%s tool=%s ext=%s texcoord_sets=%s" % (os.path.basename(a.file), a.tool, ",".join(ext) or "-", ",".join(sets) or "-"))
        for r in report:
            print("node %s found=%s verts=%d/%d mode=%s moved=%s missing=%d" % (
                r["name"], r["found"], r["n_out"], r["n_exp"], r["mode"], r["moved"], r["missing"]))
        for g, res in groups.items():
            for chan in ORDER:
                r = res[chan]
                extra = describe(r, meta)
                if r["status"] == "PARTIAL":
                    extra += " n=%d/%d" % (r["ok"], r["total"])
                if r["status"] == "CHANGED" and r.get("max_err") is not None:
                    extra += "max_err=%.3g" % r["max_err"]
                print("%s %s=%s %s" % (g, chan, r["status"], extra.strip()))
            print("%s moved_vertex_orig=%s" % (g, moved_all[g]))
        print("verdict=%s" % verdict)
        print("notes=%s" % ("; ".join(notes) or "-"))
        return

    print("Round-trip report: %s" % a.file)
    print("tool: %s" % a.tool)
    print("extensionsUsed: %s    TEXCOORD sets in file: %s" % (", ".join(ext) or "none", ", ".join(sets) or "none"))
    print("\nNodes")
    for r in report:
        print("  %-8s found as %-12s vertices %d/%d  paired by %-8s moved %s  missing %d" % (
            r["name"], r["found"], r["n_out"], r["n_exp"], r["mode"], r["moved"] or "none", r["missing"]))
    for g, title in (("custom", "Custom attributes (_SRC, _SRCFLAG, _SRCIDX, _ORIG)"), ("fallback", "Fallback (TEXCOORD_1..3, float32)")):
        print("\n%s" % title)
        for chan in ORDER:
            r = groups[g][chan]
            tail = describe(r, meta)
            if r["status"] == "PARTIAL":
                tail += "  %d of %d vertices" % (r["ok"], r["total"])
            if r["status"] == "CHANGED" and r.get("max_err") is not None:
                tail += "  max error %.3g" % r["max_err"]
            print("  %-7s %-8s expected in %s.%d   %s" % (chan, r["status"], r["target"][0], r["target"][1], tail.strip()))
        print("  moved vertex kept its original position: %s" % moved_all[g])
    print("\nVerdict: %s" % verdict)
    if notes:
        print("\nNotes")
        for n in notes:
            print("  - %s" % n)


main()
