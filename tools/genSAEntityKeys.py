"""
Generates src/SAEntityKeys.h: the SAEntityKey of every object type of an SADX stage's object list whose model could be found, a table of SAPlacedEntityKey (object type -> key)
for the stage's SALandTableKey, and the object list's names (type -> name).

Run from anywhere:  python3 tools/genSAEntityKeys.py
Inputs: build/Disks/SADX.iso (the disc table through tools/dumpCCL.py; the .rel modules through tools/decomp/sadx/relTool.py, which reads and decompresses them), and the SA Tools INIs
of tools/.cache/gc_sadx/ (_Stg01.ini, _Main.ini: names, addresses and textures of the modules' models). Writes src/SAEntityKeys.h and External/Prefabs/SADX/Placement.json (the objects' rotation recipes and variant field,
which are not resource keys: Loading::loadPrefabData gives them to the prefabs).

Where each fact comes from (external: SA Tools' INIs and the module's own data; the object list and relocations decoded from _Stg01.rel):
- The object list of a stage module (STAGES below): an array of 20 byte entries { u32 flags, f32 clip distance squared, u32 0, u32 function, u32 name pointer } starting at ENTRIES, in the
  order the SET files' types index it (Set0100S.bin's types are all below its count). The first 32 are shared by every stage; their function pointers are relocations into _Main.rel.
  SA Tools' INI puts the object list at 0x251FD0; the entries were found by the pointer to the name "RING" and the count (84) is the word 0x54 next to it.
- An object's model: the module's relocations tell which data each instruction of its function refers to (the high and low halves of an address, types 6 and 4). The first model of the INI
  (basicmodel with a texture, not a Debug one) referenced between the function's start and the next function start (the targets of calls and of function pointers, in the module) is the object's
  model. Hypothesis: the object's main function may draw several models, or reach its model through another function or a table, and then the first one is not the object's or none is found.
  MODEL_NAMES (tools/data/SADXObjectModels.json) overrides it by the INI's model name for the shared objects, whose functions are in _Main.rel with no clear boundaries (hypothesis from the names).
- Textures: the INI's `texture` of the model (OBJ_BEACH, OBJ_REGULAR ...) is the name of a .gvm on the disc (the disc's own spelling is used: it is case sensitive).
- Motions: a model's INI section lists its animation files (`animations=`); each is an `animation` section with its address and `numparts` (the node count SAMotionKey needs). They are the
  entity's motions, in the INI's order with the ones named like an idle (wait, stand, swim, fly, walk, mihari) first; Level plays the first.
- Collisions: a collision table (the 0x30 byte entries of Physics::CollisionShape) is a literal address that the object's own function (its exec function, and what it names) passes to the game's collision init
  with the entry count and a mode, as the Ghidra pseudocode of the module shows (sadxDecomp/decompiled*/pseudocode.c; the call and the table, with `count` plausible entries: form <= 10, sane floats).
  The function and the functions it names (calls and the exec / display / destroy pointers it stores), one level down, are searched; a table the code computes is not found. For the shared objects (code in _Main.rel) only tables that src/SAShapeKeys.h already has, with its counts from the decompile, are taken.
- A model with no `texture` in the INI is drawn with the stage's object textures (STAGE_TEXTURES); the first model in code order may be one the object only collides with or only spawns, which is why
  NO_MODEL (tools/data/SADXObjectModels.json) lists the spawners. A model reached through an array of pointers counts (the first model of the array).
- Rotation: the objects' display code turns by the task's angles with the Ninja rotate functions of main.dol (0x8004AFDC about z, 0x8004B3E0 about x, 0x8004AF5C about y; external, the decompile
  of main.dol: each builds a rotation about the axis and multiplies it into the current matrix on the right). The calls to them in the object's code, in code order, with the angle each is given
  (found by following r4 backwards to a `lwz r4, 0x14 / 0x18 / 0x1C (task)` of the task's x, y or z angle, plus the `addi`, `addis` and `neg` on the way, or a constant) are the object's recipe
  (the first function of the object's code that has any; the exec function and what it calls or names, two levels). A recipe with an angle that could not be followed or that is a constant, or that does not turn about all three axes (a display that only uses the yaw, or an effect's turn such as O PIER's 45 degrees about x), is not used; one that equals
  the usual z, x, y is not written (Placement.json then has no rotation for it).
Objects with no model found are listed on stdout; they are not in the table, so the level gives them a marker.
"""
import bisect, collections, json, math, os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT + "tools/decomp/sadx")
import dumpCCL
import relTool

