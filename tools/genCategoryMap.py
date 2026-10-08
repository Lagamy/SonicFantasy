"""
Generates ResourceMapByCategory.txt in the project root from ResourceMap.txt: the same assets, grouped per game
into categories (characters, levels, bosses, ...) with the files that belong to each (models, textures, bones,
animations, collision, audio).

Run from anywhere:  python3 tools/genCategoryMap.py
Run tools/genResourceMap.py first if ResourceMap.txt is missing or old.

Every grouping below is a hypothesis made from file names, texture hints and entry types (the labels of
ResourceMap.txt are kept on the per-type headings); nothing was checked in the engine.
"""
import collections, os, re, textwrap

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/"
WIDTH = 118

# ------------------------------------------------------------------ SADX grouping tables
# rel module -> (category, subject). A subject of None means the rows are split one by one (see sadxRowGroup).
SADX_REL = {
    "a_mot": ("Playable characters", "Amy"),
    "b_mot": ("Playable characters", "Big"),
    "e_mot": ("Playable characters", "E-102 Gamma"),
    "k_mot": ("Playable characters", "Knuckles"),
    "s_mot": ("Playable characters", "Sonic"),
    "s_sbmot": ("Playable characters", "Sonic"),
    "m_sbmot": ("Playable characters", "Tails"),
    "ModelsMiles": ("Playable characters", None),
    "ModelsKnuckles": ("Playable characters", None),
    "_Adv00": ("Adventure fields (hubs)", "Station Square"),
    "_Adv0100": ("Adventure fields (hubs)", "Egg Carrier, exterior/deck"),
    "_Adv0130": ("Adventure fields (hubs)", "Egg Carrier, interior"),
    "_Adv02": ("Adventure fields (hubs)", "Mystic Ruins"),
    "_Adv03": ("Adventure fields (hubs)", "Past (Echidna village)"),
    "_Stg01": ("Action stages", "Stage 01 (Emerald Coast)"),
    "_Stg02": ("Action stages", "Stage 02 (Windy Valley)"),
    "_Stg03": ("Action stages", "Stage 03 (Twinkle Park)"),
    "_Stg04": ("Action stages", "Stage 04 (Speed Highway)"),
    "_Stg05": ("Action stages", "Stage 05 (Red Mountain)"),
    "_Stg06": ("Action stages", "Stage 06 (Sky Deck)"),
    "_Stg07": ("Action stages", "Stage 07 (Lost World)"),
    "_Stg08": ("Action stages", "Stage 08 (Ice Cap)"),
    "_Stg09": ("Action stages", "Stage 09 (Casinopolis)"),
    "_Stg10": ("Action stages", "Stage 10 (Final Egg)"),
    "_Stg12": ("Action stages", "Stage 12 (Hot Shelter)"),
    "_B_chaos6": ("Bosses", "Chaos 6"),
    "_B_chaos7": ("Bosses", "Perfect Chaos"),
    "b_chaos0": ("Bosses", "Chaos 0"),
    "b_chaos2": ("Bosses", "Chaos 2"),
    "b_chaos4": ("Bosses", "Chaos 4"),
    "b_e101": ("Bosses", "E-101 Beta"),
    "b_e101_r": ("Bosses", "E-101 mkII"),
    "b_robo": ("Bosses", "E-101 mkII arena"),
    "b_egm1": ("Bosses", "Egg Hornet"),
    "b_egm2": ("Bosses", "Egg Walker"),
    "b_egm3": ("Bosses", "Egg Viper"),
    "_Minicart": ("Mini-games", "Twinkle Circuit (cart)"),
    "_Shooting": ("Mini-games", "Sky Chase (shooting)"),
    "sboard": ("Mini-games", "Sand Hill (sandboard)"),
    "stg00": ("Mini-games", "Hedgehog Hammer (whack-a-mole)"),
    "_Main": (None, None),
    "Advertise": ("Menus and system", "Advertise (title, menus, character select)"),
}

