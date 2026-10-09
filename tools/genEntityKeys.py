"""
Generates src/STH2005EntityKeys.h: the STH2005::EntityKey of every object whose .BIN is in the common gadget archive (stg_cmn_gdt.one) or in a stage's
(stgNNNN/stgNNNN_gdt.one), and for each stage a table of STH2005::PlacedEntityKey (object type id -> key) that the stage's LevelKey uses.

Run from anywhere:  python3 tools/genEntityKeys.py [stage ...]      (default: stg0100; a stage is its folder name)
Inputs: build/Disks/Shadow2005.iso (the disc file table, .ONE and PRS through tools/dumpCCL.py, main.dol read from the disc). Writes src/STH2005EntityKeys.h and External/Prefabs/STH2005/Placement.json (the per-object layout, variants and loop flag, which are not resource keys: Loading::loadPrefabData gives them to the prefabs).

Where each fact comes from (external: the Ghidra decompile of main.dol and the files of the disc; the layouts are decoded from the files):
- Object type ids and names: the registry of main.dol, a null terminated array of descriptor pointers at 0x8052B080 (REGISTRY_ADDRESS); a descriptor's
  first word points at its name (0 in two descriptors, ids 24 and 57, which are named UnnamedNN here), and the u16 in the high half of its word at +0x18 is the id (the ids a stage's placement files use, see src/Importers/STH2005/Placement.cpp).
- .BIN (an entry of a gdt archive, one per object kind): the same relocatable container as a .CCL (u32 size, u32 A = size of section 1, u32 B, u32 1, u32 0, u32 0x3142, ...,
  section 1 from 0x20, big-endian, pointers are offsets into section 1). It starts with four {pointer, count} pairs: models, infos, motions, collisions.
  An info is 16 bytes (name, pointer to a list, 0, count) and the model table follows the infos. A model is 64 bytes: name, .DFF, .UVA (0 = none), ...; the
  words after are not used here (word 4 names a parent model in many entries; how a child attaches to it is not decoded, so each model is its own key). A
  motion is 28 bytes: name, the model it animates, .BON, motion name, .MTN file, .MTN name, 0. A collision is 24 bytes: name, .CCL file, two words not used here.
- Word 4 of a model names its parent model and word 5 is the HAnim id of a bone of the parent's .BON (observed: DASHPANEL01's parent is model0 with 0x7D0, the id of the one bone of DASHPANEL00.BON,
  \"DashPanel\"; BATTERYBK's children name 0x251C, 0x251D, 0x7D0 ... which are bones of BATTERYBK.BON). A child whose parent has a .BON containing that id is a part of the parent's ModelKey, attached to that bone
  by name (the way ModelPart::bone works); a child whose parent has no .BON, or no such bone, stays a model of its own.
- A model's .BON is the one of the motion that names the model (a model without a motion has none: it is imported as a static mesh).
- An info is a variant: the BIN's models it lists, by name (Placement.json holds their indices into the key's models, children attached to another model left out). The first info is what an object is made of by default
  (observed: CAPTURECAGE's info 'cage' lists CAPTCAGE.DFF and CAPTCAGE1.DFF, the bars); the BINs in VARIANT_STEMS pick the info by the record.
- Motions: a motion entry naming an .MTP gives one MotionKey per motion of that .MTP (names read from its table, the entry's .BON); one naming an .MTN gives one (a .MTN is a single
  motion, see src/Importers/STH2005/Mtp.cpp), named after the file.

Which BIN an object type uses is not stored anywhere that was found: the BIN is the type's name in capitals without underscores and spaces (RING, SPRING, Helicopter ->
HELICOPTER), or the entry of NAME_OVERRIDES (below), written from the names alone: hypothesis. A type with no BIN (enemies, particles, invisible objects) is skipped
and listed on stdout. A stage's own BIN is preferred over the common one. Objects with no BIN but an archive of their own (ARCHIVE_ENTITIES, CHARACTERS) are listed by hand in tools/data/STH2005Objects.json; they are living things, so their motion loops (loopMotion in Placement.json).
"""
import json, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dumpCCL

OUT = dumpCCL.ROOT + "src/STH2005EntityKeys.h"
PLACEMENT_OUT = dumpCCL.ROOT + "External/Prefabs/STH2005/Placement.json"
REGISTRY_ADDRESS = 0x8052B080
COMMON_ARCHIVE = "stg_cmn_gdt.one"