dumpCCL.ISO = ROOT + "build/Disks/SADX.iso"
INI = ROOT + "tools/.cache/gc_sadx/"
OUT = ROOT + "src/SAEntityKeys.h"
PLACEMENT_OUT = ROOT + "External/Prefabs/SADX/Placement.json"

# stage name -> (module, its INI, address of the first object list entry, entry count, the SET file)
STAGES = {
    "EmeraldCoast": ("_Stg01.rel", "_Stg01.ini", 0x251940, 84, "Set0100S.bin"),
}

# The texture archive (INI texture name) of a stage's own objects, for a model whose INI section has no `texture` (e.g. "Pier Collision": observed in the decompile of _Stg01.rel that O PIER
# draws that model, the one its dynamic collision uses, with the stage's object textures).
STAGE_TEXTURES = {"EmeraldCoast": "OBJ_BEACH"}

# Objects with no model, the stage objects whose model is named by hand and the shared objects' model names: tools/data/SADXObjectModels.json (hand-written, with the evidence of each).
with open(ROOT + "tools/data/SADXObjectModels.json") as _f:
    _OBJECT_MODELS = json.load(_f)
NO_MODEL = set(_OBJECT_MODELS["noModel"]["names"])
STAGE_MODEL_NAMES = dict(_OBJECT_MODELS["stageModels"]["values"])
MODEL_NAMES = dict(_OBJECT_MODELS["mainModels"]["values"])

# O PARASOL's kinds: observed, the display function (found by tools/sadxLiveTasks.py in a RAM dump of Emerald Coast act 1, whose display code is _Stg01.rel 0xE008) indexes a table of 5 pointers
# (_Stg01.rel 0x249CC4) by the kind, and each entry is an array of the kind's model and its breakable pieces: kind 0 Parasol A, 1 Parasol B, 2 Breakable Lounge Chair, 3 Breakable Table Chair, 4
# Breakable Table. That is why records of different kinds sit at the same spot (an umbrella, a chair and a table together).
VARIANT_MODELS = {"O PARASOL": ["Parasol A", "Parasol B", "Breakable Lounge Chair", "Breakable Table Chair", "Breakable Table"]}
# O PIER: observed in the pseudocode of _Stg01.rel (the init 0x12B84 and the display 0x12458): the kind is the record's scale y & 1; it picks an array of models at 0x249F6C (0x249E98 straight, 0x249F00
# corner: element 0 the whole tile, the others its breakable pieces, see docs/EMERALD_COAST_OBJECTS.md) and a collision table at 0x24A034. VARIANT_FIELD is the scale component that picks the variant.
VARIANT_MODELS["O PIER"] = ["Pier (Straight)", "Pier (Corner)"]
VARIANT_FIELD = {"O PIER": 1}
# Collision tables per variant: (offset of the array of table pointers in the stage module, number of variants, entries per table). O PARASOL: the pointer table at 0x249D98 (6 pointers; the kind is the
# scale x byte modulo 5 as for its models, and the 5 pointers of kinds 0 to 4 are used; a RAM dump of Emerald Coast act 1 shows the kind 4 table at 0x249D38 and the kind 5 table (= kind 0) at 0x249CD8).
VARIANT_COLLISIONS = {"O PARASOL": (0x249D98, 5, 2), "O PIER": (0x24A034, 2, 2)}