# first word of a row name -> character, for the rels that hold several characters
SADX_CHAR_WORD = {
    "Tails": "Tails", "miles_face_m_head": "Tails", "m_itembody_m_itembody": "Tails",
    "Sonic": "Sonic", "Metal": "Sonic (incl. Metal Sonic)",
    "Big": "Big", "Knuckles": "Knuckles", "knuckles_face_k_head": "Knuckles",
    "E-102": "E-102 Gamma", "Amy": "Amy",
}

SADX_NPC = re.compile(r"^(Boy|Girl|Woman|Man|Fat Man|Jungle Explorer|Echidna \(|Pachacamac|Turtle Bot|Maid Bot|Past Chao)")
SADX_ANIMALS = re.compile(r"^(Seal|Penguin|Swallow|Peacock|Parrot|Koala|Wallaby|Deer|Rabbit|Mole|Elephant|Lion|Gorilla|"
                          r"Skunk|Otter)( |$)")
SADX_ENEMY = re.compile(r"^(Buyon|E-100|Leon|Rhinotank|Egg Keeper|Kiki|Bomb|Spikeball|Boa-Boa|Spinner|Unidas|Gola|"
                        r"Sweeper|Cop Speeder|Ice Ball|Aircraft|Satellite|Space Ship|Head Wobble|Belly Wobble|"
                        r"Middle [AB] Wobble|Look Around|Move (Fast|Slow))")

# texture archive / audio name -> subject, first match wins. Names are matched in lower case.
SADX_PATTERNS = [
    (r"^(sonic|son_|supersonic|metalsonic|ava_metal|endbg_(super)?sonic|endbg_last_(super)?sonic|alife_sonic|"
     r"advsonic|thesonic|sprsonic|p_lineartest_sn|(alt|casino|v|p)_sonictails|sonictails)", ("Playable characters", "Sonic")),
    (r"^(miles|tails|endbg_tails|endbg_last_tails|advmiles|themiles|alife_tails|p_metaltails)", ("Playable characters", "Tails")),
    (r"^(knuckles|knu_|endbg_(last_)?knuckles|advknkls|theknkls|alife_knuckles|p_knuckles|p_lineartest_kn|"
     r"alt_knuckles|casino_knuckles|v_knuckles)", ("Playable characters", "Knuckles")),
    (r"^(amy|am_|endbg_(last_)?amy|advamy|theamy|alife_amy|p_amy|v_amy)", ("Playable characters", "Amy")),
    (r"^(big|endbg_(last_)?big|advbig|thebig|alife_big|p_big|v_big|fishing|fishget|fishmiss)", ("Playable characters", "Big")),
    (r"^(e102|e_102|endbg_(last_)?e102|adve102|thee102|alife_e102|p_e102|v_e102|alt_e102)", ("Playable characters", "E-102 Gamma")),
    (r"^(beach|bg_beach|am_sea|emeraldcoast|ecoast)", ("Action stages", "Stage 01 (Emerald Coast)")),
    (r"^(windy|wndyvly|wing_|zou)", ("Action stages", "Stage 02 (Windy Valley)")),
    (r"^(_twinkle|twinkle|twnklpk|eff_twinkle)", ("Action stages", "Stage 03 (Twinkle Park)")),
    (r"^(bg_highway|highway)", ("Action stages", "Stage 04 (Speed Highway)")),
    (r"^(redmntn|bg_ruin)", ("Action stages", "Stage 05 (Red Mountain)")),
    (r"^skydeck", ("Action stages", "Stage 06 (Sky Deck)")),
    (r"^(lostworld|lstwrld)", ("Action stages", "Stage 07 (Lost World)")),
    (r"^(icecap|bg_icecap)", ("Action stages", "Stage 08 (Ice Cap)")),
    (r"^(casino[0-9]?$|casinopolis)", ("Action stages", "Stage 09 (Casinopolis)")),
    (r"^(finalegg|finaleg|eff_finalegg)", ("Action stages", "Stage 10 (Final Egg)")),
    (r"^(hotshelter|shelter)", ("Action stages", "Stage 12 (Hot Shelter)")),
    (r"^(_advss|advss|ss_|stationsquare|s_square|titl_ss)", ("Adventure fields (hubs)", "Station Square")),
    (r"^(adv_ec|_obj_ec|ec_|bg_ec|eggcarrier|egcarer|garden01ec)", ("Adventure fields (hubs)", "Egg Carrier (all)")),
    (r"^(adv_mr|mr_|mysticruin|titl_mr|alt_mysticruin|tanken)", ("Adventure fields (hubs)", "Mystic Ruins")),
    (r"^(eff_past|past_)", ("Adventure fields (hubs)", "Past (Echidna village)")),
    (r"^(chaos0|chaos_0|ev_chaos0)", ("Bosses", "Chaos 0")),
    (r"^chaos2", ("Bosses", "Chaos 2")),
    (r"^chaos4", ("Bosses", "Chaos 4")),
    (r"^(chaos6|chaos_6)", ("Bosses", "Chaos 6")),
    (r"^(chaos7|eff_c7)", ("Bosses", "Perfect Chaos")),
    (r"^(e101r|e101_r|er_9000|erobo)", ("Bosses", "E-101 mkII")),
    (r"^(e101|bosse101|ev_e101)", ("Bosses", "E-101 Beta")),
    (r"^(egm1|mobile1)", ("Bosses", "Egg Hornet")),
    (r"^(egm2|egm2_|mobile2)", ("Bosses", "Egg Walker")),
    (r"^(egm3|mobile3|eggmbl23)", ("Bosses", "Egg Viper")),
    (r"^(chao|al_|garden|ev_alife|alife$|cream|c_select)", ("Chao", "Chao (all)")),
    (r"^(cart|shooting|eff_sandboard|bg_sandboard|sandhill|ssracing)", ("Mini-games", "Mini-games (shared names)")),
    (r"^(ev_|icm|edv_|tikal|er_)", ("Cutscenes", "Cutscene textures and audio (ev_*, icm*)")),
    (r"^(mini_cart|score_cart|score_shoot|score_mole|score_board|sandboard|minigame|miniprf)",
     ("Mini-games", "Mini-games (shared names)")),
    (r"^(frog|shape_frog|f0000_frog|big_kaeru)", ("Playable characters", "Big")),
    (r"^(tuto|smrybg|score_|mission|map_|gg_|ggmenu|segalogo|tgs_|hint|loading|nowload|pressstart|gc_vms|tvsetting|"
     r"tx_chnam|m_chnam|[abekm]_stage|action_|boss_|m_em_|timeover|mi_)",
     ("Menus and system", "Menus, title, jingles")),
    (r"^(ava_|b32ascii|b_chnam|title|titl_|option|continue|gameover|jingle|extra|con_regular|abc_txt|emblem)",
     ("Menus and system", "Menus, title, jingles")),
]
SADX_PATTERNS = [(re.compile(p), g) for p, g in SADX_PATTERNS if p]


