# SonicFantasy

C++ reimplementation of the behavior of Sonic Adventure and Shadow the Hedgehog (2005).
Compact docs for Claude, loaded on demand: docs/ai/README.md is the index (which twin to read for which task; read only those).
Human source of truth: docs/ARCHITECTURE.md (grep it for detail). How to run tools/: skill project-tools.

## How we work
I'm the architect and review every diff. You get small-to-medium tasks; implementation details within a task are yours. If a task
looks larger, say so and suggest a split. I must be able to maintain everything without you: prefer simple, direct code.

## Rules
- Match the nearest existing pattern. Existing code is the source of truth.
- Keep changes scoped to the task. Mention anything else you notice, briefly, at the end, and leave it alone.
- Proceed by default. Propose and wait for approval only for: public API changes, new abstractions or dependencies, architecture
  changes, or code that depends on an unresolved guess about the original game.
- Ask only when the answer would materially change the result and the code doesn't answer it. Otherwise state your assumption and go on.
- Don't edit CLAUDE.md without showing me the change first.

## Original game behavior
- Label claims observed (from me), external (outside sources; forums and other reimplementations can be wrong), hypothesis
  (including yours) or unknown. Never imply you ran experiments yourself.
- If sources and my observations conflict, show me the conflict; don't override my observation.
- Reproduce observed behavior, quirks included. Don't "fix" it, invent causes, or build general systems for behavior we don't understand yet.
- If you find leaked original source code, flag it and don't use it.
- Document what you learn about the original game as you find it, in the repo's docs, not only in chat. Keep one consolidated
  reference per subsystem (docs/STH2005_PLAYER_REFERENCE.md is the model) next to the working notes, and update it in the same task.
  Label each fact with its evidence (decompile, live measurement, disc file, hypothesis) and where it was read or measured. Record
  corrections where they supersede an earlier claim. Everything a recording or decompile reveals goes in, even if unrelated to the task.
- Before working on the original game or the port's behaviour, read docs/ai/README.md and only the twins it names for the task; open the
  full human docs only for a specific section. Update a twin in the same task as its human doc; the human doc wins on conflict.

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

## Building
I build and run the app myself. Don't build, run or test anything unless I ask. Use errors and logs I paste. After a change, say what
changed and that nothing was run. Never claim it matches the original game without evidence from me.

## Finishing
When a task that used tools is done, run `python3 tools/notifyDone.py` as the last tool call before the final message
(a short ring at 10% volume). Skip it for plain chat answers.
