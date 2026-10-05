# Shadow player (Shadow the Hedgehog 2005): decode notes

**The consolidated reference is `docs/STH2005_PLAYER_REFERENCE.md`; this file is the working log in the order things were found (some early lines are superseded there).**

The Shadow player work: the Ground behavior, the integrator, Fall and Jump are read and ported to `Scripts/shadow/player.lua` (see Port at the end); the rest is unknown.
Labels: external = the Ghidra decompile of the retail `main.dol` (`shadowDecomp/decompiled_nops/pseudocode.c`, Gekko paired-single math is nopped, so vector math is partly lost), hypothesis = mine, unknown. Nothing here is observed (no RAM dump check yet). Nothing in the engine uses this yet.

Floats below are read from `main.dol` at the `DAT_` addresses the decompile names (scratch helper, not kept in the repo). Speeds look like units per second (the update gets `dt` and multiplies by it; 1000 to 15000 do not fit units per frame): hypothesis.

## Player::Behavior::Ground (vtable 0x80520690, Update `FUN_800838f4`)
Behavior object (external): `+4` the player, `+0x1c` sub-state, `+0x20` step inside the sub-state, `+0x24` min speed, `+0x28` max speed, `+0x2c` current speed, `+0x30` and `+0x34` set by the run code (meaning unknown, see below), `+0x38` a word the code only tests for non-zero (a slide or rail state? unknown).
Player (external, offsets only): `+0x18` vec3, `+0x24` vec3 (written as current speed x direction: hypothesis, the move velocity), `+0x64` an int that indexes a table, `+0xa0`/`+0xa8` bit sets tested by index (`FUN_8006cd78(set, bit)`; bits 0x1b, 0x1e, 0x2e, 0x3b, 0x3c, 3, 2, 0x1f seen here, meanings unknown), `+0xb8` a vec3 the run code reads as the stick (hypothesis), `+0x230` the animation object.

Update (`FUN_800838f4`), external: first `FUN_80082cc8` (below), then sub-state 0 -> `FUN_800831d8` (run), 1 -> `FUN_80082ff4`, 2 and 3 -> `FUN_80082f04`.