def sadxRowGroup(rel, name, what):
    """(category, subject) of one SADX rel row."""
    cat, subj = SADX_REL.get(rel, (None, None))
    if rel.startswith("ChaoStg"):
        return "Chao", rel[3:] + " (garden/race/market stage)"
    if rel in ("ModelsMiles", "ModelsKnuckles"):
        word = name.split(" ")[0]
        return cat, SADX_CHAR_WORD.get(word, SADX_CHAR_WORD.get(name, "Unnamed (offset only) / other"))
    if rel == "_Main":
        if SADX_ANIMALS.match(name):
            return "Animals", "Freed animals (Animal Bubble contents)"
        if SADX_ENEMY.match(name):
            return "Enemies", "Enemies in _Main.rel (shared by stages)"
        return "Common objects", "Common objects in _Main.rel (rings, springs, items, shields, debug, ...)"
    if cat == "Adventure fields (hubs)" and SADX_NPC.match(name):
        return "NPCs", subj + " NPCs"
    return cat, subj


def sadxUnmappedGroup(name):
    if name.startswith("Chao"):
        return "Chao", "Chao modules (not mapped)"
    if name.startswith("ev"):
        return "Cutscenes", "ev*.rel cutscene modules (not mapped)"
    return "Menus and system", "Other modules (not mapped)"