# Registry names whose BIN has another name (hypothesis, from the names alone).
NAME_OVERRIDES = {
    "HRD_CONTAINER": "CONTAINERHARD",
    "UNBRK_CONTAINER": "CONTAINERUNBREAK",
    "WEAPON_CONTAINER": "CONTAINERWEAPON",
    "SECRET CONTAINER": "CONTAINERSECRET",
    "UD_REEL": "UPDOWNREEL",
    "MOVE_FOOTING": "FOOTINGMOVABLE",
    "BRK_OBJ": "BREAKOBJ",
    "BREAK_BLAST": "BLASTBREAK",
    "CAGE": "CAPTURECAGE",
    "SET_WEAPON": "SETWEAPON",
    "ART WORK": "ARTISTWORK",
    "ART GADGET": "ARTISTGADGET",
    "ART WORK ZONE": "ARTWORKZONE",
    "ART GADGET ZONE": "ARTGADGETZONE",
}


# BINs whose textures are in another archive than their models' (stem -> the archive whose STG_GDT.TXD to use): FRUIT's JG0404BKFRUIT.DFF names s0404_nn022, which the common and stg0100's
# STG_GDT.TXD lack and the ones of stages 0301, 0404 and 0603 have (observed); the model is named after stage 0404, so that one (hypothesis: where the game finds the texture is unknown).
TEXTURE_ARCHIVES = {"FRUIT": "stg0404/stg0404_gdt.one"}
# BINs whose objects are laid out as a ring line ("RingLine" in Placement.json, see src/Importers/STH2005/RingLine.cpp: from the decompile of the RING factory).
RING_LINE_STEMS = {"RING"}
# BINs whose objects pick their models by the placement's first parameter ("Variant" in Placement.json): observed on ARTISTGADGET, whose 83 stg0100 records have a u32 of 0 to 19 there and the BIN
# has 27 infos named after street objects (hypothesis: the index of the info; not checked for the other ART BINs).
VARIANT_STEMS = {"ARTISTGADGET"}


# The objects of archives of their own, the character picked by SET_CHARACTOR and the extra parts of a bundle: tools/data/STH2005Objects.json (hand-written, with the evidence of each).
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "STH2005Objects.json")) as _f:
    _OBJECTS = json.load(_f)
ARCHIVE_ENTITIES = {k: tuple(v) for k, v in _OBJECTS["archiveEntities"]["values"].items()}
CHARACTOR_TYPE = _OBJECTS["characterType"]
CHARACTERS = {int(k): tuple(v) for k, v in _OBJECTS["characters"]["values"].items()}
BUNDLE_PARTS = {k: [tuple(p) for p in v] for k, v in _OBJECTS["bundleParts"]["values"].items()}


def be(b, o, f=">I"):
    return struct.unpack_from(f, b, o)[0]


def read_dol(f, files):
    """The sections of main.dol: the disc's boot.bin says where it is (0x420)."""
    f.seek(0x420)
    off = be(f.read(4), 0)
    f.seek(off)
    h = f.read(0x100)
    secs = []
    for i in range(18):
        so, sa, ss = be(h, i * 4), be(h, 0x48 + i * 4), be(h, 0x90 + i * 4)
        if ss:
            f.seek(off + so)
            secs.append((sa, f.read(ss)))
    return secs


def dol_u32(secs, a):
    for sa, d in secs:
        if sa <= a < sa + len(d):
            return be(d, a - sa)
    raise RuntimeError(f"address 0x{a:X} is not in main.dol")


def dol_str(secs, a):
    for sa, d in secs:
        if sa <= a < sa + len(d):
            o = a - sa
            return d[o:d.index(b"\0", o)].decode("latin1")
    raise RuntimeError(f"address 0x{a:X} is not in main.dol")


def registry(secs):
    types = {}
    a = REGISTRY_ADDRESS
    while dol_u32(secs, a):
        p = dol_u32(secs, a)
        name = dol_u32(secs, p)
        types[dol_u32(secs, p + 0x18) >> 16] = dol_str(secs, name) if name else f"Unnamed{dol_u32(secs, p + 0x18) >> 16}"
        a += 4
    return types


def one_entries(f, files, path):
    """{entry name: decompressed bytes} of a One Ver 0.60 archive of the disc."""
    off, size = next((o, s) for p, o, s in files if p.lower() == path.lower())
    f.seek(off)
    d = f.read(size)
    if d[0x0C:0x18] != b"One Ver 0.60":
        raise RuntimeError(f"{path}: not a One Ver 0.60 archive")
    count = be(d, 0x1C, "<I")
    ents = []
    for i in range(count + 2):
        e = 0x40 + i * 56
        if be(d, e + 52, "<I") != 1:
            continue
        ents.append((d[e:e + 44].split(b"\0")[0].decode("latin1"), be(d, e + 44, "<I"), be(d, e + 48, "<I")))
    out = {}
    for i, (name, usize, eoff) in enumerate(ents):
        end = ents[i + 1][2] if i + 1 < len(ents) else len(d) - 0xC
        data = dumpCCL.prs(d[0xC + eoff:0xC + end])
        if len(data) != usize:
            raise RuntimeError(f"{path} {name}: {len(data)} bytes, expected {usize}")
        out[name] = data
    return out


