"""
Generates ResourceMap.txt in the project root: models, textures, audio, animations and collision on the
two GameCube disc images (Sonic Adventure DX.iso, Shadow The Hedgehog.iso), with what each is and where it lives.

Run from anywhere:  python3 tools/genResourceMap.py
Reads only the disc file tables and the .ONE archive headers, never whole images.
SADX .rel contents come from SA Tools' GameConfig/GC_SADX INIs, downloaded from GitHub on first run
and cached in tools/.cache/gc_sadx/ (delete that folder to refresh).
"""
import collections, datetime, glob, json, os, re, struct, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/"
INI_CACHE = os.path.join(ROOT, "tools", ".cache", "gc_sadx")
INI_LISTING = "https://api.github.com/repos/X-Hax/sa_tools/contents/GameConfig/GC_SADX"


def listing(path):
    """GameCube disc file table (big-endian FST at 0x424/0x428), returns (open file, [(path, offset, size)])."""
    f = open(path, "rb")
    d = f.read(0x440)
    fo, fs = struct.unpack(">II", d[0x424:0x42C])
    f.seek(fo)
    fst = f.read(fs)
    n = struct.unpack(">I", fst[8:12])[0]
    strs = fst[n * 12:]
    ents = [struct.unpack(">III", fst[i * 12:i * 12 + 12]) for i in range(n)]
    files = []
    def name(o):
        return strs[o:strs.index(b"\0", o)].decode("latin1")
    def walk(s, e, p):
        i = s
        while i < e:
            w0, a, b = ents[i]
            nm = p + name(w0 & 0xFFFFFF)
            if w0 >> 24 == 1:
                walk(i + 1, b, nm + "/")
                i = b
            else:
                files.append((nm, a, b))
                i += 1
    walk(1, n, "")
    return f, files


def sa_tools_inis():
    """Paths of the cached SA Tools GC_SADX INIs, downloading them first if the cache is empty."""
    os.makedirs(INI_CACHE, exist_ok=True)
    cached = glob.glob(os.path.join(INI_CACHE, "*"))
    if cached:
        return cached
    with urllib.request.urlopen(INI_LISTING, timeout=30) as r:
        entries = json.load(r)
    for e in entries:
        if e["type"] == "file" and e["name"].lower().endswith(".ini"):
            with urllib.request.urlopen(e["download_url"], timeout=30) as r:
                open(os.path.join(INI_CACHE, e["name"]), "wb").write(r.read())
    return glob.glob(os.path.join(INI_CACHE, "*"))


out = []
def w(s=""): out.append(s)

def row(name, what, path):
    w(f"  {name} | {what} | {path}")

# ---------------------------------------------------------------- header
w("SonicSunset resource map")
w("=========================")
w(f"Generated {datetime.date.today()} from the two GameCube disc images in the project root:")
w("  Sonic Adventure DX.iso   (GXSE8P, Sonic Adventure DX: Director's Cut, GameCube)")
w("  Shadow The Hedgehog.iso  (GUPE8P, Shadow the Hedgehog, GameCube)")
w("")
w("What this file is: a lookup table of the game assets on those discs - models, textures, audio,")
w("animations and collision - with a short note on what each one is and where to read it from.")
w("Everything else (object layouts, cameras, paths, scripts, fonts, UI layouts, videos, text, save data,")
w("code-only modules) is left out on purpose.")
w("")
w("Line format:   name | what it is [label] | path")
w("Path format:   <file on disc>                  a file at that path on the disc")
w("               <file on disc> > <entry>        an entry inside a Shadow .ONE archive (PRS-compressed)")
w("               <file on disc> @ 0xOFFSET       data at that file offset inside a SADX .rel module")
w("                                               (pointers inside need relocateRel first)")
w("")
w("Labels (how sure the 'what it is' is):")
w("  [obs]  observed in these files (parsed or decoded in this project)")
w("  [ext]  external source: SA Tools GameConfig, HeroesONE-Reloaded, format docs. Can be wrong.")
w("  [hyp]  hypothesis (from names, magic bytes or context)")
w("  [?]    unknown")
w("")

# ================================================================ SADX
f, files = listing(ROOT + "Sonic Adventure DX.iso")
byname = {n.lower(): (n, a, b) for n, a, b in files}
w("")
w("#" * 100)
w("# Sonic Adventure DX (GameCube) - Sonic Adventure DX.iso")
w("#" * 100)