# ------------------------------------------------------------------ parsing
def rows(lines):
    for ln in lines:
        if ln.startswith("  ") and " | " in ln:
            p = [x.strip() for x in ln.strip().split(" | ")]
            if len(p) >= 3:
                yield p[0], " | ".join(p[1:-1]), p[-1]


def section(lines, start, stop=None):
    """Lines after the first line starting with `start` until a line starting with `stop` (or a blank-separated '==')."""
    out, on = [], False
    for ln in lines:
        if not on:
            on = ln.startswith(start)
            continue
        if ln.startswith("== ") or ln.startswith("####"):
            break
        out.append(ln)
    return out


lines = open(ROOT + "ResourceMap.txt", encoding="utf-8").read().split("\n")
shadowAt = next(i for i, l in enumerate(lines) if l.startswith("# Shadow the Hedgehog"))
sadx, shadow = lines[:shadowAt], lines[shadowAt:]

out = []
def w(s=""): out.append(s)

def wrapped(items, indent, sep=", "):
    w(textwrap.fill(sep.join(items), WIDTH, initial_indent=indent, subsequent_indent=indent + "  ",
                    break_long_words=False, break_on_hyphens=False))

def emit(game, groups, order):
    """groups: category -> subject -> kind -> [str]"""
    w("#" * 100)
    w(f"# {game}")
    w("#" * 100)
    w()
    cats = [c for c in order if c in groups] + sorted(c for c in groups if c not in order)
    for c in cats:
        n = sum(len(v) for s in groups[c].values() for v in s.values())
        w(f"=== {c} ({len(groups[c])} items, {n} files) ===")
        for s in sorted(groups[c], key=natural):
            w()
            w(f"  {s}")
            for kind, items in groups[c][s].items():
                if not items:
                    continue
                w(f"    {kind} ({len(items)}):")
                wrapped(items, "      ")
        w()
    w()

def natural(s):
    return [int(t) if t.isdigit() else t.lower() for t in re.split(r"(\d+)", s)]

def newGroups():
    return collections.defaultdict(lambda: collections.defaultdict(lambda: collections.defaultdict(list)))


# ------------------------------------------------------------------ SADX
sg = newGroups()
sadxLines = sadx

mapped = []
cur = None
for ln in section(sadxLines, "== Models, animations and land tables"):
    m = re.match(r"-- (.+)\.rel \(\d+\) --", ln)
    if m:
        cur = m.group(1)
        continue
    if cur is None or " | " not in ln:
        continue
    name, what, path = rows([ln]).__next__()
    mapped.append((cur, name, what, path))

gvmUse = collections.defaultdict(set)
for rel, name, what, path in mapped:
    cat, subj = sadxRowGroup(rel, name, what)
    if cat is None:
        cat, subj = "Unassigned", rel + ".rel"
    off = path.split(" @ ")[-1] if " @ " in path else path
    if what.startswith("model"):
        kind = f"Models [{rel}.rel]"
    elif what.startswith("animation"):
        kind = f"Animations [{rel}.rel]"
    elif what.startswith("land table"):
        kind = f"Level geometry + collision (land tables) [{rel}.rel]"
    else:
        kind = f"Other [{rel}.rel]"
    sg[cat][subj][kind].append(f"{name} @{off}")
    for t in re.findall(r"-> (\S+\.GVM)", what, re.I):
        gvmUse[t.lower()].add((cat, subj))

unmapped = section(sadxLines, "== .rel modules not mapped")
for name, what, path in rows(unmapped):
    cat, subj = sadxUnmappedGroup(name)
    sg[cat][subj]["Code+data modules, contents unknown"].append(path)

def place(name, path, kind, hinted):
    """Texture or audio: a texture archive referenced by rel rows goes to the group that references it."""
    use = hinted.get(path.lower())
    if use and len(use) == 1:
        cat, subj = next(iter(use))
        return cat, subj
    for rx, g in SADX_PATTERNS:
        if rx.match(name.lower()):
            return g
    if use:
        return "Shared (referenced by several groups)", ", ".join(sorted(s for _, s in use))
    return "Unassigned", "Not matched to any group"

