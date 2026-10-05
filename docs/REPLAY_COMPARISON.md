# Replay comparison: checking the Lua port against the original game

Method for making the Shadow port (`Scripts/shadow/`) behave like the original GameCube game: record the original, feed the recorded pad input to the port, compare the two. Findings go into `docs/STH2005_PLAYER_REFERENCE.md` (section 20 and following), labelled with their evidence.

## The loop
1. **Record the original** (user, Dolphin running the game): `.venv/bin/python tools/shadowLiveLog.py --pad 8056ED4C tools/<name>.csv`, Ctrl+C to stop. One row per game frame: position, velocity, behavior, and with `--pad` the pad slot at 0x8056ED4C (`pad_sx`, `pad_sy` raw s16 stick, `pad_fx`, `pad_fy` float stick, `pad_buttons`). It overwrites its CSV, so never run it to read a log; analyse the CSV with python.
2. **Make a replay**: `python3 tools/makeReplay.py tools/<name>.csv Scripts/shadow/replays/<name>.lua`. It writes the stick as camera-relative forward/right floats, the A button, the start position, `heading0` and the camera heading `phi` (not recorded: fitted from velocity angle minus stick angle, a hypothesis; the right sign is tried both ways).
3. **Register it**: add `sf.include("shadow/replays/<name>.lua")` to `Scripts/shadow/main.lua` and the name to `ORDER` in `Scripts/shadow/replay.lua`. A missing generated file gives "cannot open Assets/Scripts/shadow/replays/<name>.lua": run step 2.
4. **Run the port** (user builds and runs; `cmake --build build` copies `Scripts/` to `build/Assets/Scripts`). Redirect the app's stdout to a file (Lua has no file output), e.g. `tools/port_run.txt`. R teleports Shadow to the recording's start and runs it: first R the first name of `ORDER`, second R the second, and round again. Each tick prints `REPLAY\t<name>\t<tick>\t<t>\t<x y z>\t<vx vy vz>\t<motion>`. `ShadowReplay.enabled = false` gives R back as a plain respawn.
5. **Compare**: `python3 tools/compareReplay.py tools/<name>.csv tools/port_run.txt <name> [step]`. Prints, per step (0.1 s), planar speed, vertical speed and height, original / port / difference, plus the behavior and motion names, and the largest differences. Only axis-independent quantities are compared, because the port's world is the original turned by `RenderWareYawDegrees` (hypothesis: (x, z) goes to (z, -x), heading h to h - pi/2).
6. **Interpret, change the port, document**: decode the rule from the numbers, port it with a comment labelling the evidence, add a bullet to the reference doc, re-record or re-run. The user builds and runs; do not build or run the app, only the python tools on pasted/stored files.

## Known limits and constants
- Alignment: the logger's rows are wall-clock samples, about 1 to 2 frames of error; single-sample differences of 2 to 5 in vertical speed during jumps are that shift, not physics. Planar speed on the ground is the reliable signal.
- The port's rest vertical speed is -11.34 (ground press), the original stores 0: a known constant difference.
- Spawn: the port's ground lies 5.02 below the recorded height whatever the spawn height (cause unknown); `START_LIFT = -5.0` in `replay.lua`. The compare tool takes the port's height at `SETTLE_TIME` (0.25 s), after the drop.
- Tick rate 60 Hz is a guess.
- The camera is not recorded, so `phi` is a fit; stick right sign is only weakly determined.
- Rules fitted to a recording are only validated by a different recording. Prefer new, independent recordings (other stick levels, turns, slopes, landing with the stick held, air/jump with analog stick) over re-checking the same one.

## Recording protocols used so far
- `live_run`: run, jump, dash, landing, jump from a slope.
- `live_accel`: full stick, accelerate, release, brake.
- `live_half`: stick held at about quarter, half and slightly more, then released (analog stick rule).

Not recorded yet: turns (stick sideways while moving), slopes, landing with stick held, air control with analog stick.
