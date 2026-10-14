# docs/ai: compact twins for Claude, loaded on demand
Token-saving versions of the long human docs, in dense shorthand. The human doc is the source of truth; on conflict it wins and the twin gets fixed. Nothing here is auto-loaded: read this index, then only the files the task needs.

| Task touches | Read | Human doc |
|---|---|---|
| any code change | `ARCHITECTURE.md` (state, build, layout, runtime flow, conventions, gaps; 7 KB) | `docs/ARCHITECTURE.md` |
| Globals, pools, camera, meshes, Entity, levels, streaming, culling | + `ARCH_ENGINE.md` | same |
| collision, physics, gameplay tick, Lua `sf` API, scripts | + `ARCH_GAMEPLAY.md` | same |
| a disc format / importer | + `ARCH_IMPORTERS.md` | same |
| the original Shadow player (any) | `STH2005_PLAYER.md` (core: reading the game, frame, offsets, flags, behaviors, open questions) | `docs/STH2005_PLAYER_REFERENCE.md` |
| ground speed, turning, skid, slopes, drift | + `STH2005_GROUND.md` | same |
| ground resolve, collision sweep, probe/snap, fence kick | + `STH2005_COLLISION.md` | same |
| jump, jumpdash, landing, drawn model, animation | + `STH2005_AIR_ANIM.md` | same |
| the original SADX Sonic (task/struct layout, input, time, `sadxLiveLog.py`) | `SADX_SONIC.md` | `docs/SADX_SONIC_REFERENCE.md` |
| using the engine: loading a level, level JSON, adding a level or a game, the Lua `sf` entry points | `ENGINE_USAGE.md` | `docs/ENGINE_USAGE.md` |
| the SADX Station Square import (land tables, textures, level file) | `docs/STATION_SQUARE.md` (already compact, no twin) | same |
| replay recording / comparison | `REPLAY.md` | `docs/REPLAY_COMPARISON.md` |
| running a tool | skill `project-tools` | `docs/TOOLS.md` |
| deciding what is left / parked as polish | `POLISH.md` | `docs/POLISH_NOTES.md` |

Rules:
- A task that changes a human doc updates the matching twin file in the same task. Twins keep current truth only; a superseded claim stays as a one-line `~~...~~` pointer at most; history lives in the human doc. A new fact goes in the topic file it belongs to, not in the core file.
- Open a human doc only for one section (grep its heading, Read with offset/limit).
- Decompile: grep the twins for the FUN_ address first (decoded functions are recorded there); otherwise `tools/decompFunc.py` (summary, `--grep`, `--lines`) instead of reading a whole function.
- Tool and script output: compact `--ai` / digest forms only; scratchpad scripts print the answer (a few lines or a short table), never whole lists or dumps.
- One task per chat: at the end of a task its status sits in the twins (e.g. `REPLAY.md` Status), so a new chat starts from them instead of a long history.
