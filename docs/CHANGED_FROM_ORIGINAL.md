# Changed from the original (Shadow the Hedgehog 2005 player)

Everything where `Scripts/shadow/player.lua` or the engine does something other than the game, with the game's value stored next to it. "Original" values are external: read from the Ghidra decompile of the retail `main.dol` (`shadowDecomp/decompiled_nops/pseudocode.c`, constants read from the `.dol` sections) unless marked hypothesis. Where this and `docs/SHADOW_PLAYER.md` differ, that one is the decode, this one is the list of deviations. Units: the game's are per second (hypothesis: its update gets a time step in seconds).

## Decompiled values stored here (the reference)
| What | Original value | Where |
|---|---|---|
| Min / max speed (acceleration length at rest / full) | 1000 / 15000 x table factor (index `player+0x64`; the live Shadow has index 14, factor 1.0); 3000 with player flag 0x1e | `FUN_80082cc8`, table 0x804f1838 stride 0x2c |
| Acceleration with the stick | target = 1000 + share x 14000, share = (c - 0.766) / 0.234 if speed >= 30 and c >= 0.766 else 0; new = min(target, c^8 x dt x 0.25 x 14000 + old) | `FUN_800831d8` |
| c | fabs of the cosine between the stick and the player's forward (`FUN_8000a980` = fabs, `FUN_8000a950` = near zero test); if the stick has a positive part along a second axis (`FUN_80079698`) the stick is first projected off it | `FUN_800831d8` |
| Stick strength | below 0.9 (squared 0.81): speed x 0.9 a frame (not below min), no acceleration; the stick vector is clamped to length 1 | `FUN_800831d8`, `FUN_800a7d78` |
| Drag | v' = a - k v; k = 20 on the ground (flag 2), 1 with flags 7 / 9, 10 with flag 0x1d; otherwise split along the up axis: 3 in the ground plane, 1 along up | `FUN_800936f4` .. `FUN_800938cc` |
| Gravity | -320 along up in the air, -800 on the ground (along the ground normal when `+0xa0` bit 0, set above speed 250, and not flag 0xd), -0.0625 with flag 1; added to the acceleration every frame | `FUN_800a7700` |
| Heading turn | fraction min(rate x dt, 1) of the way to the stick; rate 15 up to speed 150, falling linearly to 2 at 300, 2 above; in the air 15; jumping 6.5 | `FUN_800831d8`, `FUN_80093af8`, `FUN_80093ba0`, `FUN_80080828`, `FUN_800871d8` |
| Air | acceleration 220 along the stick (110 with flag 0x1e) | Fall, Jump |
| Jump | speed 100 along the jump direction; held acceleration 270 for at most 0.75 s | `FUN_800877c0`, `FUN_800871d8`, table 0x804b0558 |
| Ground probe | ray along minus the previous floor's normal from the body center, 2.37 x 5.0 (radius of the record, `+0x274`) + the intended move; near (<= 2 x radius) accepted, far needs a second ray (from 0.5 x (d + r) down, backwards along the move, at least r long) whose normal is within 80 degrees (0.1736) of the floor's; moves to 1 unit before the hit; only if on the floor the frame before (flag 2) | `FUN_8009728c`, `FUN_800979e8` |
| Sphere radius | 5.0 (`+0x274`) in the record; the body collision in a RAM dump is a capsule of radius 5.5, length 0 | RAM dump |
| Skate | the Ground update sets a player flag (0x3b) above squared speed 22500 (150) and a bit above 62500 (250) | `FUN_800838f4` (what the flag does: hypothesis) |
| Jump motions | `sh_jump_a` 13 frames, `sh_jump_b` 28, `sh_jump_c` 30 (a somersault curled up), then `sh_jump_idle` 60 (observed on the disc) | `SHADOW.MTP` |
| Motion playback | the game calls PlayMotion(2.0, index) in Jump, (15.0, index) in Ground: read as a speed or a blend time, unknown | `FUN_80087128`, `FUN_800831d8` |

