---
name: shadow-replay-compare
description: Compare the Lua Shadow port against the original game by replaying a recorded pad input (tools/shadowLiveLog.py, makeReplay.py, replay.lua, compareReplay.py). Use when the user records a new live CSV, pastes a comparison table, or wants a port behaviour matched to the original.
---

Full method, commands, limits and recording protocols: `docs/REPLAY_COMPARISON.md`. Read it first.

Summary: user records the original with `tools/shadowLiveLog.py --pad 8056ED4C tools/<name>.csv` -> `python3 tools/makeReplay.py tools/<name>.csv Scripts/shadow/replays/<name>.lua` -> register in `Scripts/shadow/main.lua` and `ORDER` in `replay.lua` -> user builds, runs the port with stdout redirected, presses R -> `python3 tools/compareReplay.py tools/<name>.csv tools/port_run.txt <name>`.

Rules (CLAUDE.md): never run the app or build, and never run shadowLiveLog.py (it overwrites its CSV); run only the python analysis tools on existing files. Label claims observed / external / hypothesis; port observed behaviour quirks included; write findings to `docs/STH2005_PLAYER_REFERENCE.md` section 20+ in the same task; run `python3 tools/notifyDone.py` last. Treat 1 to 2 frame alignment error and the -11.34 rest vy as known, not bugs. Rules fitted to a recording need a different recording to validate.