kindOf = {
    ".gvm": "Textures (GVM archives)", ".gvr": "Textures (GVR single)", ".nb": "Models (Ninja binary)",
    ".adx": "Audio (ADX music/jingles/voice streams)", ".mlt": "Audio (MLT sound banks)",
}
for head, sect in (("Textures", "== Textures"), ("Models", "== Models (Ninja binary)"), ("Audio", "== Audio")):
    for name, what, path in rows(section(sadxLines, sect)):
        ext = os.path.splitext(path)[1].lower()
        kind = kindOf.get(ext, head + f" ({ext})")
        cat, subj = place(name, path, kind, gvmUse)
        sg[cat][subj][kind].append(path)

emit("Sonic Adventure DX (GameCube)", sg,
     ["Playable characters", "NPCs", "Enemies", "Bosses", "Animals", "Common objects", "Adventure fields (hubs)",
      "Action stages", "Mini-games", "Chao", "Cutscenes", "Menus and system", "Shared (referenced by several groups)",
      "Unassigned"])

# ------------------------------------------------------------------ Shadow
hg = newGroups()
TYPE_KIND = {
    "DFF": "Models (.DFF)", "BSP": "Level geometry (.BSP)", "TXD": "Textures (.TXD)", "BON": "Bones (.BON)",
    "MTP": "Animations: motion packages (.MTP)", "MTN": "Animations: motions (.MTN)",
    "UVA": "Animations: UV (.UVA)", "DMA": "Animations: morph (.DMA)", "SNB": "Animations: camera (.SNB)",
    "CCL": "Collision (.CCL)", "EFD": "Effect data (.EFD)",
}

def shadowKind(entry):
    ext = entry.rsplit(".", 1)[-1].upper()
    if ext == "BSP" and "COLI" in entry.upper():
        return "Collision? (.BSP with COLI in the name) [hyp]"
    return TYPE_KIND.get(ext, f"Other / unknown (.{ext})")

archives = []
cur = None
for ln in section(shadow, "== .ONE archives"):
    m = re.match(r"-- (\S+) \(One Ver ([\d.]+), (\d+) entries\) --", ln)
    if m:
        cur = m.group(1)
        archives.append(cur)
        continue
    if cur and " | " in ln:
        name, what, path = rows([ln]).__next__()
        entry = path.split(" > ", 1)[1]
        base = os.path.basename(cur)[:-4]
        stg = re.match(r"stg(\d{4})", base, re.I)
        ev = re.match(r"event(\d{4})", base, re.I)
        top = cur.split("/")[0]
        if stg:
            n = stg.group(1)
            cat, subj = "Levels (stgNNNN)", f"stg{n}"
            tag = base[len(stg.group(0)) + 1:] or "main"
            label = f"{entry}@{tag}"
        elif ev:
            cat, subj, label = "Cutscenes (eventNNNN)", f"event{ev.group(1)}", f"{entry}@{base[9:]}"
        elif top == "character":
            cat, subj, label = "Characters", base, entry
        elif cur.startswith("enemy/boss/"):
            cat, subj, label = "Bosses", base, entry
        elif top == "enemy":
            cat, subj, label = "Enemies", base, entry
        else:
            cat, subj, label = "Common / shared archives", cur, entry
        hg[cat][subj][shadowKind(entry)].append(label)

# loose files and audio: stage-numbered ones join their stage, the rest go to shared groups
SHADOW_AUDIO = [
    (r"^(b3_shadow|audiodataplayer(hero|dark)?$)", ("Characters", "shadow")),
    (r"^(b4_subhero|b5_subdark)", ("Characters", "Audio banks for the other characters (hero/dark side)")),
    (r"^(b6_enemy|audiodataenemy)", ("Enemies", "Enemy audio (shared)")),
    (r"^(b2_weapons|audiodataweapon|audiodatagadget)", ("Common / shared archives", "Weapon and gadget audio")),
    (r"^(b7[b-f]_|b80_|b81_|audiodatastage\w*boss)", ("Bosses", "Boss audio banks (shared)")),
    (r"^(b7[1-9]_|audiodatastage)", ("Levels (stgNNNN)", "Level audio banks (not tied to one stgNNNN)")),
]
SHADOW_AUDIO = [(re.compile(p), g) for p, g in SHADOW_AUDIO]