# O KASA (display code seen in a RAM dump of Emerald Coast act 1, machine code, _Stg01.rel 0xB0B8) turns about y only.
ROTATION_OVERRIDES = {"O KASA": [("Y", 1, 0, False)]}
# O JUMP (the launch board): its display (_Stg01.rel 0x9560, called by the display function 0x951c; machine code) pushes the matrix, translates, rotates about z, y, x by the task's angles and draws the
# board's models; the recipe finder gives that to O IWA instead, whose code sits in the same merged function range (0x930c). A launch board is pitched (x) before it is turned (y).
# O BGIRO: its code (_Stg01.rel 0x6B38 on, machine code) turns by the task's y angle only, in all 8 rotate calls (no z, x), like O KASA; the records' x and z angles (e.g. 65408 and 256) are not used.
# E SARU (Kiki): observed (user): rotated wrongly everywhere. Its display (_Main.rel 0x3D748, the pseudocode FUN_8053d748) turns about y by the record's x angle plus its y angle (the init
# 0x3D900 also reads the x angle as a heading, and keeps parameters in the z angle's low bytes), scales by 1.2 times the work scale (not applied by us), and turns about y by 0x8000 before drawing the model.
# Turns about the same axis add up, so the steps are y by x, y by y and y by 0x8000 (field 3 = no angle).
ROTATION_OVERRIDES["E SARU"] = [("Y", 0, 0, False), ("Y", 1, 0, False), ("Y", 3, 32768, False)]
ROTATION_OVERRIDES["O BGIRO"] = [("Y", 1, 0, False)]
ROTATION_OVERRIDES["O JUMP"] = [("Z", 2, 0, False), ("Y", 1, 0, False), ("X", 0, 0, False)]

def be(b, o, f=">I"):
    return struct.unpack_from(f, b, o)[0]


def parse_ini(path):
    secs = {}
    cur = None
    for line in open(path):
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            cur = line[1:-1]
            secs[cur] = {}
        elif "=" in line and cur:
            k, v = line.split("=", 1)
            secs[cur][k] = v
    return secs


