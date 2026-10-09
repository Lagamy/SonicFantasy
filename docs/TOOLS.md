# Tools

What every script in `tools/` is for, how to run it, what it reads and what it writes. Each script's own docstring (the top of the file) is the full reference, including the evidence behind its formats; this page is the overview. The compact version for Claude is the `project-tools` skill (`.claude/skills/project-tools/SKILL.md`); a change to a tool's arguments, flags or output updates both in the same task, and the replay tools are also described in `docs/REPLAY_COMPARISON.md`.

Run the Python tools from the project root with `python3`, except `shadowLiveLog.py`, which needs the venv (`.venv/bin/python`). Tools that write a report for reading print two forms: a human one (the default) and a compact one for Claude (`--ai`, or the digest tool).

## Comparing the Shadow port with the original

The loop: record the original in Dolphin, turn the recording into a replay, run the replay in the port, compare. The method, its limits and the list of recordings are in `docs/REPLAY_COMPARISON.md`.

### shadowLiveLog.py: record the original (you run it, Claude never does)

```
.venv/bin/python tools/shadowLiveLog.py [out.csv] [--seconds N] [--pad HEX]
.venv/bin/python tools/shadowLiveLog.py --find-pad
```

Reads the running game in Dolphin (through `dolphin-memory-engine`) and writes one CSV row each time the player's position or velocity changes. Stop with Ctrl+C. It overwrites `out.csv`.

- `--pad HEX` also logs the pad (sticks and buttons) from that address; the current one is `8056ED4C`. A recording needs `--pad` to become a replay.
- `--find-pad` searches for the pad's address and prints it.
- The camera is always logged (since 2026-10-06): `cam_eye_x/y/z` and `cam_target_x/y/z`, the eye and the look-at point of the player's camera, found through the camera manager at `0x8056ee14`. The cells are empty when that chain does not lead to a camera. The old `--camera` and `--find-camera` flags are gone.
- The last column `vi_count` (since 2026-10-07) is the u32 at `0x805f1024`, read when the row is written. Hypothesis from the decompile of `FUN_8037d434` (the video interrupt handler), not yet checked live: the retrace count, +1 per video frame, so +1 per game tick. Check a new recording: it should rise by 1 per distinct position, and then ticks can be aligned by it.

### shadowEffectLog.py: record the original's ball effects (you run it, Claude never does)

```
.venv/bin/python tools/shadowEffectLog.py [out.csv] [--seconds N] [--no-mem2] [--vtable HEX] [--range START-END]     (default out: tools/shadow_effect.csv)
python3 tools/shadowEffectLog.py --digest out.csv
```
Dolphin running, Shadow in a stage; start it before the jump, Ctrl+C after (overwrites the CSV; reads only, can run next to `shadowLiveLog.py`). It finds the effect objects of the model-effect class (vtable word 0x8053c934, FUN_801d1238) with a background memory scan, tells Jump_ballA / Jump_ballB from other model effects by the EFD record address in the object, and writes one row per object per change: the player's behavior and pose, the animation object's ball handle slots, joint 0's node, the current behavior's first 0x70 bytes, and the whole object (91 words) plus its colour floats. `--digest` writes `out.ai.txt` (compact) and `out.txt` (readable) with each object's life, the behaviors around it, its colour / alpha track and which words changed (a counter shows as one that only goes up). Hypothesis, not run yet: the vtable word and the record matching may need `--vtable` or a second recording. Layout and what is known of the effect: `docs/STH2005_PLAYER_REFERENCE.md` section 21.

### sadxLiveLog.py: record the original SADX Sonic (you run it, Claude never does; added 2026-10-07, not run yet)

```
.venv/bin/python tools/sadxLiveLog.py [out.csv] [--seconds N] [--buttons HEX]
.venv/bin/python tools/sadxLiveLog.py --find-buttons
```

The SADX counterpart of `shadowLiveLog.py` (Dolphin running SADX GC, Sonic in a stage; same venv, same Ctrl+C, overwrites `out.csv`). It finds Sonic's task itself (three function pointers side by side) and writes one row per change of his position, velocity, state or stick. Core columns keep the Shadow names (`frame, t, behavior, substate, pos_*, rot_*, vel_*, planar, speed3d, pad_buttons, vi_count`) so the replay tools can read both. Values are as read: `vel_*`, `planar`, `speed3d`, `fwd_speed` are per game frame, `rot_*` are 16 bit angles. Layout and the evidence for it: `docs/SADX_SONIC_REFERENCE.md`.

