# SonicFantasy

C++ reimplementation of the behavior of Sonic Adventure and Shadow the Hedgehog (2005).
Repository layout, architecture, tools and conventions: @docs/ARCHITECTURE.md

## How we work
This is my codebase and I am the architect. I give you small-to-medium tasks
and review every diff myself, so implementation details within a task are
yours to decide. If a task looks larger than that, tell me and suggest a
split. I must be able to maintain everything without you: prefer simple,
direct code I can adjust as I make new observations.

## Rules
- Match the nearest existing pattern. Existing code is the source of truth.
- Keep changes scoped to the task. Mention anything else you notice in short form at the end and leave it alone.
- Proceed by default. Propose and wait for approval only for: public API
  changes, new abstractions or dependencies, architecture changes, or code
  that depends on an unresolved guess about the original game.
- Ask only when the answer would materially change the result and the code
  doesn't answer it. Otherwise state your assumption and continue.
- Don't edit CLAUDE.md without showing me the change first.

## Original game behavior
- Label claims as observed (from me), external (outside sources; forums and
  other reimplementations can be wrong), hypothesis (including yours), or
  unknown. Never imply you ran experiments yourself.
- If sources and my observations conflict, show me the conflict; don't
  override my observation.
- Reproduce observed behavior, quirks included. Don't "fix" it, and don't
  invent causes or build general systems for behavior we don't understand yet.
- If you find leaked original source code, flag it and don't use it.

## Comments
- One /* ... */ block before each logical block with non-obvious behavior:
  what it does and, when relevant, why (formats, constraints, discoveries,
  assumptions).
- No trailing or per-line comments. Nothing on trivial code.

## Building
I build and run the app myself. Don't build, run, or test anything unless I
ask. Use errors and logs I paste. After a change, say what changed and that
nothing was run. Never claim it matches the original game without evidence
from me.

## Finishing
When a task that used tools is done, run `python3 tools/notifyDone.py` (a short ring at 10% volume) as the last tool call before the final message. Skip it for plain chat answers.
