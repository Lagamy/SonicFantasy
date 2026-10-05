# SonicFantasy

C++ reimplementation of the behavior of Sonic Adventure and Shadow the Hedgehog (2005).
Layout, architecture, tools, conventions: @docs/ARCHITECTURE.md

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

## Comments
- One /* ... */ block before each logical block with non-obvious behavior: what it does and, when relevant, why (formats,
  constraints, discoveries, assumptions). No trailing or per-line comments. Nothing on trivial code.

## Building
I build and run the app myself. Don't build, run or test anything unless I ask. Use errors and logs I paste. After a change, say what
changed and that nothing was run. Never claim it matches the original game without evidence from me.

## Finishing
When a task that used tools is done, run `python3 tools/notifyDone.py` as the last tool call before the final message
(a short ring at 10% volume). Skip it for plain chat answers.