- The game's own processed stick is always logged: `stick_angle` (world direction, camera already applied) and `stick_mag`. Hypothesis: a replay can feed that directly, no camera and no raw pad needed for the stick.
- `--buttons HEX` also logs the u32 at that address as `pad_buttons` (the button word; the replay's jump needs it). Its address is not known yet: `--find-buttons` finds it (release everything, hold A, then tap A; it prints candidate addresses with the bit mask of A).
- The camera (`cam_rot_x/y`, `cam_x/y/z`) comes from the pointer at `0x808B3D10` (hypothesis); the cells are empty when it is null.
- `vi_count` is the u32 at `0x800C44E4` (the VI retrace count). Whether SADX runs one game frame per retrace is not known: a first recording shows it.
- Raw blocks for decoding later: `w1_raw`, `w2_raw`, `w3_raw`, `col_raw`, `stick_raw` (words, hex).

### makeReplay.py: recording to replay, and the camera check

```
python3 tools/makeReplay.py tools/<name>.csv Scripts/shadow/replays/<name>.lua
python3 tools/makeReplay.py tools/<name>.csv --check-camera [--ai]
sh tools/regenReplays.sh [name ...]   # all nine replays from tools/live_<name>.csv (or only the names given)
```

The first form writes a Lua replay: the start position, the starting heading and, per row, the time, the stick, the jump button and phi (the camera's heading). phi comes from the camera columns when the recording has them, otherwise (older recordings) from the player's world-space stick vector. It prints which source it used. Then add the replay to `Scripts/shadow/main.lua` and to `ORDER` in `Scripts/shadow/replay.lua`.

`--check-camera` writes nothing. It prints, for each row that has both, phi from the camera next to phi from the stick vector and their difference, with the 3D speed, the ground's slope and a `fast` mark (3D speed 250 or more, where the two are expected to differ on slopes). Its summary also tries the camera of one frame earlier, to tell whether the game uses this frame's camera or the last one. `--ai` prints the summary and only the rows more than 1 degree apart.

### compareReplay.py: port against original

```
python3 tools/compareReplay.py tools/<name>.csv tools/port_run.txt <name> [step_seconds] [--ai]
```

Compares the port's last replay called `<name>` (the `REPLAY` lines in its stdout, saved to `tools/port_run.txt`) with the recording: planar speed, vertical speed, height, path drift, the port's heading against the original's facing, and the original's behavior next to the port's motion, every `step_seconds` (default 0.1). `--ai` gives one summary line and only the port's differences, quiet stretches collapsed. The port's position of a row is taken from the next row (a REPLAY line's position is pre-move, its velocity post-behaviours; the original's row has both after the move).

### digestRun.py: read one run frame by frame

```
python3 tools/digestRun.py tools/<name>.csv [--from T] [--to T] [--every N] [--contacts [--words OFFSETS]] [--absolute]
python3 tools/digestRun.py tools/port_run.txt <name> [--from T] [--to T] [--every N]
```

Recording only: `--contacts` adds a line under each row with the flag words +0xa0 / +0xa4, the set bits of +0xa8, and the words of the hit block (+0x68..+0xa0) and ground block (+0x170..+0x1f0) that changed since the row before (use a short time window); `--words 1c8,1cc,a0` keeps only the listed offsets (hex) in that list. `--absolute` prints positions as recorded instead of from the first row (still in the port's axes: the game's level position is (-z, y, x) of the printed one).

A readable per-frame view of a recording or of the port's replay, both in the port's axes so the two line up column by column. The recording's raw memory columns are decoded (stick, flags, speeds); duplicate rows are dropped. Recordings with the camera columns get a `cam` column (the camera's heading in degrees). The port's digest ends with the wall flag and the wall normal (`wall wx wy wz`, port axes). Use this instead of opening the CSV.

### dtm.py: Dolphin movies

```
python3 tools/dtm.py dump run.dtm [first [count]]
python3 tools/dtm.py synth template.dtm out.dtm "60:128,128" "120:128,255,A" ...
```

Prints a Dolphin movie's header and inputs, or writes a new movie with given stick and button segments over a template recorded from the same savestate. The file layout is from memory of the Dolphin wiki and was not checked on a file of the Dolphin build used here.

## Disc and resource generators

These read the discs in `build/Disks/` and write files. Run them only when the generated output should change.

| Script | Writes | What |
|---|---|---|
| `genResourceMap.py` | `ResourceMap.txt` | Models, textures, audio, animations and collision on both discs, with paths and offsets. Downloads SA Tools INIs into `tools/.cache/` on first run. |
| `genAssetCatalog.py` | `Catalog/AssetCatalog.tsv` | The models and textures of both discs, one row each (game, kind, group, name, source), for the editor's Assets tab; CMake copies it to `Assets/Catalog`. Groups guessed from names. Run `genResourceMap.py` first. `--ai` prints only the counts per game, kind and group instead of writing. |
| `genCategoryMap.py` | `ResourceMapByCategory.txt` | The same, grouped per game into categories (grouping guessed from names). Run `genResourceMap.py` first. |
| `genEntityKeys.py [stage ...]` | `src/STH2005EntityKeys.h`, `External/Prefabs/STH2005/Placement.json` | Shadow object keys (the layout, variants and loop flag go to the JSON) and per-stage placement tables (default stage `stg0100`). Reads `tools/data/STH2005Objects.json` (hand-written: objects with an archive of their own, the SET_CHARACTOR characters and the extra bundle parts, with the evidence). |
| `genCCLKeys.py` | `src/STH2005ShapeKeys.h` | One collision key per `.CCL` entry on the Shadow disc. |
| `genSAEntityKeys.py` | `src/SAEntityKeys.h`, `External/Prefabs/SADX/Placement.json` | SADX Emerald Coast object keys and tables (the rotation recipes and variant field go to the JSON). Reads `tools/data/SADXObjectModels.json` (hand-written: objects with no model, and the object-to-model-name choices, with the evidence). |
| `genSAShapeKeys.py` | `src/SAShapeKeys.h` | SADX object collision tables; needs the `sadxDecomp/` pseudocode. |

`dumpCCL.py` reads Shadow `.CCL` collision files: `[outDir]` dumps them all (a summary only without it), `-x NAME` hex dump, `-p NAME` parsed, `--check` lists the entries that break the known layout.

## RAM-dump readers (read only)

- `sadxLiveTasks.py [mem1.raw]`: the live SET-object tasks of an SADX dump (Emerald Coast act 1) and their exec, display and destroy functions.
- `sadxLiveCollision.py [mem1.raw]`: the live collision entries of those tasks.

Both default to `sadxDecomp/mem1.raw` (Dolphin: Memory window, Dump MRAM).

## Decompiles

`decompFunc.py` shows one function of the Shadow decompile without opening the 850,000-line file:

```
python3 tools/decompFunc.py FUN_800221a8                         where it is, its length, the functions it calls, the DAT_ values it reads
python3 tools/decompFunc.py FUN_800221a8 --grep PATTERN [-C N]   only the lines matching PATTERN, with N lines around them (default 2)
python3 tools/decompFunc.py FUN_800221a8 --lines 40-80           lines 40 to 80 of the function
```

`--file` reads another pseudocode file (default `shadowDecomp/decompiled_nops/pseudocode.c`).

`tools/decomp/sadx/` and `tools/decomp/shadow/` hold the Ghidra headless scripts that build the untracked `sadxDecomp/` and `shadowDecomp/` folders. You run Ghidra; Claude reads the output.

- `patchDol.py` writes `main_nops.dol`, `main.dol` with the Gekko paired-single instructions replaced by nops (Ghidra can't decode them).
- `shadow/runNops.sh` (run from `shadowDecomp/`) builds `decompiled_nops/pseudocode.c`, the decompile to use (the older `decompiled/` cuts update functions short).
- `sadx/run.sh` and `sadx/runStg01.sh` (run from `sadxDecomp/`) build the SADX decompiles with `_Main.rel` or `_Stg01.rel` mapped in; `relTool.py` decompresses and describes a `.rel`, `buildRelImage.py` lays a relocated `.rel` out for Ghidra.
- The Java scripts still write to absolute paths under `/home/lagamyrose/Github/SonicFantasy/`.

## Other

`notifyDone.py [volume]` plays a short ring (default 10% volume). Claude runs it at the end of each task that used tools.