### Speed limits (`FUN_80082cc8`, every update)
- min speed = 1000 (external).
- max speed = 15000 x `table[player+0x64].factor` (external), or 3000 with bit 0x1e set (external). The table is at 0x804f1838, stride 0x2c, first float only is non-zero: index 0 = 0, 1 = 1.0, 2 = 0.9, 3 = 1.0, 4 = 0.6, 5 = 0.9, 6 = 0.8, 7 = 0.8 (external; what `+0x64` is, and which index Shadow's normal state has, is unknown; if it is a character id, Shadow would be one of these).
- `FUN_80082c48`: below speed 30 the current speed is the min; otherwise `curve(speed / 750) * (max - min) + min` (external). The curve is a call through `FUN_800704ac` on the object at `DAT_805e4264`; its shape is unknown.

### Run (`FUN_800831d8`), external unless marked
- Bit 0x3c set: switch to sub-state 1 and run `FUN_80082ff4`.
- Otherwise the stick vector `s` is read from player+0xb8. If `|s|^2 >= 0.81` (stick pushed at least 0.9: hypothesis that `s` is the stick scaled to 1):
  - `c` = a cosine-like value between the stick and the player's forward (from `FUN_8040cf90`, `FUN_8000a980`; the exact function is lost to nopped math: unknown).
  - target speed `t` = `k * (max - min) + min`, `k = (c - 0.766) / 0.234` if the player's speed is at least 30 and `c >= 0.766`, else 0.
  - new speed = `min(t, c^8 * dt * 0.25 * (max - min) + speed)`.
  - the player's `+0x24` vector = new speed x the stick direction, then clamped to length `max`.
  - `+0x34` = `FUN_80015d68(c)` (a function of `c`, probably an angle: unknown).
- If `|s|^2 < 0.81`: speed x 0.9 per update, not below the min; `+0x34` = 360.
- `+0x30` (a turn or sticking rate, hypothesis from its use as `FUN_80093af8(+0x30 * dt, ...)`): 15 when the speed is at most 150 (`|v|^2 <= 22500`); between 150 and 300 it falls from 13 + 2 towards 2 along `FUN_8000fee4` (unknown shape), above 300: 2.
- The animation rate (`FUN_800aa7b8`, value `r`) from the speed `v`: `v / 12.5` below 12.5; 12.5..30: `(v - 12.5) / 17.5 + 1`; 30..100: `(v - 30) / 70 + 2`; 100..720: `(v - 100) / 620 + 3`; 720 and up: 4. Bit 3 is set at 100 or more; a second effect is switched on at 100 or more (calls on `player+0x23c`, unknown) except with bit 0x1e.
- Sub-state 2 is chosen by `FUN_80082df0` when bit 0x1b is set, `|v|^2 >= 40000` (speed 200) and the angle between the player's vector and the stick is at most 90 degrees (the code compares a dot product with `FUN_8000f3b0(90 * (pi / 180))`; hypothesis: that function is a cosine, so within 90 degrees). Its use is unknown.
- Sub-state 2/3 (`FUN_80082f04`): plays animation 10 (or 9) once then returns to 0 (unknown meaning).

## Integrator and the vectors (external, `FUN_800936f4` -> `FUN_80093728` -> `FUN_800938cc`)
The player (`player+0x18` velocity, `+0x24` acceleration, `+0xf4` the up axis, `+0xa8` the flag set) is moved by a linear drag: `v' = a - k v`, so over a step `dt` (`+0x60` points at 1/60 in the RAM dump) `v = a/k + (v - a/k) e^(-k dt)` and the position moves by `a/k dt + (1 - e^(-k dt))/k (v - a/k)`. `k` is 20 with flag 2 (the ground contact `FUN_8009728c` sets), 1 with flags 7 or 9, 10 with flag 0x1d, otherwise the vectors are split along the up axis (`FUN_8040f990`) and the first part has k = 1, the second (hypothesis: the one in the ground plane) k = 3. `FUN_8040cff8` is a vector `+=`: behaviors add to `+0x24`. With k = 20 the top speed is `a/k`: the ground acceleration 15000 gives 750 per second, which fits the animation breakpoints (12.5, 30, 100, 720). Where gravity is added is not found.

## Fall (`FUN_80080828`) and Jump (`FUN_800871d8`, Enter `FUN_800877c0`), external
- Fall: acceleration 220 times the stick (110 with flag 0x1e); the heading turns at rate 15 (`FUN_80093af8(15 dt)`).
- Jump: the table at 0x804b0558 (stride 12, all rows the same: 100, 270, a flag 1) gives the jump speed 100 and a held acceleration 270 along the jump direction; a timer of 0.75 seconds (`DAT_805f2794`) counts down while the hold bit is set; air acceleration 220 (110 with 0x1e); heading rate 6.5.
- The live player of the RAM dump (`mem1.raw`, Idle behavior at 0x80feeb28, Shadow at 0x80f13780): `+0x64` = 14, `+0x274` = 5.0, `+0xf4` = (0, 1, 0), at rest.
- Behavior vtables (from the typeinfo at 0x805e4xxx; Update is slot 5 = vtable + 0x28): Fall 0x805205e0 -> 0x80080828, Jump 0x80520804 -> 0x800871d8, JumpDash 0x80520844 -> 0x800882a4, HomingAttack 0x8052073c -> 0x800854e4, HomingJump 0x80520794 -> 0x800867e4, Landing 0x80520890 -> 0x80088d5c, Idle 0x805207c8 -> 0x80086c40, SpinDash 0x80520a80 -> 0x8008f1ec, Sliding 0x80520a20 -> 0x8008dcd4, Damage 0x80520494 -> 0x8007cbb8, GroundAttack 0x805206cc -> 0x80084170, WallJump 0x80520be8 -> 0x8009270c. Only Ground, Fall and Jump are read.

## Port
`Scripts/shadow/player.lua` (entry `Scripts/shadow/main.lua`): the Ground run (speed target by the cosine between the stick and the heading, `cosine^8` growth, drag 20, turn rate by speed), Fall and Jump as above, with the engine's gravity (-288, Sonic's: placeholder), the motions picked by name (hypothesis). Not ported: homing attack (its target selection is not read), spin dash, skate, Chaos Control, weapons, damage.

## Not decoded yet (needed for the port)
- The player's main update and the state machine that switches behaviors (Ground, Air, Jump, ...): which classes exist (RTTI names are in `main.dol` from 0x804b000c on) and what triggers each.
- Gravity, jump height and air control: unknown.
- What bits 0x1b, 0x1e, 0x3b, 0x3c are, what `+0x64` is, `+0xb8`, the stick scaling, `FUN_800704ac`'s curve.
- Gravity, slope handling, braking, homing (the target choice), skate, Chaos Control.
- Verification against `shadowDecomp/mem1.raw` (observed values of speed and the stick vector while running).