class Module:
    """A relocated .rel module: its bytes (pointers applied, big-endian), the relocations, the function starts and the INI's models by file offset."""

    def __init__(self, name, ini):
        raw = relTool.disc_file(name)
        self.name = name
        self.b = bytearray(relTool.decompress(raw) if b"SaCompGC" in raw[:0x100] else raw)
        b = self.b
        self.mid, sc, st, it, isz = be(b, 0), be(b, 0xC), be(b, 0x10), be(b, 0x28), be(b, 0x2C)
        self.so = [be(b, st + s * 8) & ~1 for s in range(sc)]
        exe = {s for s in range(sc) if be(b, st + s * 8) & 1}
        self.relocs = []
        for i in range(isz // 8):
            tm, cur, ps, po = be(b, it + i * 8), be(b, it + i * 8 + 4), 0, 0
            while True:
                dl, ty, sec, ad = struct.unpack_from(">HBBI", b, cur)
                cur += 8
                if ty == 203:
                    break
                po += dl
                if ty == 202:
                    ps, po = sec, 0
                    continue
                self.relocs.append((self.so[ps] + po, ty, tm, sec, ad))
        for off, ty, tm, sec, ad in self.relocs:
            if ty == 1 and tm == self.mid:
                struct.pack_into(">I", b, off, self.so[sec] + ad)
        self.starts = sorted({self.so[sec] + ad for off, ty, tm, sec, ad in self.relocs if tm == self.mid and ty in (1, 10) and sec in exe})
        self.models = {}
        self.ini = parse_ini(INI + ini)
        for n, sec in self.ini.items():
            if sec.get("type") == "basicmodel":
                self.models[int(sec["address"], 16)] = (n, sec.get("texture"))
        by = collections.defaultdict(list)
        for off, ty, tm, sec, ad in self.relocs:
            if tm == self.mid and ty in (4, 6):
                by[off].append(self.so[sec] + ad)
        self.by = by
        self.offs = sorted(by)
        self.by_name = {n: (a, t) for a, (n, t) in self.models.items()}

    def in_data(self, o):
        """True if the file offset is in a data section that holds anything sizeable (not the code, and not the small constructor and constant sections (under 64 KiB))."""
        b = self.b
        st, sc = be(b, 0x10), be(b, 0xC)
        return any(be(b, st + x * 8) & ~1 and not be(b, st + x * 8) & 1 and be(b, st + x * 8 + 4) >= 0x10000 and self.so[x] <= o < self.so[x] + be(b, st + x * 8 + 4) for x in range(sc))

    def plausible(self, o):
        b = self.b
        if o + 0x30 > len(b) or b[o + 1] > 10:
            return False
        for off in (8, 12, 16, 0x14, 0x18, 0x1C):
            v = struct.unpack_from(">f", b, o + off)[0]
            if not math.isfinite(v) or abs(v) > 100000:
                return False
        return True

    def motions(self, model_name):
        """[(address, node count)] of the animations the INI lists for the model's section, an idle first."""
        sec = self.ini[model_name]
        if "animations" not in sec:
            return []
        folder = sec["filename"].rsplit("/", 1)[0]
        by_file = {s2["filename"].lower(): s2 for s2 in self.ini.values() if s2.get("type") == "animation"}
        out = []
        for a in sec["animations"].split(","):
            full = folder + "/" + a.replace("\\", "/").split("/")[-1]
            anim = by_file.get(full.lower())
            if anim is None:
                print(f"note: {self.name}: animation {full} of {model_name} has no section in the INI, skipped")
                continue
            out.append((full, int(anim["address"], 16), int(anim["numparts"])))
        out.sort(key=lambda m: 0 if any(k in m[0].lower() for k in ("wait", "stand", "swim", "fly", "walk", "mihari")) else 1)
        return [(a, n) for _, a, n in out]

    def first_model(self, f):
        """(offset, (name, texture or None)) of the first model the function at f refers to, directly or as the first model of an array of pointers, in code order (see the header)."""
        nxt = min([x for x in self.starts if x > f], default=f + 0x800)
        i = bisect.bisect_left(self.offs, f)
        while i < len(self.offs) and self.offs[i] < nxt:
            for tgt in self.by[self.offs[i]]:
                candidates = [tgt] + [be(self.b, tgt + 4 * k) for k in range(8) if tgt + 4 * k + 4 <= len(self.b)]
                for c in candidates:
                    m = self.models.get(c)
                    if m and not m[0].startswith("Debug"):
                        return c, m
            i += 1
        return None


class RotCode:
    """The calls to the Ninja rotate functions in a module's code, with their angles (see the header)."""
    ROTF = {0x8004AFDC: "Z", 0x8004B3E0: "X", 0x8004AF5C: "Y"}

    def __init__(self, mod):
        self.m = mod
        b = mod.b
        st, sc = be(b, 0x10), be(b, 0xC)
        self.exe = [(mod.so[x], mod.so[x] + be(b, st + x * 8 + 4)) for x in range(sc) if be(b, st + x * 8) & 1 and be(b, st + x * 8 + 4)]
        self.starts = [x for x in mod.starts if any(lo <= x < hi for lo, hi in self.exe)]
        self.rotcalls = collections.defaultdict(list)
        self.edges = collections.defaultdict(set)
        for off, ty, tm, sec, ad in mod.relocs:
            if not any(lo <= off < hi for lo, hi in self.exe):
                continue
            f = self.fn(off)
            if ty == 10 and tm == 0 and ad in self.ROTF:
                self.rotcalls[f].append((off, self.ROTF[ad]))
            if tm == mod.mid and ty in (1, 4, 6, 10):
                tgt = mod.so[sec] + ad
                if any(lo <= tgt < hi for lo, hi in self.exe):
                    self.edges[f].add(tgt)
        for f in self.rotcalls:
            self.rotcalls[f].sort()

    def fn(self, off):
        i = bisect.bisect_right(self.starts, off) - 1
        return self.starts[i] if i >= 0 else off

    def arg(self, off):
        """The angle given to the rotate call (a bl) at off: ("field", n, addend, negate), ("const", v) or None."""
        b = self.m.b
        want, add, neg = 4, 0, False
        s16 = lambda v: struct.unpack(">h", struct.pack(">H", v & 0xFFFF))[0]
        for w in range(off - 4, max(off - 80, 0), -4):
            x = be(b, w)
            op, rt, ra, rb = x >> 26, (x >> 21) & 31, (x >> 16) & 31, (x >> 11) & 31
            if op == 18 and (x & 1):
                break
            if op == 14 and rt == want:
                if ra == 0:
                    return ("const", -(s16(x) + add) if neg else s16(x) + add)
                add += s16(x)
                want = ra
            elif op == 15 and rt == want:
                if ra == 0:
                    return ("const", -((s16(x) << 16) + add) if neg else (s16(x) << 16) + add)
                add += s16(x) << 16
                want = ra
            elif op == 21 and ra == want:
                want = rt
            elif op == 31 and ((x >> 1) & 0x3FF) == 104 and rt == want:
                neg = not neg
                want = ra
            elif op == 31 and ((x >> 1) & 0x3FF) == 444 and ra == want and rt == rb:
                want = rt
            elif op == 32 and rt == want:
                d = x & 0xFFFF
                if d in (0x14, 0x18, 0x1C) and not (neg and add):
                    return ("field", (d - 0x14) // 4, add, neg)
                return None
        return None

    def recipe(self, f, depth=2):
        """([(axis, angle)], function) of the first function reachable from f (two levels) that calls a rotate function, until an axis repeats; None if there is none."""
        order, seen, frontier = [f], {f}, [f]
        for _ in range(depth):
            nxt = []
            for a in frontier:
                for t in sorted(self.edges.get(a, ())):
                    g = self.fn(t)
                    if g not in seen:
                        seen.add(g)
                        order.append(g)
                        nxt.append(g)
            frontier = nxt
        for g in order:
            calls = self.rotcalls.get(g)
            if not calls:
                continue
            seq, axes = [], set()
            for off, axis in calls:
                if axis in axes:
                    break
                axes.add(axis)
                seq.append((axis, self.arg(off)))
            return seq, g
        return None


def main():
    f, files = dumpCCL.disc_files()
    real = {p.lower(): p for p, _, _ in files}

    def disc_path(path):
        if path.lower() not in real:
            raise RuntimeError(f"no file {path} on the disc")
        return real[path.lower()]

    modules = {}

    def module(name, ini):
        if name not in modules:
            modules[name] = Module(name, ini)
        return modules[name]

    main_mod = module("_Main.rel", "_Main.ini")

    # The tables a function gives the game's collision init as a literal, read from the function's own body in the Ghidra pseudocode (the modules are mapped at 0x80400000 + file offset): a call
    # `init(task, 0x806xxxxx, count, mode)` whose table holds `count` plausible entries. The function ranges of the modules are too wide to tell from the relocations which tables a function loads
    # (observed: SPRING got tables of other objects, SPRINGB none, O BED a second table of another object that a RAM dump shows it does not use; the palms' code adds 0x30 / 0x60 / 0x90 to the base address,
    # which the pseudocode already shows as the sum). _Main.rel's pseudocode is sadxDecomp/decompiled/ (its collision init is 0x8041E728), _Stg01.rel's sadxDecomp/decompiled_stg01/ (its own wrapper).
    pseudo = {}
    for mod_name, path in (("_Main.rel", "decompiled/pseudocode.c"), ("_Stg01.rel", "decompiled_stg01/pseudocode.c")):
        funcs = {}
        for chunk in re.split(r"\n(?=// FUN )", open(ROOT + "sadxDecomp/" + path).read()):
            head = re.match(r"// FUN (\w+) ", chunk)
            if head:
                funcs[int(head.group(1), 16)] = chunk
        pseudo[mod_name] = funcs
    init_call = re.compile(r"\w+\(\w+,(?:&DAT_|0x)(80[67][0-9a-fA-F]{5}),(\d+),(\d+)\)")

    def init_tables(mod, func):
        """[(offset, count)] of the tables the function and what it names (calls, or stores as its exec / display / destroy function), one level, give the collision init."""
        funcs = pseudo[mod.name]
        seen, level, found = set(), [0x80400000 + func], []
        for _ in range(2):
            following = []
            for addr in level:
                if addr in seen or addr not in funcs:
                    continue
                seen.add(addr)
                body = funcs[addr]
                for text, n, mode in init_call.findall(body):
                    t, n = int(text, 16) - 0x80400000, int(n)
                    if 1 <= n <= 16 and int(mode) <= 8 and mod.in_data(t) and all(mod.plausible(t + 0x30 * k) for k in range(n)) and (t, n) not in found:
                        found.append((t, n))
                following += [int(x, 16) for x in re.findall(r"FUN_(80[0-9a-f]{6})", body)]
            level = following
        return found

    # The collision tables of _Main.rel that tools/genSAShapeKeys.py found from the decompile, with their counts.
    shape_keys = {int(m.group(1), 16): int(m.group(2)) for m in re.finditer(r'"_Main\.rel", 0x([0-9A-Fa-f]+), (\d+)\}', open(ROOT + "src/SAShapeKeys.h").read())}
    out = [
        "#pragma once",
        "#include <cstdint>",
        "#include <map>",
        '#include "SAKeys.h"',
        "",
        "/*",
        "GENERATED by tools/genSAEntityKeys.py, do not edit by hand (run the script again; see the script for where each fact comes from). One SAEntityKey for each object type of a",
        "stage's object list whose model was found, named SAEntity_<stage>_<type>, per stage a table of SAPlacedEntityKey (type -> key) for its SALandTableKey, and the object list's names.",
        "The model of an object is a hypothesis (the first model its function refers to, or by name for the shared objects); motions are the INI's, collisions the tables its code loads.",
        "*/",
    ]
    texts = []
    placement = {}
    for stage, (rel, ini, table, count, setfile) in STAGES.items():
        mod = module(rel, ini)
        b = mod.b
        ptr = {off: (tm, sec, ad) for off, ty, tm, sec, ad in mod.relocs if ty == 1}
        # The shared objects' functions are named by this stage's object list, not by _Main.rel itself: they are function starts of _Main.rel too.
        shared = {main_mod.so[ptr[table + 20 * i + 12][1]] + ptr[table + 20 * i + 12][2] for i in range(count)
                  if not be(b, table + 20 * i + 12) and (table + 20 * i + 12) in ptr and ptr[table + 20 * i + 12][0] == main_mod.mid}
        main_mod.starts = sorted(set(main_mod.starts) | shared)
        rotcode = {mod.name: RotCode(mod), main_mod.name: RotCode(main_mod)}

        # A recipe found in a function that belongs to several objects (not to the exec function of the object itself) is not trusted: it is a shared helper, not this object's own turn.
        def exec_target(i):
            e = table + 20 * i
            func = be(b, e + 12)
            if func:
                return mod.name, func
            if (e + 12) in ptr and ptr[e + 12][0] == main_mod.mid:
                return main_mod.name, main_mod.so[ptr[e + 12][1]] + ptr[e + 12][2]
            return None
        users = collections.defaultdict(set)
        for i in range(count):
            t = exec_target(i)
            if t:
                found = rotcode[t[0]].recipe(t[1])
                if found:
                    users[(t[0], found[1])].add(i)
        shared_recipe_fns = {k for k, v in users.items() if len(v) >= 2}
        names, table_rows, missing = {}, [], []
        stats = [0, 0, 0]
        for i in range(count):
            e = table + 20 * i
            name_ptr = be(b, e + 16)
            name = bytes(b[name_ptr:b.index(b"\0", name_ptr)]).decode("latin1").strip()
            names[i] = name
            func = be(b, e + 12)
            target = (mod, func) if func else None
            if target is None and (e + 12) in ptr and ptr[e + 12][0] == main_mod.mid:
                target = (main_mod, main_mod.so[ptr[e + 12][1]] + ptr[e + 12][2])
            model = None
            if name in NO_MODEL:
                missing.append(f"{i} {name} (spawner)")
                continue
            if name in STAGE_MODEL_NAMES:
                if STAGE_MODEL_NAMES[name] not in mod.by_name:
                    raise RuntimeError(f"{mod.name}: no INI model {STAGE_MODEL_NAMES[name]}")
                addr, tex = mod.by_name[STAGE_MODEL_NAMES[name]]
                model = (mod.name, addr, STAGE_MODEL_NAMES[name], tex)
            elif name in MODEL_NAMES:
                if MODEL_NAMES[name] not in main_mod.by_name:
                    raise RuntimeError(f"_Main.ini has no model {MODEL_NAMES[name]}")
                addr, tex = main_mod.by_name[MODEL_NAMES[name]]
                model = ("_Main.rel", addr, MODEL_NAMES[name], tex)
            elif target is not None:
                hit = target[0].first_model(target[1])
                if hit:
                    model = (target[0].name, hit[0], hit[1][0], hit[1][1])
            if model is None:
                missing.append(f"{i} {name}")
                continue
            if model[3] is None and model[0] != mod.name:
                missing.append(f"{i} {name} (shared model with no texture)")
                continue
            tex = disc_path((model[3] or STAGE_TEXTURES[stage]) + ".gvm")
            ident = f"SAEntity_{stage}_{i}"

            motions = [f'{{"{model[0]}", 0x{a:X}, {n}}}' for a, n in modules[model[0]].motions(model[2])]
            collisions = []
            if target is not None:
                for t, n in init_tables(target[0], target[1]):
                    if target[0] is not main_mod or shape_keys.get(t) == n:
                        collisions.append(f'{{"{target[0].name}", 0x{t:X}, {n}}}')
            rotation = []
            if target is not None:
                found = rotcode[target[0].name].recipe(target[1])
                seq = None
                if found and (found[1] == target[1] or (target[0].name, found[1]) not in shared_recipe_fns):
                    seq = found[0]
                if seq and len(seq) == 3 and all(a is not None and a[0] == "field" for _, a in seq):
                    usual = [("Z", ("field", 2, 0, False)), ("X", ("field", 0, 0, False)), ("Y", ("field", 1, 0, False))]
                    if seq != usual:
                        for axis, a in seq:
                            rotation.append((axis, 3, a[1], False) if a[0] == "const" else (axis, a[1], a[2], a[3]))
            if name in ROTATION_OVERRIDES:
                rotation = ROTATION_OVERRIDES[name]
            stats[2] += bool(rotation)
            variants = []
            if name in VARIANT_MODELS:
                for vname in VARIANT_MODELS[name]:
                    variants.append("{nullptr, 0}" if vname is None else f'{{"{mod.name}", 0x{mod.by_name[vname][0]:X}}}')
            variant_collisions = []
            if name in VARIANT_COLLISIONS:
                array, kinds, entries = VARIANT_COLLISIONS[name]
                for k in range(kinds):
                    variant_collisions.append("{" + f'{{"{mod.name}", 0x{be(b, array + 4 * k):X}, {entries}}}' + "}")
            tail = ""
            if variant_collisions:
                tail = f", {{{', '.join(variant_collisions)}}}"
            if rotation or name in VARIANT_FIELD:
                placement[name] = {}
                if rotation:
                    placement[name]["rotation"] = [{"axis": a, "field": f, "addend": d, "negate": n} for a, f, d, n in rotation]
                if name in VARIANT_FIELD:
                    placement[name]["variantField"] = VARIANT_FIELD[name]
            texts.append(f'/* {name}: {model[2]} */')
            texts.append(f'inline const SAEntityKey {ident}{{"{name}", {{"{model[0]}", 0x{model[1]:X}}}, {{"{tex}"}}, {{{", ".join(motions)}}}, {{{", ".join(collisions)}}}, {{{", ".join(variants)}}}{tail}}};')
            table_rows.append((i, ident))
            stats[0] += bool(motions)
            stats[1] += bool(collisions)
        texts.append("")
        texts.append("inline const std::map<uint16_t, const char*> SAObjectTypeNames_" + stage + "{")
        texts += [f'\t{{{i}, "{n}"}},' for i, n in sorted(names.items())]
        texts.append("};")
        texts.append("")
        texts.append(f"inline const std::vector<SAPlacedEntityKey> SAEntityTable_{stage}{{")
        texts += [f"\t{{{i}, {ident}}}," for i, ident in table_rows]
        texts.append("};")
        print(f"{stage}: {len(table_rows)} of {count} types have a model ({stats[0]} with motions, {stats[1]} with collisions, {stats[2]} with their own rotation order); none for: " + ", ".join(missing))
    open(OUT, "w").write("\n".join(out + texts) + "\n")
    os.makedirs(os.path.dirname(PLACEMENT_OUT), exist_ok=True)
    with open(PLACEMENT_OUT, "w") as f:
        json.dump(placement, f, indent=1)
        f.write("\n")


if __name__ == "__main__":
    main()
