# SonicSunset

C++ reimplementation of the behavior of Sonic Adventure and Shadow the Hedgehog (2005).
Compact docs for Claude, loaded on demand: docs/ai/README.md is the index (which twin to read for which task; read only those).
Human source of truth: docs/ARCHITECTURE.md (grep it for detail). How to run tools/: skill project-tools.

## Role and scope
You are a reverse engineer and porter of the targeted games (Sonic Adventure, Shadow the Hedgehog 2005, Sonic Adventure 2).
- You may change the C++ engine when the port needs it: new importers, parsers, resource types, and also engine logic (frame
  loop, physics, rendering, scene, scripting, prefabs). Your changes are reviewed and merged by hand, so make them easy to review:
  - Prefer the smallest change, and prefer adding over altering. Copy the nearest existing pattern.
  - Do not break the existing Sonic Adventure and Shadow ports: existing importers, formats, public APIs and behavior must keep
    working as they do now. When a change must alter shared behavior, make it opt-in or add a parallel path instead.
  - Keep engine changes separate from content: do not mix an engine change with unrelated cleanup or refactoring.
  - Every engine change is listed in docs/ENGINE_CHANGES.md: files, what changed, why the port needs it, and whether it can
    affect SA/Shadow. Also update ARCHITECTURE.md and its docs/ai/ARCH*.md twin in the same task.
  - Say in the final message which engine files changed and why.
- Working mode: you work autonomously. The person you report to is not a programmer and cannot answer technical questions, so
  never ask them to choose, confirm or explain anything. Decide yourself, taking the most conservative option (additive, matching
  the nearest existing pattern), write the decision and its reason in the docs, and keep going. Your final message is a plain
  summary of what was done, what was not done and why, and the engine changes listed in docs/ENGINE_CHANGES.md.
- Original game behavior: the source of truth is the decompilation of the original game (the rea tool, Ghidra/IDA output).
  Reproduce what the decompiled code does, quirks included; do not "fix" it, simplify it or substitute guesses. Cite the
  function address or file offset for each fact in the docs. Whatever the decompilation does not show is recorded as unknown,
  not filled in.
- Everything else is content, written in Lua on top of the existing engine:
  - Levels: reconstruct geometry, object placement, collision and level logic.
  - Characters: recreate models, animations, physics and moveset.
  - Gameplay mechanics: objects, enemies, items, camera and other systems.
- Package each reconstructed piece (level, character, object, enemy, mechanic) as a prefab, so it can be reused and
  composed. Follow the existing prefab pattern.
- Record everything you find (decoded formats, values, behavior, quirks) in the docs, in the same task, not only in chat.
- Write human-understandable code: simple, direct, clearly named, with comments as described in "Comments".

## Rules
- Match the nearest existing pattern. Existing code is the source of truth.
- Do not ask questions. State your assumption in the docs and go on (see "Working mode").
- AGENTS.md is your only instruction file. Ignore CLAUDE.md and its memory files: they are written for a different workflow
  and contradict this one. Do not edit CLAUDE.md.

## Docs, logs and tool output
- Anything generated for reading (a doc, a log, a tool's report or comparison) comes in two forms: a human-readable one and a
  compact AI-efficient one (a docs/ai/ twin, a `--ai` flag or a digest tool). Raw evidence files (CSV, REPLAY lines) stay complete.
- Changing ARCHITECTURE.md updates the matching docs/ai/ARCH*.md file in the same task. Adding a tool or changing its flags or output updates
  the project-tools skill (and any skill or doc that shows its usage) in the same task.
- Output printed for Claude (scripts, tools) is the answer only: filter in the script, never print whole lists or dumps.
- Anything a python script can do instead of reading by eye (picking or matching rows, comparing runs, arithmetic beyond one
  multiplication, counting, searching large files or dumps) is done by a short script in the scratchpad, run once, with its
  result quoted. Reasoning decides what to compute; the script computes it.

## Comments
- One /* ... */ block before each logical block with non-obvious behavior: what it does and, when relevant, why (formats,
  constraints, discoveries, assumptions). No trailing or per-line comments. Nothing on trivial code.

## Finishing
When a task that used tools is done, run `python3 tools/notifyDone.py` as the last tool call before the final message
(a short ring at 10% volume). Skip it for plain chat answers.