## Suggested next steps
1. Walk the other Player::Behavior vtables (find each Update as in the memory note on vtable layout) and list them here.
2. Find the code that sets `player+0x64` and the player's flag bits 0x1b/0x1e/0x3b/0x3c.
3. Dump the Ground behavior object from `mem1.raw` while Shadow runs (user: a Dolphin RAM dump while running at full stick) to check min 1000, max 15000 x factor, and the speed units.

## Motions and what the disc data shows (observed on `SHADOW.MTP`, hypotheses marked)
- The three channels before the nine of every non-root bone (ignored by the importer) hold the parent's scale: in `sh_jump_a`, `sh_j_head` and the arms' first bones carry the keys of `sh_j_mune`'s Y scale (frames 0, 10, 13: 1.0, 2.83, 2.43), `sh_j_l1te` the Z scale of `sh_j_l1ude3` and so on. Hypothesis: a segment scale compensation (the child cancels the parent's scale). Not applied yet.
- `sh_jump_a` is 13 frames and ends with the chest at scale (1, 2.43, 1) and the limbs folded; `sh_jump_idle` (60 frames) is the tucked pose with scale about 1. So the jump plays `sh_jump_a` once and then `sh_jump_idle` (hypothesis: the game's sequence): holding `sh_jump_a`'s last frame is what stretched the model.
- Skate: `sh_slow_skate` / `sh_high_skate` are used above 150 and 250 (hypothesis from the squared compares 22500 and 62500 in the Ground update).
- The engine cross-fades a switch of motions over 0.1 s (placeholder, `ONEInstance.cpp`), and the body can stick to the ground (`sf.setGroundSnap`, 11.85, `PhysicsBody::snapDistance`).

## Gravity, ground probe, jump motions (external unless marked; decompile of main.dol + SHADOW.MTP on the disc)
- Per frame the executer (`FUN_800a7d78`) runs the behaviors, then zeroes the acceleration (`+0x24`) and the stick (`+0xb8`, clamped to length 1) and clears flag 0x1b. Gravity is `FUN_800a7700`: if player flag 0 is set, it adds to `+0x24` an acceleration of -320 along the up axis in the air, -800 on the ground (flag 2), -0.0625 with flag 1; the direction is `+0xf4` (the gravity direction, which `SetGravityCommand` writes from the zone) or, with player bit (`+0xa0` bit 0, set by the Ground update at squared speed above 62500 = 250^2) and not flag 0xd, `+0x1c8` (the ground normal). With the drag the vertical speed settles at -320 in the air and -40 on the ground.
- Ground probe (`FUN_8009728c`): a ray along `-(+0x1c8)` of length `2.37 x (+0x274 = 5.0) + |this frame's move|`; the player is snapped to it only if flag 2 (on the ground last frame) was set and the hit's normal is within 80 degrees (cos 0.1736) of up. `PressGroundCommand` sets flag 2.
- Jump: `sh_jump_a` (13 frames), `sh_jump_b` (28), `sh_jump_c` (30), then `sh_jump_idle` (loops). Observed: each one's last pose equals the next one's first (a end = b start on the root, hips, thighs, knees, head; b end = c start exactly); `sh_jump_c` turns the root by -6.98 radians (a somersault, curled up); `sh_jump_a` and the start of `sh_jump_b` stretch bones (scale 2.4 on the chest, 2.9 on a shin), the end of `sh_jump_b` is back at 1. Hypothesis: that is the game's jump (a squash-and-stretch takeoff and the rolled ball), which `Scripts/shadow/player.lua` plays as a, b, c once each, then idle.
- `Player::Behavior` animation indices (`+0x230` object, PlayMotion(rate, index)): Ground 1 (run) / 2 (skate, flag 0x3c) / 9, 10 (the turn-around), Fall 5, 6, 7, Jump 7 then 12; the index to name table is not in main.dol (no `sh_` strings), only the events (`sh_run` footsteps at frames) are in the MTP's tail.

## Ground snap as a per-body parameter (engine, `PhysicsBody`, `CollisionSystem.cpp`)
`sf.setGroundSnap(e, d)` (> 0 switches it on) and `sf.setGroundSnapMode(e, {ray, radius, minNormalY})`. Default: the stepped probe (steps down no longer than the shape radius, stops at the first walkable contact, restores if none). `ray = true` is the game's probe, read from `FUN_8009728c` (external; the sphere sweep it calls, `FUN_800986f0`, and the ray test `FUN_800979e8` are read too):
1. It runs only if the body was on the floor the frame before (flag 2) and the move did not end on one.
2. N = the previous floor's normal (`+0x1c8`, up if none). Ray from the body's center along -N, length 2.37 x r + |intended move| (r = `radius`, the record's `+0x274`, 5.0 in a RAM dump). The nearest triangle, any normal, at distance d; no hit: no ground.
3. d <= 2r: accepted. Else a second ray from the point 0.5 (d + r) down along -N, backwards along the move (at least r long): accepted if its nearest hit's normal has a dot with N above cos 80 degrees (0.1736).
4. Accepted: the body moves along -N to 1 unit before the hit (the game's second sweep stops earlier where its sphere touches); it is on the floor if the surface it meets has a normal within 80 degrees of N, and that becomes the floor's normal.
Not 1:1 (the game's own sweep is not ported): the first sweep is this system's move (the game's `c4`, the last sweep displacement, is taken as the whole intended move); the stop of the second sweep is computed for a plane (`radius / cos`), not by sweeping the sphere; the ray's triangle stands for the contact; the mover's behaviour stands for the game's attribute masks (`0x1a93` etc.).

## Measured on the original (live log, `tools/shadowLiveLog.py`, stage with Shadow, standing start then about 3 s of full stick on flat ground, then released)
Sampled per game frame (60 a second) from the running game. These confirm or correct the decode:
- **Acceleration growth confirmed**: the Ground behavior's current speed (`+0x2c`) rises by 58.3 each frame while the stick is held at full (1000, 1058.2, 1116.4 ...; 14000 x 0.25 / 60 = 58.33), not capped by anything until 15000. Planar speed follows it as `cur / 20` with the lag of the drag: cur 10279 gives 507, cur 5005 gives 243.
- **Drag confirmed**: with no stick the planar speed falls by exactly 0.717 each frame (e^(-20/60)).
- **Turn rate formula confirmed**: `+0x30` reads 15 up to speed about 150 and `13 x ((speed - 300) / -150) + 2` between 150 and 300 (170.2 gives 13.25, 205.2 gives 10.22, 299 gives 2.08), 2 above.
- **Flags confirmed**: bit 3 of the set (`+0xa8`) is set from planar speed 100, bit 0x3b from about 150 (first seen at 170, absent at 147), as decoded.
- **No stick**: the current speed is set to `1000 + 14000 x min(1, speed / 537.5)` (a straight line: 1.3956 x speed / 750, and 1.3956 = e^(1/3)), but only while the speed before the frame is 30 or more, else 1000. Corrects the earlier guess `speed / 750`.
- **Release of an analog stick**: for the first frames the current speed is multiplied by 0.9 each frame (11212, 10091, 9082, then the line above) while the speed falls 0.877, 0.786, 0.717 per frame: the stick's magnitude passing below 0.9 on its way to 0 (a keyboard has no such passage).
- **Start**: the speed gain per frame follows the stick's magnitude (the acceleration is `cur x |stick|`, the stick vector is not normalized): 0.24, 0.38, 0.57, 0.69, 0.79, 0.95, 1.0 of 1000 over the first 7 frames.
- Not yet measured: turning at speed (the cosine's effect), braking against the motion, jumps, slopes, the original's camera.

## Animation, measured and decoded (live log with the animation object, first recording: stand, walk, run, full stick, release)
Animation object = player `+0x230` (vtable 0x80522260; `FUN_800b5210` is PlayMotion(blend, index), `FUN_800b5320` its per-frame update, `FUN_800b4804` the blend update). All values below observed in the log unless marked.
- **Index to motion**: PlayMotion looks the animation index up in a table at 0x804b38a4 (16 bytes per index; the first word is a motion number or -1); the motion number is **1-based into `SHADOW.MTP`'s table** (motion = table position + 1; observed: the numbers 103, 147, 136, 145, 102 read in the log are `sh_idle`, `sh_walk`, `sh_run`, `sh_slow_skate`, `sh_high_skate` at positions 102, 146, 135, 144, 101). External for the table (0x804b38a4 values: index 6 -> 83, 8 -> 42, 9 -> 60, 10 -> 59, 11 -> 73, 13 -> 44, 16 -> 152, 17 -> 156, 18 -> 154, 19 -> 158, 20 -> 157, 23 -> 101, 25 -> 132, 26 -> 66, 27 -> 65, 30 -> 41, 31 -> 43, 33 -> 115, 34 -> 116, 35 -> 120, 36 -> 38, 37 -> 39, 38 -> 83, 39 -> 81, 40 -> 57, 41 -> 67, the rest -1; these are numbers to read as above, not yet checked against names).
- **Ground locomotion (index 1, the Ground behavior; index 0 is Idle) is a blend of two motions by the rate** (`+0x438`: speed / 12.5 below 12.5, then 1 + (speed - 12.5) / 17.5 to 30, 2 + (speed - 30) / 70 to 100, 3 + (speed - 100) / 620 to 720, 4 above; confirmed: planar speed 13.65 -> rate 1.066, 30.25 -> 2.004, 100.9 -> 3.002). The integer part picks the pair, the fraction is the weight of the second (`+0x448` = fraction):
  - rate 0 to 1: `sh_idle` -> `sh_walk`; 1 to 2: `sh_walk` -> `sh_run`; 2 to 3: `sh_run` -> `sh_slow_skate`; 3 to 4: `sh_slow_skate` -> `sh_high_skate`. The pair changes where the weight reaches 1 (the second becomes the first), so there is no jump. So the skate is not at 150 / 250 as guessed: it is the blend's third and fourth band, from speed 30 to 720.
- **Playback speed** (`+0x44c`, set on the mixer channel as its time scale, `FUN_800a9ef8`): `1 / ((lenA / lenB - 1) x (1 - w) + 1)` times a global time scale (1.0), where lenA / lenB are the lengths of the two motions and w the weight (`FUN_804125f4` gives the lengths): the blend keeps the two cycles in step. Observed values 0.60, 0.60, 1.71, 0.75, 1.18, 0.91, 0.89 over the bands. The mixer's own unit is not identified (its first word reads 30, a channel's `+0xac` reads 30.0: hypothesis, 30 frames a second).
- **Fade on a change of index** (Idle <-> Ground): PlayMotion's first argument is stored at `+0x430` and passed as the fade time to the library (`FUN_80416148`): read 15.0 entering Ground and 5.0 entering Idle in the log (units not yet known: needs the fade weights over time, which the next recording keeps in `channels_raw` / `fade_raw`).

## Animation, second recording (a run with a jump and landing): what the game does, now in `Scripts/shadow/animation.lua`
- **Motion numbers are 1-based into `SHADOW.MTP`'s table**: 110 `sh_jump_a`, 118 `sh_jump_fa`, 111 `sh_jump_b`, 119 `sh_jump_fb`, 113 `sh_jump_c`, 114 `sh_jump_d`, 117 `sh_jump_e` (and 103 `sh_idle`, 147 `sh_walk`, 136 `sh_run`, 145 `sh_slow_skate`, 102 `sh_high_skate`).
- **A motion plays inside a frame range stored in its data**: two big-endian u16 at +0x10 and +0x12 of the motion (my importer had called that u32 "not a reliable frame count"). Examples: `sh_jump_b` 21 to 28, `sh_jump_fb` 14 to 20, `sh_jump_c` 20 to 30, `sh_jump_d` 53 to 68, `sh_jump_e` 68 to 102, most others 0 to their length. The log shows the times starting at those starts and looping inside them (jump_c 20-29, jump_d 53-66).
- **Time**: each channel's time (`+0xac` of the channel) advances by its time scale every game frame (60 a second): exactly 1 per frame at scale 1. The scales for a blended pair follow `scaleB = 1 / ((la / lb - 1)(1 - w) + 1)`, `scaleA = scaleB x la / lb`, la and lb the range lengths (verified on every row of the Ground blend, and on the jump pair with 7 and 6).
- **Jump**: idx 5 (`jump_a` + `jump_fa`, weight of the second = planar speed / 700 at the jump: 314.6 / 700 = 0.4494 observed) for 13 frames, then idx 12: `jump_b` + `jump_fb` from their first frames for 7 frames, then `jump_c` looping until the apex; Fall (idx 3): `jump_d` looping; Landing (idx 4): `jump_e` from 68 to 102 (34 frames), then Idle.
- **Fades**: the weight of the new motions rises linearly 0 to 1 in as many frames as the number the state was started with (15 into Ground, 5 into Idle and Landing, 10 into Fall, 2 into the jump, 0 between a and b and c): read from the fade block (`+0x10` of the channel's second block) going 0.2, 0.3, 0.5, 0.7, 0.9, 1.0 in 10 frames for the Fall.
- **Not measured yet**: a standing jump, a held jump, the landing with the stick held, braking, damage, and the exact Idle <-> Ground change (the log's Idle returns at planar speed 0.19, Ground was at 10).

## Animation index table (static, from `main.dol` 0x804b38a4 and the PlayMotion calls of the behaviors)
The index the behaviors pass to PlayMotion, the motion it plays when it is a plain motion (number from the table, 1-based into `SHADOW.MTP`; its name and frame range read from the disc), and which behavior plays it with which fade (frames). `-1` = not a single motion: the state builds its own set at run time (the pairs of the Ground blend, the jump stages, ...).

| index | motion number | motion (range) | played by (fade frames) |
|---|---|---|---|
| 0 | -1 | (set built at run time) | Idle (blend 5) |
| 1 | -1 | (set built at run time) | Ground (15) |
| 2 | -1 | (set built at run time) | Ground sub-state 1 (15) |
| 3 | -1 | (set built at run time) | Fall (10) |
| 4 | -1 | (set built at run time) | Landing (5) |
| 5 | -1 | (set built at run time) | Jump (2) |
| 6 | 83 | `sh_fw_jump` (0-15) | Jump (2), JumpDash (5), LightDash (0) |
| 7 | -1 | (set built at run time) | Jump (2) |
| 8 | 42 | `sh_atc_dash` (0-15) | Sliding (10) |
| 9 | 60 | `sh_break_all` (0-60) | Ground braking (0) |
| 10 | 59 | `sh_break_a` (0-10) | Ground braking (0) |
| 11 | 73 | `sh_edge_otto` (1-60) | Ottotto (0) |
| 12 | -1 | (set built at run time) | Jump hold (2), SpinDash (5), Launch, HomingAttack, DarkSpin, DriveVehicle (5) |
| 13 | 44 | `sh_bra_a` (0-45) | EdgeHang (0) |
| 14 | -1 | (set built at run time) | LineHang / SomethingHang (2) |
| 15 | -1 | (set built at run time) | LineHang (2) |
| 16 | 152 | `sh_wall_jump` (0-15) | WallJump (10) |
| 17 | 156 | `sh_wall_r_a` (0-15) | ? |
| 18 | 154 | `sh_wall_l_a` (0-15) | ? |
| 19 | 158 | `sh_wall_run_r` (0-30) | ? |
| 20 | 157 | `sh_wall_run_l` (0-30) | ? |
| 21 | -1 | (set built at run time) | Grind (10) |
| 23 | 101 | `sh_glind_pick_throw` (0-50) | Grind (0) |
| 24 | -1 | (set built at run time) | Throw (0) |
| 25 | 132 | `sh_pick` (0-60) | PickupObject (5) |
| 26 | 66 | `sh_buki_pick` (0-30) | PickupWeapon (0) |
| 27 | 65 | `sh_buki_off` (0-30) | PickupWeapon (0) |
| 28 | -1 | (set built at run time) | TurnOver (5) |
| 29 | -1 | (set built at run time) | HomingJump (2) |
| 30 | 41 | `sh_atc` (0-90) | GroundAttack (0) |
| 31 | 43 | `sh_atc_eda` (0-90) | GroundAttack (0) |
| 32 | -1 | (set built at run time) | Grind (2) |
| 33 | 115 | `sh_jump_drill` (0-30) | AirAttack (0) |
| 34 | 116 | `sh_jump_drill_eda` (0-30) | AirAttack (0) |
| 35 | 120 | `sh_jump_gun` (0-6) | AirAttack (0) |
| 36 | 38 | `sh_CB` (0-240) | ChaosBlast (0) |
| 37 | 39 | `sh_CC_a` (0-106) | ChaosControl (0) |
| 38 | 83 | `sh_fw_jump` (0-15) | Launch (0) |
| 39 | 81 | `sh_flot` (0-50) | Stream (0) |
| 40 | 57 | `sh_bra_move` (0-30) | helper (10) |
| 41 | 67 | `sh_dam_m_a` (0-20) | Damage (5) |
| 42 | -1 | (set built at run time) | Dead (5) |
| 44 | -1 | (set built at run time) | JumpDash (5) |
| 45 | -1 | (set built at run time) | Damage (5) |