def parse_bin(name, d):
    """{'models': [(name, dff, uva)], 'motions': [(model, bon, mtn)], 'collisions': [ccl], 'infos': [(name, [model names])], 'parents': [(parent model name or None, bone id)]} of a .BIN, strict."""
    if be(d, 0) != len(d) or be(d, 0x0C) != 1 or be(d, 0x10) != 0 or be(d, 0x14) != 0x3142:
        raise RuntimeError(f"{name}: not a .BIN (header constants differ)")
    size1 = be(d, 4)
    s1 = d[0x20:0x20 + size1]

    def w(o):
        if o + 4 > len(s1):
            raise RuntimeError(f"{name}: word at 0x{o:X} is past section 1")
        return be(s1, o)

    def s(p):
        if p == 0:
            return None
        if p >= len(s1):
            raise RuntimeError(f"{name}: string pointer 0x{p:X} is past section 1")
        return s1[p:s1.index(b"\0", p)].decode("latin1")

    model_ptr, model_n, info_ptr, info_n, motion_ptr, motion_n, col_ptr, col_n = (w(i * 4) for i in range(8))
    if model_ptr != info_ptr + 16 * info_n:
        raise RuntimeError(f"{name}: the model table does not follow the infos")
    models = [(s(w(model_ptr + 64 * i)), s(w(model_ptr + 64 * i + 4)), s(w(model_ptr + 64 * i + 8))) for i in range(model_n)]
    parents = [(s(w(model_ptr + 64 * i + 16)), w(model_ptr + 64 * i + 20)) for i in range(model_n)]
    infos = []
    for i in range(info_n):
        ip, cnt = w(info_ptr + 16 * i + 4), w(info_ptr + 16 * i + 12)
        infos.append((s(w(info_ptr + 16 * i)), [s(w(ip + 8 * k)) for k in range(cnt)]))
    motions = [(s(w(motion_ptr + 28 * i + 4)), s(w(motion_ptr + 28 * i + 8)), s(w(motion_ptr + 28 * i + 16))) for i in range(motion_n)]
    cols = [s(w(col_ptr + 24 * i + 4)) for i in range(col_n)]
    return {"models": models, "motions": motions, "collisions": cols, "infos": infos, "parents": parents}


def bon_nodes(d):
    """[(HAnim id, name)] of a .BON (layout of src/Importers/STH2005/Bon.cpp: u16 node count at 0x0A, 0x70 byte records from 0x30, id first, name in the last 32 bytes)."""
    return [(be(d, 0x30 + 0x70 * i), d[0x30 + 0x70 * i + 0x50:0x30 + 0x70 * i + 0x70].split(b"\0")[0].decode("latin1")) for i in range(be(d, 0x0A, ">H"))]


def mtp_names(d):
    """The motion names of a .MTP (layout of src/Importers/STH2005/Mtp.cpp: u16 count at 2, table offset at 0x10, 12 byte entries starting with the name's offset)."""
    count, table = be(d, 2, ">H"), be(d, 0x10)
    names = []
    for i in range(count):
        o = be(d, table + 12 * i)
        names.append(d[o:d.index(b"\0", o)].decode("latin1"))
    return names


def idle_first(names):
    """The names with those that look like an idle (hypothesis: from the names) first, in file order otherwise; Level plays the first one while there is no behaviour."""
    return sorted(names, key=lambda n: 0 if any(k in n.lower() for k in ("idle", "stand", "fly")) else 1)


def ident(archive, stem):
    return "Entity_" + re.sub(r"\W", "_", archive[:-4].split("/")[-1]) + "_" + re.sub(r"\W", "_", stem)