# ---- .rel contents from SA Tools
w("")
w("== Models, animations and land tables inside .rel modules ==")
w("Names, types and offsets: SA Tools GameConfig/GC_SADX/*.ini [ext] (key=C900000, big-endian).")
w("Texture names refer to texture archives; '-> file' means a .gvm with that name exists on the disc [hyp].")
w("Model = Ninja basic model (NJS_OBJECT tree, non-DX struct sizes), readable by importSAModel [obs for Sonic: walked in a script, not yet run in the engine].")
w("Land table = stage geometry + collision flags per piece [ext].")
TYPE_WHAT = {
    "basicmodel": "model (Ninja basic)",
    "chunkmodel": "model (Ninja chunk, SA2-style)",
    "basicattach": "mesh without hierarchy (Ninja attach)",
    "landtable": "land table: stage geometry + collision",
}
mapped_rels = set()
inis = sorted(sa_tools_inis(), key=str.lower)
for p in inis:
    text = open(p, encoding="utf-8", errors="replace").read()
    m = re.search(r"^datafile=(.*)$", text, re.M)
    datafile = m.group(1).strip() if m else ("advertise.rel" if "advertise" in p.lower() else None)
    if not datafile or datafile.lower() not in byname:
        continue
    discname = byname[datafile.lower()][0]
    entries = []
    for sec in re.split(r"^\[", text, flags=re.M)[1:]:
        head, _, body = sec.partition("]")
        kv = dict(re.findall(r"^(\w+)=(.*)$", body, re.M))
        t = kv.get("type", "").strip()
        if t not in TYPE_WHAT and t != "animation":
            continue
        name = head.strip()
        if re.fullmatch(r"[0-9A-Fa-f]{6,8}", name):
            name = kv.get("filename", name).split("/")[-1].split(".")[0]
        if t == "animation":
            what = f"animation ({kv.get('numparts','?').strip()} nodes) [ext]"
        else:
            what = TYPE_WHAT[t] + " [ext]"
        tex = kv.get("texture", "").strip()
        if tex:
            gvm = byname.get(tex.lower() + ".gvm")
            what += f", texture {tex}" + (f" -> {gvm[0]} [hyp]" if gvm else "")
        addr = int(kv.get("address", "0").strip(), 16)
        entries.append((name, what, f"{discname} @ 0x{addr:X}"))
    if not entries:
        continue
    mapped_rels.add(discname.lower())
    w("")
    w(f"-- {discname} ({len(entries)}) --")
    for e in entries:
        row(*e)

# ---- unmapped rel modules
unmapped = sorted(n for n, a, b in files if n.lower().endswith(".rel") and n.lower() not in mapped_rels)
w("")
w("== .rel modules not mapped by SA Tools GC configs ==")
w("May hold models/animations (e.g. ev*.rel cutscenes) but their contents are not identified [?].")
for n in unmapped:
    row(n.rsplit(".", 1)[0], "code + data module, contents not mapped [?]", n)

# ---- disc-level files
cats = collections.OrderedDict([
    ("Textures", []), ("Models (Ninja binary)", []), ("Audio", []),
])
for n, a, b in sorted(files, key=lambda x: x[0].lower()):
    l = n.lower()
    stem = n.split("/")[-1].rsplit(".", 1)[0]
    if l.endswith(".gvm"):
        cats["Textures"].append((stem, "GVM texture archive (GameCube counterpart of PVM) [ext]", n))
    elif l.endswith(".gvm_"):
        cats["Textures"].append((stem, "GVM texture archive (magic GVMH; odd extension) [obs]", n))
    elif l.endswith(".gvr"):
        cats["Textures"].append((stem, "GVR single texture [ext]", n))
    elif l.endswith(".gvp"):
        cats["Textures"].append((stem, "GVP texture palette [ext]", n))
    elif l.endswith(".nb"):
        cats["Models (Ninja binary)"].append((stem, "Ninja binary (magic NJB), probably models/motions of this boss [hyp]", n))
    elif l.endswith(".adx"):
        cats["Audio"].append((stem, "CRI ADX audio stream (music / jingles) [ext]", n))
    elif l.endswith(".mlt"):
        cats["Audio"].append((stem, "sound bank (MLT) [ext]", n))
    elif l.endswith(".afs"):
        cats["Audio"].append((stem, "CRI AFS archive of event voice ADX (magic AFS) [obs]", n))
for c, items in cats.items():
    w("")
    w(f"== {c} ({len(items)}) ==")
    for i in items:
        row(*i)