## Deviations
Each: what the game does, what is done instead, why.

### Input (keyboard and mouse)
- **Digital stick.** The game reads an analog stick (a vector of length 0 to 1); the keys give 0 or 1 in eight directions. The "stick below 0.9" branch is therefore unreachable.
- **Camera-relative stick from the mouse camera.** The game's camera follows behind the player, so holding forward keeps the stick aligned with where he faces. Here the camera only looks where the mouse points.
- **Cosine dead zone hack** (`COSINE_DEAD_ANGLE`, `COSINE_DEAD_MIN_SPEED` in `player.lua`; **not in the original**): above speed 150 the angle between the stick and his heading counts 30 degrees smaller in the cosine that sets the speed target (only there; the acceleration still points along the real stick). Why: a flick of the mouse at speed looked like a sharp stick turn and dropped the acceleration target (cosine 0.85 gives a share of 0.4), which cut 8 to 12% of the speed in one tick with W only held (observed, logs). The cost: stick angles up to 30 degrees off cost no speed, and beyond it the penalty is lighter than the game's (60 degrees counts as 30). An earlier version of this hack kept the stick within 20 degrees of the heading (by clamping its direction), which made turning at speed much too hard (observed); that is removed. To remove this one: delete the `dead` lines marked "Keyboard and mouse hack" and the block that uses `COSINE_DEAD_ANGLE`.
- **Facing hack** (`FACE_FOLLOW_SPEED`, `FACE_SMOOTHING` in `player.lua`; **not in the original**): above 30 units/s the model faces the direction of his planar velocity (turning to it at 30 a second, the original's visual smoothing rate) instead of his heading, which at speed turns at 2 a second towards a stick the keys and the mouse move at once (observed: skating, he did not face the way he went). Below 30 the heading as before.
- **Leg log** (debug, `LEG_BONES`, `LEG_LOG_TICKS` in `player.lua`): a ring of the last 60 ticks with the state, the animation layers and, for the thighs, shins and feet, their local translation, rotation (degrees), scale and model-space position; key L prints it. The earlier deceleration report and the per-tick animation log are removed.

### Physics
- **Signed cosine** is used for `c`; the game takes the absolute value. So a stick pointing backwards slows him instead of counting as aligned. Not ported with the game's braking state (`FUN_80082df0`, sub-state 2: brake motion, no acceleration), so the two are unfinished together.
- **Speed with no stick**: measured live (`docs/SHADOW_PLAYER.md`, live log): the stored speed is `1000 + 14000 x min(1, speed / 537.5)`, a straight line; this is what the script uses (`NO_STICK_FULL_SPEED`). Before the measurement it was `speed / 750`.
- **Engine gravity off** (`sf.setWeight(e, 0)`): the script adds the game's -320 / -800 itself (`GROUND_GRAVITY` is applied as an added velocity of `-800 / 20 x (1 - e^-20dt)` after the drag, along the normal above speed 250).
- **Slope plane projection**: on the ground the velocity is put in the plane of the surface (the same rule as Sonic's script); the game moves the body with its own sphere sweep, which is not ported.
- **Collision**: this engine's capsule push-out (radius 5.5, a capsule of length 0) instead of the game's sphere sweep (`FUN_800986f0`, radius 5.0, up to 8 slide iterations). The ground probe is ported as described in `SHADOW_PLAYER.md` with its approximations (first sweep = this engine's move; stop of the second sweep computed for a plane; the ray's triangle stands for the contact; no attribute masks).
- **Grounded memory**: `SNAP_MEMORY_TICKS` (2) keeps the snap switched on a moment after the last contact; the game uses the previous frame's flag 2 only.
- **Jump grace and rising**: for `JUMP_GRACE_TICKS` (2) after a jump, and while rising after one, he counts as airborne whatever the last move reports (the engine's contact lasted a few ticks after the jump started and reset it).
- **Hold boost ends on release**: the game's held acceleration ends on a timer and a button bit; the same here (`pressed`), hypothesis for the release.
- **Tick rate** 60 (a guess, engine-wide) stands for the game's frame time.
- **Render position of a body on the floor** is extrapolated by its last move, not its velocity (engine, `Entity::renderPosition`): the ground press made the model sink and return every tick (observed).

### Animation
The locomotion blend, the jump chain, the frame ranges, the time scales and the fades follow the measured original (`Scripts/shadow/animation.lua`, `docs/SHADOW_PLAYER.md`). What still differs or is guessed:
- **Fade of the old motions**: the new motions fade in over the measured number of frames; the old ones go on playing and fade out by the rest of the weight (hypothesis: the library's exact mix of its two channels during a fade is not worked out).
- **State choice**: Idle when no stick and speed below 1 (`IDLE_SPEED`, placeholder: the original was in Ground at 10 and back in Idle at 0.19), Landing for the length of `sh_jump_e` after any air state (the original's Landing ends when its motion does; whether a held stick cuts it short is not measured), Brake from my own rule (stick against a fast run) playing `sh_break_a` once with a 5 frame fade (placeholder, not measured), Fall as the state in the air without a rising jump.
- **Debounced contact for the states** (`AIR_ANIMATION_TICKS` 8, `LAND_ANIMATION_TICKS` 3): placeholders so a flicker of the ground contact does not restart the states.
- **Euler order**: Rz * Ry * Rx (the importer's) confirmed by eye against the five others (key J).
- **Segment scale compensation** (`MotionBone::compensation`, `T * S^-1 * R * S`): hypothesis, from the three channels that equal the parent's scale in `sh_jump_a`.
- **The mix** of layers is a weighted average of translation, slerped rotation and scale per frame of the skeleton (the library's own blend math is not decompiled).
- **Model facing**: `FACE_YAW_OFFSET` = pi (observed: faced backwards without it); the game's own orientation convention is not decoded.
- Removed since the measurement: the guessed skate thresholds (150 / 250), the walk / run split at 30, the jump playing at twice the speed, the 0.1 s cross-fade of the engine (`playMotion` still uses it; the Shadow script no longer does).

### Not ported
Homing attack, spin dash / dark spin, skate's special physics, Chaos Control / Chaos Blast, weapons, damage and death, sliding, wall jump, edge hang, grind, light dash, vehicles, launch objects, the braking state, the camera.

### Found by comparing the port's animation log (key L) with the original's recording
- The port's jump chain matches the original frame for frame on a standing jump: `sh_jump_a` + `sh_jump_fa` 13 frames, `sh_jump_b` (21 to 27) + `sh_jump_fb` (14 to 19.14, scale 7/6) 7 frames, `sh_jump_c` looping 20 to 29, the 10-frame fade into `sh_jump_d` (53 to 67 looping), the 5-frame fade into `sh_jump_e` (69 on). Locomotion pairs, weights and time scales also match the formulas.
- Fixed: the animation read the speed with the ground press included (11.3 at rest), so a standing Shadow with the stick pushed started the ground blend at 0.9 of the rate. It now reads the speed in the plane of the surface (`animSpeed`).
- Hypothesis added: a pushed stick ends the landing state at once (the original's landing was recorded with no stick).
- Known delay: the landing state starts 2 ticks after the ground contact (the contact debounce, `LAND_ANIMATION_TICKS` 3); the original's Landing starts on the contact.

- **Jump dash trigger (hypothesis)**: the port starts the dash with a new press of the jump button in the air after a jump, once per time in the air. The original's command that starts it is not read (docs/STH2005_PLAYER_REFERENCE.md section 20); the dash itself (180 added along the facing, 430 along the facing and 220 along the stick for 0.4 s) is decoded and matches two recorded dashes.
- **Jump direction**: the port jumps along the ground normal like the original (section 20); the animation choice for a mostly horizontal jump direction (`sh_fw_jump` instead of `sh_jump_a`) is not ported.