def main():
    stages = sys.argv[1:] or ["stg0100"]
    f, files = dumpCCL.disc_files()
    types = registry(read_dol(f, files))

    # The game's disc lookup is case sensitive and the disc's names are mixed case (enemy/GunSoldierData_citii.one): every path written into the keys is the disc's own spelling.
    real = {p.lower(): p for p, _, _ in files}

    def disc_path(path):
        if path.lower() not in real:
            raise RuntimeError(f"no file {path} on the disc")
        return real[path.lower()]

    for name, (arch, dff, bon) in list(ARCHIVE_ENTITIES.items()):
        ARCHIVE_ENTITIES[name] = (disc_path(arch), dff, bon)
    for k, (name, arch, dff, bon) in list(CHARACTERS.items()):
        CHARACTERS[k] = (name, disc_path(arch), dff, bon)
    archives = [disc_path(COMMON_ARCHIVE)] + [disc_path(f"{st}/{st}_gdt.one") for st in stages]
    bins = {}
    contents = {}
    for arch in archives:
        ents = one_entries(f, files, arch)
        contents[arch] = ents
        for n, d in ents.items():
            if n.upper().endswith(".BIN"):
                bins[(arch, n[:-4].upper())] = parse_bin(f"{arch} {n}", d)

    out = [
        "#pragma once",
        '#include <cstdint>',
        '#include <map>',
        '#include "STH2005Keys.h"',
        "",
        "/*",
        "GENERATED by tools/genEntityKeys.py, do not edit by hand (run the script again; see the script for where each fact comes from). One STH2005::EntityKey",
        "for each .BIN of the common and the listed stages' gadget archives that an object type uses, named Entity_<archive>_<BIN>, and per stage a table of",
        "STH2005::PlacedEntityKey (object type id -> key) for its LevelKey. Models are named <BIN>:<model> inside the archive. ObjectTypeNames is the whole registry (id -> name).",
        "*/",
        "inline const std::map<uint16_t, const char*> ObjectTypeNames{",
    ] + [f'\t{{{t}, "{n}"}},' for t, n in sorted(types.items())] + ["};", ""]
    used = set()
    archive_used = set()
    tables = {}
    skipped = {}
    for st in stages:
        table = []
        for t in sorted(types):
            n = types[t]
            if n in ARCHIVE_ENTITIES:
                archive_used.add((t, n))
                table.append((t, ident(ARCHIVE_ENTITIES[n][0], n), None))
                continue
            if t == CHARACTOR_TYPE:
                for k, (cname, carch, _, _) in sorted(CHARACTERS.items()):
                    archive_used.add((t, cname))
                    table.append((t, ident(carch, cname), k))
                continue
            stem = NAME_OVERRIDES.get(n, n.upper().replace("_", "").replace(" ", ""))
            found = next(((a, stem) for a in (disc_path(f"{st}/{st}_gdt.one"), disc_path(COMMON_ARCHIVE)) if (a, stem) in bins), None)
            if found is None:
                skipped.setdefault(st, []).append(f"{t} {n}")
                continue
            used.add(found)
            table.append((t, ident(*found), None))
        tables[st] = table

    for arch, stem in sorted(used):
        b = bins[(arch, stem)]
        ents = contents[arch]
        txd = next((n for n in ents if n.upper().endswith(".TXD")), None)
        bon_of = {}
        for model, bon, mtn in b["motions"]:
            if model is not None and bon is not None:
                bon_of[model] = bon
        for _, dff, _ in b["models"]:
            if dff is None or dff not in ents:
                raise RuntimeError(f"{arch} {stem}.BIN names {dff}, which is not in the archive")
        for ccl in b["collisions"]:
            if ccl not in ents:
                raise RuntimeError(f"{arch} {stem}.BIN names {ccl}, which is not in the archive")
        for bon in bon_of.values():
            if bon not in ents:
                raise RuntimeError(f"{arch} {stem}.BIN names {bon}, which is not in the archive")
        a = f'{{"{arch}"}}'
        motion_keys = []
        for model, bon, file in b["motions"]:
            if file and file.upper().endswith(".MTP") and bon is not None:
                motion_keys += [f'{{{{"{arch}"}}, "{file}", "{n}", "{bon}"}}' for n in idle_first(mtp_names(ents[file]))]
            elif file and file.upper().endswith(".MTN") and bon is not None:
                motion_keys.append(f'{{{{"{arch}"}}, "{file}", "{file[:-4]}", "{bon}"}}')
        out.append(f"inline const STH2005::EntityKey {ident(arch, stem)}{{")
        out.append(f'\t"{stem}",')
        out.append("\t{")
        names = [m[0] for m in b["models"]]
        attached = {}
        for i, (model, dff, uva) in enumerate(b["models"]):
            pname, pbone = b["parents"][i]
            if pname is None or pname not in names:
                continue
            pbon = bon_of.get(pname)
            if pbon is None:
                continue
            bone = next((n for nid, n in bon_nodes(ents[pbon]) if nid == pbone), None)
            if bone is None:
                continue
            attached[model] = (pname, bone)
        for model, dff, uva in b["models"]:
            parts = [f'{{"{dff}", {json_str(bon_of.get(model))}}}']
            for cmodel, (pname, bone) in attached.items():
                if pname == model:
                    cdff = b["models"][names.index(cmodel)][1]
                    parts.append(f'{{"{cdff}", nullptr, "{bone}"}}')
            out.append(f'\t\t{{{a}, "{stem}:{model}", {{{", ".join(parts)}}}}},')
        out.append("\t},")
        if stem in TEXTURE_ARCHIVES:
            tarch = disc_path(TEXTURE_ARCHIVES[stem])
            txd = next(n for n in one_entries(f, files, tarch) if n.upper().endswith(".TXD"))
            out.append(f'\t{{{{"{tarch}"}}, {json_str(txd)}}},')
        else:
            out.append(f'\t{{{a}, {json_str(txd)}}},')
        out.append("\t{" + ", ".join(motion_keys) + "},")
        kind = "RingLine" if stem in RING_LINE_STEMS else "Variant" if stem in VARIANT_STEMS else "Single"
        names = [m[0] for m in b["models"]]
        variants = [[names.index(n) for n in models if n in names and n not in attached] for _, models in b["infos"]]
        entry = {}
        if kind != "Single":
            entry["layout"] = kind
        if variants:
            entry["variants"] = variants
        add_placement(stem, entry)
        out.append("\t{" + ", ".join(f'{{{a}, "{c}"}}' for c in b["collisions"]) + "}")
        out.append("};")

    bundles = {n: (ARCHIVE_ENTITIES[n]) for _, n in archive_used if n in ARCHIVE_ENTITIES}
    bundles.update({cname: (carch, cdff, cbon) for _, cname in archive_used for k, (cn, carch, cdff, cbon) in CHARACTERS.items() if cn == cname})
    for name, (arch, dff, bon) in sorted(bundles.items()):
        ents = one_entries(f, files, arch)
        for needed in (dff, bon):
            if needed not in ents:
                raise RuntimeError(f"{arch} has no {needed}")
        txd = next((n for n in ents if n.upper().endswith(".TXD")), None)
        ccls = [n for n in ents if n.upper().endswith(".CCL")]
        mtps = [n for n in ents if n.upper().endswith(".MTP")]
        a = f'{{"{arch}"}}'
        motion_keys = [f'{{{a}, "{m}", "{n}", "{bon}"}}' for m in mtps for n in idle_first(mtp_names(ents[m]))]
        out.append(f"inline const STH2005::EntityKey {ident(arch, name)}{{")
        out.append(f'\t"{name}",')
        out.append("\t{")
        extra = BUNDLE_PARTS.get(name, [])
        for part_dff, part_bone in extra:
            if part_dff not in ents:
                raise RuntimeError(f"{arch} has no {part_dff}")
            if part_bone not in [n for _, n in bon_nodes(ents[bon])]:
                raise RuntimeError(f"{bon} has no bone {part_bone}")
        parts = [f'{{"{dff}", "{bon}"}}'] + [f'{{"{d}", nullptr, "{b}"}}' for d, b in extra]
        out.append(f'\t\t{{{a}, "{name}:{dff}", {{{", ".join(parts)}}}}},')
        out.append("\t},")
        out.append(f'\t{{{a}, {json_str(txd)}}},')
        out.append("\t{" + ", ".join(motion_keys) + "},")
        out.append("\t{" + ", ".join(f'{{{a}, "{c}"}}' for c in ccls) + "}")
        add_placement(name, {"loopMotion": True})
        out.append("};")

    for st in stages:
        out.append("")
        out.append(f"inline const std::vector<STH2005::PlacedEntityKey> EntityTable_{st}{{")
        for t, key, parameter in tables[st]:
            out.append(f"\t{{{t}, {key}" + (f", {parameter}" if parameter is not None else "") + "},")
        out.append("};")
    open(OUT, "w").write("\n".join(out) + "\n")
    os.makedirs(os.path.dirname(PLACEMENT_OUT), exist_ok=True)
    with open(PLACEMENT_OUT, "w") as placement_file:
        json.dump(placement, placement_file, indent=1)
    print(f"{len(used) + len(bundles)} keys and {len(stages)} tables written to {OUT}")
    for st in stages:
        print(f"{st}: {len(tables[st])} types with a BIN; no BIN for: " + ", ".join(skipped.get(st, [])))


placement = {}


def add_placement(name, entry):
    """Records an object's placement data (Placement.json); two keys of one name must agree, since a prefab is named after the key."""
    if name in placement and placement[name] != entry:
        raise RuntimeError(f"two keys named {name} with different placement data")
    placement[name] = entry


def json_str(s):
    return "nullptr" if s is None else '"' + s + '"'


if __name__ == "__main__":
    main()
