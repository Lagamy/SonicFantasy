# Unknowns

Things we do not know yet about the original games and would have to figure out. Not a task list for the code: a record of what is open,
what we have for it, and where to look. Same labels as `CLAUDE.md`: observed (from the user), external (the Ghidra decompile of the retail
`main.dol`, in the untracked `shadowDecomp/`, or other outside sources), hypothesis, unknown. Add a line when something turns up, move it to
`ARCHITECTURE.md` once it is settled, and delete it here.

## Shadow `.CCL` object collision (readCCL, `tools/dumpCCL.py`)
| What | Have | To find out |
|---|---|---|
| Meaning of a record's `flags` (`0x10000202`, `0xf0040000`, ...) and `id` | Passed to the `Range` base class constructor (`FUN_800292c4`, args 4 and 5); values repeat widely (`0x202`/`0x201` low bits, high nibble 0..f) | Read what the Range base class and the collision manager do with them (probably group/mask, damage, push) |
| Order the 3 angles are applied in | The game builds the matrix with a helper (`FUN_8001e070`) that takes a per-shape mode (0..2) selecting the order | Find which mode the Box/Capsule/Cylinder use; the lines view uses SADX's order |
| Capsule/Cylinder: which of f3/f4 is the radius, which the half length, and the axis | Hypothesis: f4 radius, f3 half length along local z (the game scales f4 with x and f3 with z); bounding radius f3 + f4 (capsule), sqrt(f3^2 + f4^2) (cylinder) | Read the shapes' contact/overlap functions (Capsule vtable `0x8051cb68`, Cylinder `0x8051cbe4`) |
| Angles are radians | Hypothesis from values (±1.5708, 0.785, 0.698, ...) | Same functions |
| Plane (type 4) layout | Box subclass, second size always 0 in the files; the game's plane code is `FUN_80034a1c` | Check how the plane uses its sizes |
| Space of the shapes | Kept in the file's own space; the renderer yaws RenderWare data by `RenderWareYawDegrees` (itself a hypothesis) | Compare with a drawn Shadow entity once one is wired |
| What a group name means | One group per file, `{0, 0}`; name = entry name in lower case, or a build path (3 files) | Find who looks groups up by name (`FUN_8040e4bc`, called with "SCENE" elsewhere) |
| Forms used for the Shadow shapes (`Physics::CollisionShape::form`) | Sphere 0, Box and Plane 3, Capsule 6, Cylinder 11 (an importer-own value, drawn along z): my choices; SADX form 6 as capsule is a guess | Decide whether `CollisionShape` needs its own kind for Shadow |

## Shadow's own (player) collision
Everything here is external (the Ghidra decompile in `shadowDecomp/decompiled/pseudocode.c`) unless marked hypothesis; function names are `FUN_<address>`.
| What | Have | To find out |
|---|---|---|
| How the player's collision shape is chosen | Observed (user's MRAM dump `shadowDecomp/mem1.raw`, Shadow idle in a stage, player position about (-196, -4725, -2410) in game axes): a `CharaColliBody` (`0x80f108a0`, holds the position and a heading of -pi) owns a **Capsule** (`0x80f10910`): center (0, 0, 0), f3 = 0, f4 = 5.5, angles 0, flags `0x302`, bounding radius 5.5 (so a capsule of radius 5.5 and no length). External (decompile): bodies are built from records by `FUN_80037668`, the same factory as `.CCL` records, via `FUN_800c81f4(.., info, mode)` (Body when the second record's pointer is 0, else General). Not found: the code or data that holds those numbers for Shadow (no `.CCL`); it is probably a record built in code | Find the player's call to the factory (not a direct caller of `FUN_800c81f4`; the 14 setup functions such as `FUN_800ad3f8` create components through other paths); or dump other characters/stages to see whether 5.5 is fixed |
| How a `.CCL` is tied to an object | A group is found by `FUN_8001277c(manager, a, b)` / `FUN_80012700(manager, archive index, name or index)`; `FUN_8019a890(group, index)` returns the record array and overwrites each record's flags with `0x202` (loop ends at the record whose pointer is 0: the zeroed struct). `FUN_80162084` looks entries up in a per-model table of 0x18-byte entries `{?, index, ?, kind}` and passes `DAT_804c2880[kind]` as the mode | What the table entries and the modes (2, 5, 6, 10 seen) mean; where those tables come from (a `.BIN`?) |
| How the player is put together | `FUN_800ad3f8` is the Shadow player setup (it writes the name "Shadow" into a component, `0x805f2c58`): it creates components (`Player::Watch`, `Controller`, `ShadowExecuter` via `FUN_800af1d4`, `Player::CharaColliMove` via `FUN_80094088`) and, for each character id in `param_1+0x64` (0xf, 0x11, 0x13 seen), binds slots 2..0xb to bones with `FUN_800ade6c(&out, skeleton, slot)` (hypothesis: hit areas for attacks, not the body shape). `FUN_80095c24` (a `CharaColliMove` method) only reacts to collision results (sets a state in `param_2+0x54`) | Read the `Range`/`CharaColliBody` creation reached from `FUN_800ade6c`'s users and `FUN_800afcc8`; or read the shape in memory while the game runs (Dolphin debugger), which would be an observation |
| What `flags 0x202` means | Written over every record's flags by `FUN_8019a890` | Same as the `.CCL` flags entry above |
| What the player does with `CharInfo` | `CharaColliBodyCharInfo` holds a vec3 at `+0xc` and a float at `+0x30` which the Body constructor copies | Whether those are the shape sizes of the player |

## Engine side (already listed in `ARCHITECTURE.md`, repeated here because they gate the above)
- Euler order of Shadow motions, SADX/Shadow motion frame rates, `RenderWareYawDegrees`, strip winding for worlds: hypotheses, see `ARCHITECTURE.md`.