def stageOf(s):
    m = re.search(r"(?:stg|StageTitle|pause)(\d{4})", s, re.I)
    return f"stg{m.group(1)}" if m else None

def eventOf(s):
    m = re.search(r"(?:sng_E?V?|EV)(\d{4})", s, re.I)
    return f"event{m.group(1)}" if m else None

for head, sect in (("Models", "== Models (loose)"), ("Textures", "== Textures (loose)"), ("Audio", "== Audio")):
    for name, what, path in rows(section(shadow, sect)):
        ext = os.path.splitext(path)[1].lower()
        if head == "Audio":
            kind = {".adx": "Audio: music/songs (.adx)", ".mlt": "Audio: sound banks (.mlt)",
                    ".bin": "Audio: cue tables (.bin)", ".afs": "Audio: voice archives (.afs)"}.get(ext, f"Audio ({ext})")
        else:
            kind = {".txd": "Textures (.TXD)", ".dff": "Models (.DFF)", ".png": "Textures (.png)"}.get(ext, f"{head} ({ext})")
        sk, ek = stageOf(path), eventOf(path)
        if sk and "stg" in path.lower() and not name.lower().startswith("sng_ev"):
            hg["Levels (stgNNNN)"][sk][kind + (" [UI/fonts/audio]" if head != "Models" else "")].append(path)
        elif ek and name.lower().startswith(("sng_ev", "sng_e")) and re.match(r"sng_e", name.lower()):
            hg["Cutscenes (eventNNNN)"][ek][kind].append(path)
        elif head == "Audio" and any(rx.match(name.lower()) for rx, _ in SHADOW_AUDIO):
            cat, subj = next(g for rx, g in SHADOW_AUDIO if rx.match(name.lower()))
            hg[cat][subj][kind].append(path)
        elif head == "Audio":
            sub = "Audio: stage and player cue tables, sound banks" if ext in (".bin", ".mlt") else \
                  "Audio: music, jingles, voice (not tied to a stage or event by name)"
            hg["Shared audio"][sub][kind].append(path)
        elif head == "Textures":
            top = path.split("/")[0] if "/" in path else "(root)"
            hg["UI, fonts and system textures"][top][kind].append(path)
        else:
            hg["Common / shared archives"]["Loose models"][kind].append(path)

emit("Shadow the Hedgehog (GameCube)", hg,
     ["Characters", "Enemies", "Bosses", "Levels (stgNNNN)", "Cutscenes (eventNNNN)", "Common / shared archives",
      "UI, fonts and system textures", "Shared audio"])

head = [
    "SonicSunset resource map, grouped by category",
    "===============================================",
    "Generated by tools/genCategoryMap.py from ResourceMap.txt. Same assets, regrouped per game.",
    "",
    "All grouping is hypothesis: it comes from file names, the texture hints in ResourceMap.txt, entry types and",
    "name patterns in tools/genCategoryMap.py. Stage names in parentheses for SADX are from the usual stage order and",
    "the texture names [hyp]. Shadow stage/event ids are left as the disc's own numbers; no names are assumed.",
    "Per-type headings keep the file type; see ResourceMap.txt for the evidence label of each type.",
    "",
    "Entry notation:",
    "  SADX models/animations/land tables:  name @0xOFFSET        offset inside the .rel named in the heading",
    "  SADX textures/audio:                 file on the disc",
    "  Shadow:                              ENTRY.EXT             inside the archive named by the item",
    "                                       ENTRY.EXT@x           stage archives: stgNNNN/stgNNNN_x.one > ENTRY.EXT",
    "                                                             (x = 'main' when the archive name has no suffix)",
    "                                       path                  loose files: file on the disc",
    "",
]
open(ROOT + "ResourceMapByCategory.txt", "w", encoding="utf-8").write("\n".join(head + out) + "\n")
print("wrote ResourceMapByCategory.txt", len(head) + len(out), "lines")
