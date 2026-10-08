"""
Generates Catalog/AssetCatalog.tsv from ResourceMap.txt: the models and textures of the two discs, one row each, for the
editor's Assets tab (src/EngineEditor/Panels/AssetPanel.cpp, loaded from Assets/Catalog/AssetCatalog.tsv, copied there by CMake).

Run from anywhere:  python3 tools/genAssetCatalog.py        (run tools/genResourceMap.py first if ResourceMap.txt is old)
Add --ai to print only the counts per game, kind and group.

Row: game <TAB> kind <TAB> group <TAB> name <TAB> source
  game   0 = SADX, 1 = STH2005
  kind   0 = model, 1 = texture
  group  0 Enemies, 1 NPCs, 2 Characters, 3 Static objects, 4 Interactible objects (the PrefabGroup order of Prefab.h)
  source SADX model: "<rel> @ 0x<offset>", with " (land table)" after it for a land table (the thumbnails skip those); SADX texture: disc file; Shadow: "<archive> > <entry>" or the loose disc file.
Models are the Ninja models and land tables of the .rel modules and the .DFF entries of the .ONE archives plus the loose
.dff files; textures are the .GVM/.GVR files and the .TXD entries plus the loose .txd/.png files. Everything else
(animations, bones, collision, audio, ...) is left out.

Every group is a hypothesis made from names (the same idea as groupOf in src/Objects/Prefab.cpp, whose name sets are
copied below); nothing was checked in the engine.
"""
import collections, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/"
ENEMIES, NPCS, CHARACTERS, STATIC, INTERACTIBLE = range(5)
GROUP_NAMES = ["Enemies", "NPCs", "Characters", "Static objects", "Interactible objects"]

lines = open(ROOT + "ResourceMap.txt", encoding="utf-8").read().split("\n")


def section(start):
    """Lines from the heading that starts with `start` to the next '== ' heading."""
    first = next(i for i, ln in enumerate(lines) if ln.startswith(start))
    last = next((i for i in range(first + 1, len(lines)) if lines[i].startswith("== ")), len(lines))
    return lines[first + 1:last]


def rows(block):
    for ln in block:
        if ln.startswith("  ") and " | " in ln:
            p = [x.strip() for x in ln.strip().split(" | ")]
            yield p[0], p[1], p[-1]


# ------------------------------------------------------------------ SADX
SADX_CHARACTER_REL = {"ModelsMiles", "ModelsKnuckles"}
SADX_CHARACTER_WORD = re.compile(r"^(Tails|Sonic|Metal|Big|Knuckles|E-102|Amy|miles_|knuckles_)", re.I)
SADX_NPC = re.compile(r"^(Boy|Girl|Woman|Man|Fat Man|Jungle Explorer|Echidna \(|Pachacamac|Turtle Bot|Maid Bot|Past Chao|Tikal|Seal|Penguin|Swallow|"
                      r"Peacock|Parrot|Koala|Wallaby|Deer|Rabbit|Mole|Elephant|Lion|Gorilla|Skunk|Otter)( |$)")
SADX_ENEMY = re.compile(r"^(E |E-\d|Buyon|Leon|Rhinotank|Egg Keeper|Kiki|Bomb|Spikeball|Boa-Boa|Spinner|Unidas|Gola|Sweeper|Cop Speeder|Ice Ball|Aircraft|"
                        r"Satellite|Space Ship)")
SADX_BOSS_REL = re.compile(r"^(_B_chaos|b_chaos|b_e101|b_egm|b_robo)", re.I)
SADX_INTERACTIBLE = re.compile(r"(ring|spring|switch|item ?box|capsule|checkpoint|save point|dash ?pad|jump ?panel|rocket|balloon|cannon|bumper|bomb box|"
                               r"shield|invincib|spikes?|emblem|spring)", re.I)


def sadxModelGroup(rel, name):
    if rel in SADX_CHARACTER_REL and SADX_CHARACTER_WORD.match(name):
        return CHARACTERS
    if SADX_BOSS_REL.match(rel) or SADX_ENEMY.match(name):
        return ENEMIES
    if SADX_NPC.match(name):
        return NPCS
    if SADX_INTERACTIBLE.search(name):
        return INTERACTIBLE
    return STATIC


SADX_TEXTURE_PATTERNS = [
    (r"^(sonic|son_|supersonic|metalsonic|miles|tails|knuckles|knu_|amy|am_|big|e102|e_102|endbg_)", CHARACTERS),
    (r"^(chaos|e101|egm|enemy|bosse101|er_|mobile)", ENEMIES),
    (r"^(tikal|npc|frog|shape_frog|big_kaeru)", NPCS),
    (r"^(item|ring|spring|switch|capsule|dash|obj_)", INTERACTIBLE),
]
SADX_TEXTURE_PATTERNS = [(re.compile(p, re.I), g) for p, g in SADX_TEXTURE_PATTERNS]