# ================================================================ Shadow
f, files = listing(ROOT + "Shadow The Hedgehog.iso")
w("")
w("")
w("#" * 100)
w("# Shadow the Hedgehog (GameCube) - Shadow The Hedgehog.iso")
w("#" * 100)
w("")
w("== Entry types inside .ONE archives ==")
INNER = {
    "DFF": "RenderWare clump: model [ext; parsed only in character/shadow.one]",
    "BSP": "RenderWare world: level geometry [ext]",
    "TXD": "RenderWare texture dictionary [ext]",
    "BON": "named bone hierarchy for the .DFF it belongs to [obs in character/shadow.one, hyp elsewhere]",
    "MTP": "motion package, probably animations [hyp]",
    "MTN": "motion, probably animation [hyp]",
    "CCL": "probably collision [hyp]",
    "UVA": "RenderWare UV animation [ext]",
    "DMA": "probably RenderWare delta morph animation [hyp]",
    "SNB": "probably camera animation (names like CAM_*) [hyp]",
    "EFD": "probably effect data [hyp]",
    "RG1": "unknown [?]", "ADB": "unknown [?]", "CEN": "unknown [?]", "BDT": "unknown [?]",
    "PTP": "unknown [?]", "PTB": "unknown [?]", "BIN": "unknown [?]", "RP2": "unknown [?]",
}
SKIP_INNER = {"GNCP"}
for k, v in INNER.items():
    w(f"  .{k:<4} {v}")
w("  .GNCP entries (UI layouts) are left out.")

def one_entries(a):
    f.seek(a)
    h = f.read(0x40)
    ver = h[0x0C:0x1C].split(b"\0")[0]
    cnt = struct.unpack("<I", h[0x1C:0x20])[0]
    f.seek(a + 0x40)
    t = f.read((cnt + 2) * 56)
    res = []
    for k in range(cnt + 2):
        e = t[k * 56:k * 56 + 56]
        if ver == b"One Ver 0.60":
            name, eob = e[:44], struct.unpack("<I", e[52:56])[0]
        else:
            name, eob = e[:32], struct.unpack("<I", e[40:44])[0]
        if eob != 1:
            continue
        res.append(name.split(b"\0")[0].decode("latin1"))
    return ver.decode(), res

ONE_GROUP = [
    ("character/", "character"), ("enemy/", "enemy"), ("event/", "cutscene"),
    ("common/", "common"), ("stg", "stage"), ("", "other"),
]
w("")
w("== .ONE archives (name of each entry, what it is, archive > entry) ==")
for prefix, label in ONE_GROUP:
    group = [x for x in sorted(files, key=lambda x: x[0].lower())
             if x[0].lower().endswith(".one") and x[0].startswith(prefix)
             and not any(x[0].startswith(p2) for p2, _ in ONE_GROUP[:ONE_GROUP.index((prefix, label))] if p2)]
    if prefix == "":
        group = [x for x in sorted(files, key=lambda x: x[0].lower())
                 if x[0].lower().endswith(".one") and not any(x[0].startswith(p2) for p2, _ in ONE_GROUP if p2)]
    if not group:
        continue
    w("")
    w(f"---- {label} archives ({len(group)}) ----")
    for n, a, b in group:
        ver, names = one_entries(a)
        names = [x for x in names if x.rsplit(".", 1)[-1].upper() not in SKIP_INNER]
        if not names:
            continue
        w("")
        w(f"-- {n} ({ver}, {len(names)} entries) --")
        for e in names:
            ext = e.rsplit(".", 1)[-1].upper() if "." in e else ""
            row(e, INNER.get(ext, "unknown [?]"), f"{n} > {e}")

cats = collections.OrderedDict([("Models (loose)", []), ("Textures (loose)", []), ("Audio", [])])
for n, a, b in sorted(files, key=lambda x: x[0].lower()):
    l = n.lower()
    stem = n.split("/")[-1].rsplit(".", 1)[0]
    if l.endswith(".dff"):
        cats["Models (loose)"].append((stem, "RenderWare clump: model [ext]", n))
    elif l.endswith(".txd"):
        what = "RenderWare texture dictionary, UI [ext]" if l.startswith("csdfiles/") else "RenderWare texture dictionary [ext]"
        cats["Textures (loose)"].append((stem, what, n))
    elif l.endswith(".png"):
        cats["Textures (loose)"].append((stem, "PNG image [obs]", n))
    elif l.endswith(".adx"):
        cats["Audio"].append((stem, "CRI ADX audio stream (music / event songs) [ext]", n))
    elif l.endswith(".mlt"):
        cats["Audio"].append((stem, "sound bank (magic gcaxMLT) [obs]", n))
    elif l.endswith(".afs"):
        cats["Audio"].append((stem, "CRI AFS archive of voice clips (magic AFS) [obs]", n))
    elif l.startswith("sound/audiodata") and l.endswith(".bin"):
        cats["Audio"].append((stem, "probably sound cue table for this stage/actor [hyp]", n))
for c, items in cats.items():
    w("")
    w(f"== {c} ({len(items)}) ==")
    for i in items:
        row(*i)

open(ROOT + "ResourceMap.txt", "w").write("\n".join(out) + "\n")
print(len(out), "lines")
