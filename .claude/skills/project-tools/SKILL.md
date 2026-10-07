---
name: project-tools
description: How to run every script in tools/ (replay compare and digest, live loggers, key generators, disc dumpers, decomp helpers, notifyDone) with their arguments and flags, and which output to read. Use before running or changing any tools/*.py, or when unsure which tool gives an answer.
---

Each tool's docstring is the full reference (`head -30 tools/<name>.py`). This list is the compact index, twin of the human `docs/TOOLS.md` (human wins on conflict); keep both in sync: a task that adds a tool or changes a tool's arguments/flags/output updates this file, `docs/TOOLS.md` (and `docs/ai/REPLAY.md` / `docs/REPLAY_COMPARISON.md` for the replay tools) in the same task.

Rules: never build or run the app. Run only tools that read existing files. NEVER run `shadowLiveLog.py` (records live, overwrites its CSV) or the generators/dumpers unless the user asks (they rewrite `src/*Keys.h` / read whole discs). Any tool that produces logs or reports for reading has a compact `--ai` (or digest) form plus the human form (CLAUDE.md); read the compact one.

## Replay (Shadow port vs original), see docs/ai/REPLAY.md
- `python3 tools/compareReplay.py tools/<n>.csv tools/port_run.txt <n> [step_s=0.1] [--ai]`: port's last replay `<n>` vs the recording. `--ai` = summary line + `t pl dpl vy dvy h dh drift face beh` rows, quiet runs `t0-t1 ok`. Always use `--ai`; human tables only for the user. Port position of a row = next row's (log pos is pre-move).
- `python3 tools/digestRun.py tools/<n>.csv [--from T] [--to T] [--every N] [--contacts [--words 1c8,a0,...]] [--absolute]`: original per frame, port axes, decoded columns. `--contacts`: extra line per row `a0= a4= bits=` (+0xa8 set bits) `| +off=hex` changed words of hit_raw/ground_raw (short windows); `--words` keeps only those hex offsets (use it: the full list is ~25 words a row). `--absolute`: positions not from start (orig level = (−z, y, x)). `python3 tools/digestRun.py tools/port_run.txt <n> [same flags]`: port's last run, matching columns + `wall wx wy wz` (wall flag, wall normal). Original digest adds `cam` (camera heading, port axes) when the CSV has camera columns. Never read raw CSV rows or REPLAY lines.
- `python3 tools/makeReplay.py tools/<n>.csv Scripts/shadow/replays/<n>.lua`: CSV (must be `--pad`) → Lua replay; phi per row from the camera columns when present, else from the world stick vector. Then register in `Scripts/shadow/main.lua` and `ORDER` in `replay.lua`.
- `sh tools/regenReplays.sh [name ...]`: runs makeReplay.py for every recording of ORDER (or only the names given), tools/live_<n>.csv → Scripts/shadow/replays/live_<n>.lua.
- `python3 tools/makeReplay.py tools/<n>.csv --check-camera [--ai]`: camera phi vs stick-vector phi on rows holding both (writes nothing). `--ai` = summary line (n/mean/max: all, slow flat, camera one frame earlier) + rows >1° apart, quiet `t0-t1 ok (n)`. Use `--ai`.
- `tools/shadowLiveLog.py [out.csv] [--seconds N] [--pad HEX] | --find-pad`: USER ONLY (`.venv/bin/python`, Dolphin running; current pad 8056ED4C). Always logs `cam_eye_x/y/z`, `cam_target_x/y/z` (camera from manager 0x8056ee14; `--camera`/`--find-camera` removed 2026-10-06) and a last column `vi_count` (u32 at 0x805f1024, H: VI retrace count, +1 per tick; added 2026-10-07, unverified live).
- `python3 tools/dtm.py dump run.dtm [first [count]]` / `synth template.dtm out.dtm "frames:sx,sy[,buttons]" ...`: Dolphin movie read/write (layout E, unchecked).

## Disc / resource generators (write files; only when asked)
- `genResourceMap.py` → `ResourceMap.txt`; `genCategoryMap.py` → `ResourceMapByCategory.txt` (run after the first).
- `genEntityKeys.py [stage ...]` (default stg0100) → `src/STH2005EntityKeys.h`. `genCCLKeys.py` → `src/STH2005ShapeKeys.h`. `genSAEntityKeys.py` → `src/SAEntityKeys.h`. `genSAShapeKeys.py` → `src/SAShapeKeys.h` (needs sadxDecomp pseudocode).
- `dumpCCL.py [outDir] | -x NAME (hex) | -p NAME (parsed) | --check (strict all)`: Shadow `.CCL` entries.

## RAM-dump readers (read only, print)
- `sadxLiveTasks.py [mem1.raw]`: SADX live SET tasks → exec/display/destroy offsets. `sadxLiveCollision.py [mem1.raw]`: their live collision entries. Default dump `sadxDecomp/mem1.raw` (Emerald Coast act 1).

## Decomp
- `python3 tools/decompFunc.py FUN_xxxxxxxx [--grep REGEX [-C N] | --lines A-B] [--file F]`: one function of `shadowDecomp/decompiled_nops/pseudocode.c`. No flag = summary (file line range, line count, callees, DAT_ refs); `--grep` matching lines with context; `--lines` a slice (1 = function's first line). Use it (or Read with the printed range) instead of reading a whole function; check the docs/ai twins for the FUN_ address first.
- `tools/decomp/{sadx,shadow}/run*.sh` (Ghidra headless, run from `sadxDecomp/` / `shadowDecomp/`), `patchDol.py`, `relTool.py`, `buildRelImage.py`. User runs Ghidra; read the outputs.

## Finishing
- `python3 tools/notifyDone.py [volume=0.1]`: last tool call of every task that used tools.