def sadxTextureGroup(name):
    for rx, group in SADX_TEXTURE_PATTERNS:
        if rx.match(name):
            return group
    return STATIC


# ------------------------------------------------------------------ Shadow
ONE_CHARACTERS = {"AMY", "CHARMY", "DOOMSEYE", "EGGMAN", "ESPIO", "KNUCKLES", "MARIA", "OMEGA", "ROUGE", "SONIC", "TAILS", "VECTOR", "SHADOW"}
ONE_INTERACTIBLE = {
    "BALLOON", "BLASTBREAK", "BOMB", "BREAKOBJ", "CAPTURECAGE", "CONTAINER", "CONTAINERHARD", "CONTAINERSECRET", "CONTAINERUNBREAK", "CONTAINERWEAPON", "DASHPANEL", "DASHRING", "FAN", "FRUIT",
    "GOALRING", "HEALINGUNIT", "HINTRING", "ITEMBOX", "JUMPBOARD", "LONGSPRING", "RING", "ROCKET", "SAVEPOINT", "SECRETDOOR", "SECRETKEY", "SETWEAPON", "SPRING", "SWITCH", "TANK", "TARGETSWITCH",
    "TURNOVER", "UPDOWNREEL", "WEIGHT", "FOOTINGMOVABLE", "CITYLASER", "FALLBUILDING",
}


def shadowGroup(archive, entry):
    stem = os.path.splitext(os.path.basename(entry))[0].upper()
    if stem.startswith(("GUN_", "BK_", "EGG_")):
        return ENEMIES
    if stem in ONE_CHARACTERS or stem.split("_")[0] in ONE_CHARACTERS:
        return CHARACTERS
    if stem in ONE_INTERACTIBLE:
        return INTERACTIBLE
    if archive.startswith("enemy/"):
        return ENEMIES
    if archive.startswith("character/"):
        return CHARACTERS
    return STATIC


# ------------------------------------------------------------------ rows
out = []


def add(game, kind, group, name, source):
    out.append((game, kind, group, name, source))


for name, what, path in rows(section("== Models, animations and land tables")):
    if not (what.startswith("model") or what.startswith("land table")):
        continue
    rel = path.split(" @ ")[0].replace(".rel", "")
    if what.startswith("land table"):
        add(0, 0, STATIC, name, path + " (land table)")
    else:
        add(0, 0, sadxModelGroup(rel, name), name, path)

for name, what, path in rows(section("== Textures (")):
    if what.startswith("GVM") or what.startswith("GVR"):
        add(0, 1, sadxTextureGroup(name), path, path)

in_shadow = False
current = None
for ln in lines[next(i for i, l in enumerate(lines) if l.startswith("== .ONE archives")):]:
    if ln.startswith("== ") and not ln.startswith("== .ONE archives"):
        break
    m = re.match(r"-- (\S+) \(One Ver", ln)
    if m:
        current = m.group(1)
        continue
    if current and " | " in ln:
        entry = ln.strip().split(" | ")[0].strip()
        ext = entry.rsplit(".", 1)[-1].upper()
        if ext == "DFF":
            add(1, 0, shadowGroup(current, entry), entry, f"{current} > {entry}")
        elif ext == "TXD":
            add(1, 1, shadowGroup(current, entry), entry, f"{current} > {entry}")

for head, kind in (("== Models (loose)", 0), ("== Textures (loose)", 1)):
    for name, what, path in rows(section(head)):
        if os.path.splitext(path)[1].lower() in (".dff", ".txd", ".png"):
            add(1, kind, shadowGroup("", path), path, path)

if "--ai" in sys.argv:
    count = collections.Counter((g, k, grp) for g, k, grp, _, _ in out)
    for (g, k, grp), n in sorted(count.items()):
        print(f"{'SADX' if g == 0 else 'STH2005'} {'model' if k == 0 else 'texture'} {GROUP_NAMES[grp]}: {n}")
    print(f"total {len(out)}")
else:
    os.makedirs(ROOT + "Catalog", exist_ok=True)
    with open(ROOT + "Catalog/AssetCatalog.tsv", "w", encoding="utf-8") as f:
        for g, k, grp, name, source in out:
            f.write(f"{g}\t{k}\t{grp}\t{name}\t{source}\n")
    print(f"wrote Catalog/AssetCatalog.tsv: {len(out)} rows")
