# Shadow the Hedgehog (2005, GameCube, `GUPE8P`): the player system, as mapped so far

A reference of what is known about how the original's player works, collected while porting it. It is meant to stand alone: addresses are for the retail `main.dol` (the disc's, USA); offsets are into the objects named. Every claim carries its evidence:

- **decompile** (D): read from the Ghidra decompile of `main.dol` (`shadowDecomp/decompiled_nops/pseudocode.c`: Gekko paired-single instructions are nopped there, so vector math can be missing) and from constants read in the `.dol` sections;
- **live** (L): measured on the running game with `tools/shadowLiveLog.py` (Dolphin RAM through dolphin-memory-engine), or read from a RAM dump (`shadowDecomp/mem1.raw`, one stage, Shadow standing);
- **disc** (F): read from files of the disc (`character/shadow.one`: `SHADOW.MTP`, `SH.BON`, ...);
- **hypothesis** (H): mine, to be tested.

Units: the game's per-frame values are per second where the code multiplies by the frame time `dt` (1/60 in the dumps). Companion documents: `docs/SHADOW_PLAYER.md` (the working notes and logs, in the order things were found), `docs/CHANGED_FROM_ORIGINAL.md` (what the port does differently), `docs/ARCHITECTURE.md` (the port).

## 1. How to read the running game
`tools/shadowLiveLog.py` (needs `pip install dolphin-memory-engine` in a venv and `kernel.yama.ptrace_scope=0` on Linux) finds the player by scanning RAM for the word `0x80521c50` at `+0x38` of an object whose `+0x3c` reads `Shadow`, then samples the player, its executer, the current behavior and the animation object every game frame into a CSV. In the build used here the player is at `0x80f13780` (L); it moves between runs of other stages, so scan. Object first words are vtable addresses plus 0xC (L: Ground's vtable 0x80520690 reads 0x8052069C).

## 2. The frame
The per-frame update is the executer's (`FUN_800a7d78`, D): `dt` from the time block `*(player+0x60)` (floats 1/60 and 1.0: the frame time and a global time scale); `FUN_800a72e4` (input: the stick), `FUN_80078f64` (an invulnerability-type timer, flag 0x18), `FUN_800a7700` (gravity, section 4), the current behavior's Update (`behavior->vtable+0x28`, through `FUN_800a750c` etc.), then **it zeroes the acceleration (`+0x24`) and the stick (`+0xb8`), clears flag 0x1b and copies the position into `+0xd0`** (the "previous position" of the next frame). Behaviors add to `+0x24` and move the position through the integrator (section 3) and the ground resolve (section 5). So when read from outside, `+0x24` and `+0xb8` are mostly 0.

## 3. The integrator (D, L)
`FUN_800936f4` -> `FUN_80093728` -> `FUN_800938cc`: with a drag `k` the velocity `v` and acceleration `a` give, over the step `dt`, `v' = a/k + (v - a/k) e^(-k dt)` and a position step `a/k dt + (1 - e^(-k dt))/k (v - a/k)`. `k`: 20 with player flag 2 (on the ground); 1 with flags 7 or 9; 10 with flag 0x1d; otherwise the vectors are split along the up axis `+0xf4` (`FUN_8040f990` = projection) and the part in the ground plane gets 3, the part along up gets 1. L: with no stick on the ground the planar speed falls by exactly 0.717 (= e^(-1/3)) per frame.

## 4. Gravity (D)
`FUN_800a7700`: if player flag 0 is set, add to `+0x24` an acceleration of **-320** (air), **-800** (flag 2, on the ground), **-0.0625** (flag 1) times the direction `+0xf4`; or, when `+0xa0` bit 0 is set (the Ground update sets it above a squared speed of 62500, i.e. 250) and flag 0xd is clear, along `+0x1c8` (the ground normal), which presses a fast player into loops and walls (H: that is its use). `+0xf4` is copied from `+0xe8` every frame (unless flags 0x39 / 0x3e); `SetGravityCommand` (0x8009a298) writes `+0xe8`, so gravity zones change the direction. With the drag the vertical speed settles at -320 in the air and -40 on the ground.

## 5. Ground resolve and the ground probe (D)
`FUN_8009728c` (called by behaviors after the integrator): a sphere sweep (`FUN_800986f0`, radius `+0x274` = 5.0 (L), up to 8 slide iterations; contacts count as ground when their normal's dot with `+0x1c8` is at least 0.1736 = cos 80 degrees) from the position of the frame before along the move, then, **only if flag 2 was set the frame before**, a probe: a ray from the sphere's center along **minus `+0x1c8`** of length `2.37 x radius + |move|` (`FUN_800979e8`: nearest hit, any normal); a hit nearer than 2 x radius is accepted, a farther one needs a second ray (from the point 0.5 x (d + radius) down, backwards along the move for at least a radius) whose hit normal has a dot of at least 0.1736 with `+0x1c8`; if accepted the sphere is swept along the ray to 1 unit before the hit; ground is whether that contact's normal is within 80 degrees of `+0x1c8`, and it becomes `+0x1c8` (`FUN_8009339c` smooths a copy into `+0x17c`, `+0x188`). `PressGroundCommand` (0x8009a184) sets flag 2. Flag 0xd selects another path (not read). Attribute masks `0x1a93` (| 8 with flag 2) filter the contacts.

## 6. The player object (offsets; D unless marked)
| offset | what |
|---|---|
| +0x00 | position (vec3) |
| +0x0c | orientation (euler, radians; +0x10 is the yaw `FUN_80093ba0` interpolates) |
| +0x18 | velocity |
| +0x24 | acceleration (zeroed every frame) |
| +0x30 | 2 at construction |
| +0x38 | vtable of the player interface (`0x80521c50` for `PlayerShadow`, L); +0x3c the name `Shadow` (L) |
| +0x54 | id (-1 = none) |
| +0x58 | back pointer |
| +0x60 | pointer to the time block {1/60, 1.0, ...} (L) |
| +0x64 | character / type index (14 for Shadow in the dump; indexes the table at 0x804f1838 stride 0x2c: first float = max speed factor, +0x14 an effect id) |
| +0xa0 / +0xa4 | 32-bit flag words (bits tested with `1 << n`) |
| +0xa8 | the main flag set (bit n = word n>>5 at +0xa8 + 4 (n>>5), bit n&31: `FUN_8006cd78`); +0xb4 another set |
| +0xb8 | the stick (not normalized; zeroed every frame) |
| +0xc4 | vec3 written by the animation (root motion delta, H) |
| +0xd0 | position of the frame before |
| +0xe8 | gravity direction, +0xf4 up axis, +0x100 gravity acceleration (temp) |
| +0x10c..+0x14c | orientation matrices from the up axis |
| +0x17c / +0x188 / +0x198 / +0x1c8 / +0x1d4 / +0x1e4 | ground normal smoothing; **+0x1c8 the ground normal** |
| +0x218..+0x254 | sub-objects: +0x224 the command/behavior manager, **+0x22c the executer**, **+0x230 the animation object**, +0x234 sound, +0x238 weapon/controller, +0x23c effects, +0x240 the hand/eye motion object, +0x248 collision, +0x24c, +0x250, +0x254 |
| +0x260 | handle to self; +0x268 / +0x26c the entity and the name holder |
| +0x274 | collision radius (5.0, L) |
| +0x278 | probe/collision query object |
| +0x27c | float timer (invulnerability, counts down by dt, flag 0x18 while above 0) |
| +0x290 | a bit set (flag 0x1e is derived from it: `FUN_80077180`, H: weapon held) |
Executer (`ShadowExecuter`, vtable 0x804b3624..): +0x28 the player, **+0x30 the current behavior**, +0x38 the behavior factory interface, +0x48 a float timer, +0x58 behaviors' owner.
Behavior base: +0x04 the player, +0x1c the sub-state, +0x20 the step inside it. Ground: +0x24 min speed (1000), +0x28 max speed (15000 x factor), +0x2c current speed, +0x30 turn rate, +0x34 an angle (L, section 8). Jump: +0x20 the jump direction (vec3), +0x2c the hold timer, +0x30 button bits (bit 0 held, 1, 2, 3, 4, 5), +0x40 a counter.

## 7. Player flag bits (the set at +0xa8; seen set in the live log are 0x0 0x2 0x3 0x3b 0x3e 0x46 0x47 0x61 0x64 0x65 0x6a 0x6b ... and 0x1b/0x1c)
| bit | meaning |
|---|---|
| 0x00 | gravity applies (D: `FUN_800a7700`) |
| 0x01 | the low gravity (-0.0625) |
| 0x02 | **on the ground** (D, L: set by the ground resolve and `PressGroundCommand`, cleared at the start of a jump) |
| 0x03 | speed of 100 or more (L: set at 103, absent at 100; D: set by the Ground update; selects effects) |
| 0x0d | alternate ground resolve path (D) |
| 0x18 | invulnerable/blinking while the timer `+0x27c` runs (D) |
| 0x1b | the stick is pushed this frame (L: appears and disappears within frames; cleared by the executer) |
| 0x1e | restricted-speed mode: max speed 3000, air acceleration 110 (D; H: a heavy weapon is held) |
| 0x26 | animation locked: PlayMotion does nothing (D) |
| 0x2e | no braking sub-state (D) |
| 0x35 | jump active (D: set by Jump's Enter) |
| 0x38, 0x31 | set by the jump/ground states (D); 0x38 stops the orientation smoothing of `FUN_800a750c` |
| 0x3b | speed above 150 (L: first seen at 170, absent at 147; D: the Ground update compares the squared speed with 22500) |
| 0x3c | the Ground update's alternate sub-state 1 (current speed forced to 2200, animation index 2) (D; H: a boost/forced-run state) |
| others | seen set, not read: 0x3e 0x46 0x47 0x61 0x64 0x65 0x6a 0x6b 0x1c 0x4 |

## 8. Ground behavior numbers (D, L)
Min speed 1000, max speed 15000 x `table[+0x64]` (3000 with flag 0x1e); with the stick: current speed `+0x2c` = min(target, cos^8 x dt x 0.25 x 14000 + old) where the target is 1000 + 14000 x (c - 0.766)/0.234 if the speed is 30 or more and c >= 0.766, else 1000 (L: +58.3 per frame for c = 1, linear, confirmed), c the absolute value of the cosine between the stick and the forward vector; acceleration = `+0x2c` x the stick vector (analog: its length scales it, L: the start's 0.24, 0.38, ... of 1000); stick below 0.9: `+0x2c` x 0.9 per frame (L: 11212, 10091, 9082); no stick: `+0x2c` = 1000 + 14000 x min(1, speed / 537.5) from a speed of 30 (L: slope 26.05); top speed = `+0x2c` / 20. Heading: moves by min(rate x dt, 1) of the way to the stick, rate (`+0x30`): 15 up to speed 150, 13 x ((speed - 300) / -150) + 2 to 300, 2 above (L: 13.25, 10.22, 2.08 at 170, 205, 299). `+0x34`: an angle function of the cosine, 360 with no stick. Air: acceleration 220 along the stick (110 with flag 0x1e), heading rate 15; Jump: speed 100 along the jump direction, held acceleration 270 for 0.75 s, heading rate 6.5.

## 9. The behaviors
Behavior objects are made by a factory (`Player::Behavior::Factory`, 0x804b3198) when a command asks for one; the executer holds the current one at +0x30. The names are from the RTTI strings (D); the slots are the vtable's (Enter is `Enter(self, ?, InitializeArgs)`, Update `Update(dt, self)`; slot 4 and 6 vary):

| behavior | object vtable (+0xC = first word) | Enter (slot 2, args: self, ?, InitializeArgs) | Exit (3) | Update(dt, self) (5) |
|---|---|---|---|---|
| AirAttack | 0x805203b8 | 0x8007a9bc | 0x8007a958 | 0x8007a640 |
| ChaosBlast | 0x80520414 | 0x8007b304 | 0x8007af58 | 0x8007acdc |
| ChaosControl | 0x80520454 | 0x8007c334 | 0x8007bf78 | 0x8007ba58 |
| Damage | 0x80520494 | 0x8007cd40 | 0x8007ccdc | 0x8007cbb8 |
| DarkSpin | 0x805204c8 | 0x8007d500 | 0x8007d454 | 0x8007d2b8 |
| Dead | 0x805204f8 | 0x8007d960 | 0x8007d900 | 0x8007d7e8 |
| DriveVehicle | 0x80520528 | 0x8007e9c8 | 0x8007e8d8 | 0x8007e58c |
| EdgeHang | 0x80520570 | 0x8007fa74 | 0x8007f710 | 0x8007f6cc |
| ExternalControl | 0x805205ac | 0x80080364 | 0x8008020c | 0x80080120 |
| Fall | 0x805205e0 | 0x80080d90 | 0x80080d14 | 0x80080828 |
| Grind | 0x80520640 | 0x8008248c | 0x8008209c | 0x80081950 |
| Ground | 0x80520690 | 0x80083da4 | 0x80083ca0 | 0x800838f4 |
| GroundAttack | 0x805206cc | 0x80084938 | 0x800845c0 | 0x80084170 |
| HomingAttack | 0x8052073c | 0x80085e9c | 0x8008580c | 0x800854e4 |
| HomingJump | 0x80520794 | 0x8008697c | 0x80086908 | 0x800867e4 |
| Idle | 0x805207c8 | 0x80086ea0 | 0x80086dd8 | 0x80086c40 |
| Jump | 0x80520804 | 0x800877c0 | 0x800876a4 | 0x800871d8 |
| JumpDash | 0x80520844 | 0x80088804 | 0x80088494 | 0x800882a4 |
| Landing | 0x80520890 | 0x80088f68 | 0x80088ee0 | 0x80088d5c |
| Launch | 0x805208c0 | 0x800894c8 | 0x80089404 | 0x80089100 |
| LightDash | 0x805208f0 | 0x8008a2d0 | 0x8008a220 | 0x80089c20 |
| LineHang | 0x80520920 | 0x8008b648 | 0x8008b4bc | 0x8008b088 |
| Ottotto | 0x80520950 | 0x8008bd84 | 0x8008bd58 | 0x8008bca4 |
| PickupObject | 0x80520998 | 0x8008c5c4 | 0x8008c274 | 0x8008bf30 |
| PickupWeapon | 0x805209f0 | 0x8008d264 | 0x8008cf58 | 0x8008cd0c |
| Sliding | 0x80520a20 | 0x8008df9c | 0x8008df1c | 0x8008dcd4 |
| SomethingHang | 0x80520a50 | 0x8008e930 | 0x8008e748 | 0x8008e360 |
| SpinDash | 0x80520a80 | 0x8008f5ec | 0x8008f508 | 0x8008f1ec |
| Stream | 0x80520ab0 | 0x8008f93c | 0x8008f910 | 0x8008f814 |
| Throw | 0x80520af8 | 0x8008fec8 | 0x8008fb94 | 0x8008fae8 |
| TurnOver | 0x80520b40 | 0x80090c74 | 0x80090928 | 0x800906e4 |
| WallHang | 0x80520b88 | 0x800921d4 | 0x80092128 | 0x800919a8 |
| WallJump | 0x80520be8 | 0x80092c14 | 0x800928d0 | 0x8009270c |

Commands that switch behavior (RTTI names, D): `StartJumpCommand`, `EndJumpCommand`, `HomingJumpCommand`, `HomingAttackCommand`, `SlidingCommand`, `SpinDashCommand`, `GrindCommand`, `LightDashCommand`, `DarkSpinCommand`, `ChaosControlCommand`, `ChaosBlastCommand`, `LaunchCommand`, `StreamCommand`, `TurnOverCommand`, `ThrowCommand`, `PickupObjectCommand`, `PickupWeaponCommand`, `TouchHangCommand`, `TouchLineCommand`, `StartExternalControlCommand`, `GetOnVehicleCommand`, `ElectricCircuitCommand`, `CancelBehaviorCommand`, `ResetCommand`, `SetGravityCommand`, `PressGroundCommand`, `IgnoreInputCommand`, `InvincibleCommand`, `SetMotionCommand`, `SetExternalMotionCommand`, `StartActionCommand` / `EndActionCommand`, `StartAttackCommand` / `EndAttackCommand`, `ToSuperCommand`, weapon commands. A behavior sends a command to the executer (`FUN_800a7f14(executer, command)`) to change state: the argument's word at +4 is the command id (e.g. 2 = back to Ground, 3 = Fall, observed in Ground/Jump/Fall code).

## 10. The animation system
**Object** (`ShadowMotion`, at player+0x230, vtable 0x80522260; D, L): +0x3e0 a base number, **+0x3e8 the mixer**, +0x420 / +0x424 the two motion numbers wanted, +0x428 the previous and **+0x42c the current animation index**, **+0x430 the first argument of PlayMotion (the fade length in frames)**, +0x434 (-1 normally), **+0x438 the rate** (from the speed, below), +0x43c the global scale (1.0), +0x440 / +0x444 the motions the mixer has, **+0x448 the weight of the second motion**, **+0x44c the time scale of the first** (the second's follows), +0x458 mode, +0x460..+0x474 six values copied from the state's block, +0x478 dt, +0x7e0 / +0x7e4 / +0x7e8 the hand, eye and weapon motion objects. Slots of 0x38 bytes from +0xd0 are per-index records (`FUN_800aa760`).
**PlayMotion(blend, index)** (`FUN_800b5210`, vtable +0x10): refuses if player flag 0x26; if the index differs from the current one: previous = current, current = index, `+0x430` = blend, and if the table entry (0x804b38a4 + 16 index) holds a motion number >= 0 it becomes the wanted motion (`+0x3e0 + number - 1`). **Motion numbers are 1-based into `SHADOW.MTP`'s table** (F, L). The per-frame update (`FUN_800b5320`, `FUN_800b4804`) runs the mixer.
**Mixer** (the animation library, 0x804125f4..0x80419f5c; L): +0x00 reads 30; +0x10 the channel array (0x130 bytes each), +0x20 the channel count (2), +0x28 the slot table (channel numbers, -1 none). Per channel: **+0xac the current time, +0x12c the time before, +0x108 the time scale** (`FUN_80416dc8`), +0x110 the motion (u16), +0x90 / +0x94 flags (0x200: has a fade block), **+0x120 a pointer to the fade block**: +0x00 flags, +0x04 / +0x08 types, **+0x0c / +0x10 weights** (`FUN_80416014`), **+0x18 the fade length** (`FUN_80416148`). Time advances by the time scale every game frame (60 a second) inside the motion's frame range and loops to its start at its end.
**Motion data** (F): the header has **two u16 at +0x10 and +0x12: the range the game plays** (start, end), e.g. `sh_jump_b` 21-28, `sh_jump_c` 20-30, `sh_jump_e` 68-102, `sh_idle` 0-100; the rest as in `src/Importers/STH2005/Mtp.cpp` (tracks of keys; three extra channels per bone that hold the parent's scale: a scale compensation, H). The table's third word per motion points at event data (footstep and sound frames, `ff ff` terminated).
**Fades** (L, exact): the new state's motions' weight rises linearly 0 to 1 in `+0x430` frames: 15 into Ground, 5 into Idle and Landing, 10 into Fall, 2 into the jump, 0 between the jump's stages. **Time scales of a mixed pair** (L, exact on every row): `scaleB = 1 / ((la/lb - 1)(1 - w) + 1)`, `scaleA = scaleB x la/lb`, la and lb the range lengths.
**Ground locomotion** (L): the rate (`+0x438`, from the speed: v/12.5 below 12.5; 1 + (v-12.5)/17.5 to 30; 2 + (v-30)/70 to 100; 3 + (v-100)/620 to 720; 4) picks the pair (rate 0-1 `sh_idle`+`sh_walk`; 1-2 `sh_walk`+`sh_run`; 2-3 `sh_run`+`sh_slow_skate`; 3-4 `sh_slow_skate`+`sh_high_skate`) and the weight is its fraction. **Jump** (L): idx 5 `sh_jump_a`+`sh_jump_fa` (weight = planar speed / 700) for 13 frames, idx 12 `sh_jump_b`+`sh_jump_fb` from their start frames for 7, `sh_jump_c` looping until the apex; Fall (3) `sh_jump_d` looping (53-68); Landing (4) `sh_jump_e` (68-102) once, then Idle (0) `sh_idle`.

### Animation index table (static: `main.dol` 0x804b38a4, 16 bytes per index; PlayMotion calls of the behaviors)
See the end of `docs/SHADOW_PLAYER.md` ("Animation index table") for the index, the motion, its range and which behavior plays it with which fade. Entries with -1 build a set at run time (Idle, Ground, Fall, Landing, the jump stages, Grind, TurnOver, Dead, JumpDash...): only those marked L above are measured.

## 11. Other facts
- Object type ids, `.BIN` recipes, `.CCL` collision layout, placement files: see `docs/ARCHITECTURE.md` (importers) and the notes there.
- Characters indexed by `+0x64`: the table at 0x804f1838 has rows for 0-19 (rows 10-12 hold other floats: 25, 10, 15 / 50, 10, 80; rows 0-9 and 13-19 hold the speed factor 0, 1.0, 0.9, 1.0, 0.6, 0.9, 0.8, 0.8, 1.0, 0.4, ... 1.0): H: character or form ids (Shadow's is 14 in the dump).
- The player's `Player::Shadow::*` classes: `ShadowBehavior`, `ShadowControl`, `ShadowExecuter`, `ShadowMotion`, `MotionHand`, `MotionEye`, `PlayerTarget`, `ShadowWeapon` (guns, rifles, cannons, the vacuum, sticks), `Super`, `Electric`, `Sound`, `MotionSub`.
- Not decoded: the Idle/Ground switch, braking, damage and death flows, homing target selection, Chaos Control/Blast, skate beyond the locomotion blend, the camera, weapons, the effect (+0x23c) system, sounds.

## 12. Gaps to fill with the logger
Standing jumps (tap, held), the landing with the stick held, braking, damage, homing, spin dash, sliding, wall jumps, ledge grabs: record each with `tools/shadowLiveLog.py <name>.csv` and write down what was done and when; the animation columns give the state, the motions and fades, the player columns the physics.

## 13. Motion scale channels: what the data shows (F), and the debug switch
Scale (`scale` tracks, the last three of a bone's nine) and the three channels before them are not 1 in several motions the game plays all the time, not only the jumps: `sh_walk` (scale on the thighs, shins and chest `sh_j_mune`; the channels on the shins, feet, head, upper arms), `sh_slow_skate` and `sh_high_skate` (shins, feet, toes; channels on the feet), `sh_jump_d` / `sh_jump_e` (chest, arms, shins), `sh_jump_idle` (chest; head and upper arms); `sh_idle`, `sh_run`, `sh_break_a` and `sh_jump_c` have none. In `sh_jump_a` the channels equal the parent's scale (L, F), in `sh_jump_idle` they do not (the head's differ from the chest's constant 1.02): so "the channels are the parent's scale" (the compensation hypothesis) holds at best for some motions. Reading them wrongly distorts every blend that includes such a motion (observed in the port: the models stretch, the jump ball is fine). The port has a debug switch to judge by eye (`sf.setAnimationScaleMode`, key K in `Scripts/shadow/player.lua`): 0 scale + compensation, 1 neither, 2 scale only. **Result (observed by the user, key J):** order 0, Rz * Ry * Rx, the importer's, is the one that looks right; the others were rejected by eye.

### 13a. What the scale data is (F, analysis over all 122 `sh_*` motions) and the current default
- **The three channels before a bone's nine equal the scale of its parent bone**: in 13493 of 13940 samples where either is not 1 (97%; 100% in `sh_walk`, `sh_jump_d`, `sh_jump_e`, `sh_pick_idle`, `sh_dam_m_f_b` ...; parents from `SH.BON`: head and upper arms under `sh_j_mune`, shins under thighs, feet under shins, hands under forearms). So the compensation reading of the data is right; what the game does with it is not known.
- **Where the scales are** (min..max): `sh_walk`: both thighs and shins x and y 1.26 (constant: thicker legs, z the length axis untouched), `sh_j_mune` y 0.85..1.15 (breathing); `sh_slow_skate`: shins z 1.0..1.5, shoes z 1.0..1.15, toe `sh_j_m1toge2` z 0.76..2.24; `sh_high_skate`: shins z up to 2.0, shoes up to 2.0, forearm z 1.9; `sh_jump_a`: chest y up to 2.83, right shin z up to 2.91; `sh_jump_d`: chest y 2.0..2.4, shins and forearms z about 2.0..2.2; `sh_jump_e`: chest y 0.56..2.43; `sh_idle`, `sh_run`, `sh_jump_c`, `sh_break_a`: none.
- **Observed in the port**: applying them as multipliers (with the compensation, mode 0) made every model with scale data look stretched (the takeoff, the walk, the skates), while `sh_jump_c`, which has none, looked right. The port therefore defaults to ignoring them (mode 1: translation and rotation only); `sf.setAnimationScaleMode` / key K switches. Not settled: whether the game ignores them for the skinned character (RenderWare animation keys have no scale; hypothesis), applies them as inverses, or uses them only for something else (effects, the ball's shell). To settle it: look at the running original during `sh_jump_a` (does the chest stretch 2.8 times?) and at the skate (do the shins lengthen by half?).

### 13b. The translation tracks already contain the parent's scale (F), and what the port does now
For the bones whose parent has a scale in a motion, the translation track of the bone equals its rest offset times the parent's scale, per axis: `sh_jump_a`: `sh_j_head` y 1.35 -> 3.82 (chest y scale 2.83), `sh_j_l1ude1` / `sh_j_r1ude1` y 1.05 -> 2.97, the right foot `sh_j_r1kutu1` z 2.53 -> 7.34 (shin z scale 2.91), the hands z 1.35 -> 2.52 (forearm scale 1.87); `sh_slow_skate` / `sh_high_skate`: the feet z 2.53 -> 3.74 .. 5.09 (shin z scales 1.5 .. 2.0); `sh_walk`: none (the thigh and shin scales are on x and y, the offsets along z). So the scale channels are not decoration: the offsets are baked for them. Consequences observed in the port: scale applied as a plain hierarchy (offsets counted twice) stretched every model without limit; scale ignored (mode 1) left the baked offsets stretched with nothing to stretch the bones (the takeoff stretched less, the skate's feet floated off the shins, "legs move weirdly").
Current reading (hypothesis, mode 0 in `ONEInstance.cpp`, the default): the offset is divided by the parent's scale (the three channels), the hierarchy multiplies it back, and the bone cancels the parent's scale in its own orientation and shape: `local = T / S_parent * S_parent^-1 * R * S`. The chest is stretched by its own scale, the head and arms sit where the baked offsets put them at their own size. Whether the original really shows a stretched chest at the takeoff (about 0.2 s) is for you to confirm on the running game.

## 14. Orientation (L, F)
The player's yaw (`+0x10`, radians) while running along +x (velocity (200, 0)) reads about -1.58 = -pi/2, and over four samples `yaw = -atan2(vz, vx) - pi/2` (residual below 0.06), in the original's own world; the original's yaw stays within 0.16 rad of the velocity's direction over a run from 40 to 300 (L, `tools/live_run.csv`). The skeleton's front is -Z in the file (F: running leans `sh_root` by -0.37 about x, the head towards -z). The port draws every ONE model with a 90 degree yaw (`RenderWareYawDegrees`, `ONEInstance::draw`) and turns the level the same way; the entity rotation `pi - heading` (`FACE_YAW_OFFSET = pi` in `Scripts/shadow/player.lua`) puts the front on the heading at low speed (observed). Tried and observed wrong: offset 0 (faced backwards), offset -pi/2 (the original's yaw, 90 degrees off), and removing the 90 degrees from the instances (kept for the level) with offset -pi/2: also 90 degrees off, so the instance's yaw was not the reason, and it was put back.
At speed (skating) the port's Shadow does not face the way he goes (observed): see section 15 for what is being tested (the order of the Euler angles of the motions).

## 15. Skating: what is being tested
The skate motions are the first ones with large rotations about several axes at once (`sh_slow_skate`: thighs 0.7..3.3 about x with y -0.7..0.7 and z -0.6..-0.2, hips 0.6 about y, chest pitch -0.7; `sh_high_skate`: chest pitch -1.04..-1.37 on top of the root's -0.37, head pitch +1.1..1.5), and the port's Euler order (Rz * Ry * Rx, a hypothesis fixed only by the rest pose, which has y of about 0) is the prime suspect for the skating pose looking wrong. `sf.setAnimationEulerOrder` / key J cycles the six orders (0 zyx, 1 yzx, 2 zxy, 3 xyz, 4 xzy, 5 yxz; the rest pose allows only those with z before x: 0, 1, 2). Result to be written here once seen. Facing at speed: the port makes the model face his planar velocity above 30 units/s (a keyboard-and-mouse hack, `docs/CHANGED_FROM_ORIGINAL.md`).

## 16. How the model is oriented on the ground (D)
The orientation the model is drawn with is a matrix built from three things (`FUN_80079628`, used by `FUN_800796dc` forward = local (0, 0, -1) and `FUN_80079698` up = local (0, 1, 0)): the basis `+0x1e4` of the smoothed ground normal, then the yaw `+0x10` about it (and `+0xc`, `+0x14`, normally 0). So he stands on slopes and turns about his own up axis.
- `FUN_8009339c` (end of the ground resolve): `+0x1c8` = the ground normal of the frame (the surface it found if on the ground, else the up axis `+0xf4`); `FUN_800934fc` turns (0, 1, 0) to it about the axis their cross product gives by the angle `acos(dot)` (a quaternion in `+0x1d4`, matrix `+0x1e4`); the smoothed version `+0x188` / `+0x198` goes towards `+0x1d4` by a slerp with the fraction `t = min(0.2, 15 degrees / angle)` (`FUN_800704ac` is a min, `0x805e459c` reads 0.2, `15.0` is `DAT_805f29ac`), and `+0x17c` = `+0x198` applied to (0, 1, 0), the smoothed normal.
- `+0xf4` (the up axis the gravity and the behaviors use) is copied from `+0xe8` (the world's gravity up), or from the ground normal `+0x1c8` when the stick is not pushed (flag 0x1b clear) and the normal is within `DAT_805e4594` degrees of the gravity up (`FUN_8009330c`, `FUN_800932b4`); the threshold `DAT_805e4594` was not read.
- `FUN_800a750c` smooths `+0xc` (the Euler angles of the orientation) towards (0, yaw `+0x10`, 0) by `min(30 dt, 1)` each frame unless flag 0x38 is set.
The port (`sf.orient`, `Scripts/shadow/player.lua`) keeps a smoothed normal with that rule (a normalized mix instead of the quaternion slerp) and draws the model with the basis of it times the yaw.

## 17. Phase of a motion entering a blend pair (L)
In the Ground blend a motion that enters the pair starts at the phase of the one already playing, not at its first frame: when the pair went from run and slow skate to slow skate and fast skate (live log, `tools/live_run.csv`, t = 2.194) the fast skate's channel read 20.136 (of 45) while the slow skate's read 26.848 (of 60), both 0.4475 of their cycle; the walk/run pair (0.725 of 60 and 0.423 of 35, both 0.012) and the run/slow skate pair (1.256 of 35, 2.154 of 60, both 0.0359) are the same. So the two cycles are in step from the start. The port started a new motion at its first frame, which blended the two skates a third of a cycle apart (found by the leg log: slow skate at frame 28.6 and fast skate at 2.6), one probable reason for the weird legs while skating; `advance` in `Scripts/shadow/animation.lua` now starts it at the phase of the first motion of the pair already playing.
Second cause found with the leg log: layers were mixed as matrices (`mixTransforms` pulls a matrix apart into a translation, a rotation and a scale by the column lengths). A bone with the parent-scale compensation has `local = T * S^-1 * R * S`, which is not a rotation times a scale (it has a shear when `S^-1` and `S` differ), so pulling it apart gave wrong rotations and scales for exactly the bones that have the three channels (feet, hands, head, upper arms; in the log the feet's scales read 0.84 to 1.07 where the motions' own are 1.0 to 1.15 on one axis). The pose layers are now mixed part by part (`BoneSample` in `ONEInstance.cpp`: translation, rotation as a quaternion, the inverse compensation, the scale) and composed once.

Port bug found in a leg log of the rare skating-legs glitch (60 ticks, t=1620-1679; port observation, not the original): skate (0-60) and highSkate (0-45) ran as a pair with a constant phase offset of 0.556 of a cycle (skate 56.07/60 against highSkate 17.01/45 at t=1620, 34.44/60 against 0.79/45 at t=1654), although the pair's time scales keep the phases equal once they match. Hypothesis for the cause: `advance` in `Scripts/shadow/animation.lua` kept the time of a motion that had left the pair, so after speeding up, slowing into the lower band and speeding up again without stopping, highSkate re-entered at its stale time instead of the playing motion's phase. Changed: a motion not in the current pair forgets its time. The log does not show how the offset arose, so this is unconfirmed until the glitch stops reproducing.

## 18. Feet against the ground: standing and skating (L, observed from the user's leg logs; cause unknown)
- Observed (leg log, standing, t=485-544): the left ankle `sh_j_l1kutu1` stays at y 0.70 above the model origin, the right one goes 0.78 to 1.10; root 4.0-4.5. The user sees him slightly above the ground.
- Observed (leg log, skating, t=1922-1976): the planted ankle's lowest y is about 1.03 (higher than standing), the shin `asi2` z scale reaches 1.54. The user sees the feet underground and stretched.
- `sh_j_*te` are the wrists (hands), not toes: they cannot be used for the sole.
- Hypothesis: the two are different causes (a pure height offset would move both the same way): standing = the ankle-to-sole depth is less than 0.70; skating = the shin's z stretch reaches the foot mesh. To measure: `sf.lowestPoint(entity)` puts `lowestVertex(posed, rest)` (the lowest skinned vertex y in model space) in the leg log header; the rest value is the sole of the unposed model.

## 19. Westopolis (stg0100): collision against the drawn floor (live measurement of our import, from the user's run)
- Observed (user): in Westopolis both Sonic and Shadow stand slightly above the ground, in Emerald Coast not; the collision lines view looks offset upward from the drawn level.
- Measured (our importer, user's run, temporary prints since removed): the bounds of every drawn `.RG1` world's vertices and of every collision `.BSP` ("COLI") world's vertices, in the file's own space, agree where they cover the same area (e.g. section 02 min y -6522.17 for both, 09 -6964.93, 22 -6339.76). So the two use the same space, and no translation or yaw difference between them is in the data as read.
- Measured (same run): 1305 sampled floor-like collision triangles, drawn floor height minus collision height at the triangle's center: 0.0 for 880 (67%), -1.0 for 244 (19%), -0.5 for 57 (4%), +-others rare; 70 had no drawn floor within 200 units. So the collision floor is 1 unit above the drawn one on about a fifth of the samples and equal elsewhere: there is no constant offset.
- Hypothesis: the 1 unit is how the game's own data is (the collision mesh is authored 1 unit above the visible road in places), not an import error; it would explain "slightly above ground" in Westopolis only. Unverified against the running game. Not corrected in the engine (CLAUDE.md: reproduce observed behaviour).
- Observed (user, collision lines view): the collision matches the sidewalk and sits above the road, because it does not follow the road's bend. This is the user's reading of the original data: a quirk built into the game's Westopolis collision, which supersedes the plain hypothesis above. Left as is.
- Observed (user): the skating feet going underground is the same quirk, not a pose or skinning fault: he skates correctly on the road and sinks into the sidewalk (where the road is below the collision). In the original it is not visible because while skating he glides above both surfaces, offset by the reactive effect from his shoes. So the section 18 skating hypothesis (the shin's z scale carried into the foot mesh) is superseded; the stretch is not a separate cause. Not reproduced: the glide offset and the shoe effect.
- Measured (live log `tools/live_run.csv`, the user's run, read by me: 598 rows, 8.8 s, a single short skate on the flat start area, planar speed 100 to 327 from t=2.19 to 3.53, then a jump): the player's position y does not rise while skating. Idle -5057.4536, Ground at speed 0 to 100 -5057.42 to -5057.45, skating -5057.4150 (drifting to -5057.4067 at speed 315), so the difference from standing is 0.04 to 0.05 and the ground normal stays (0, 1, 0). Single rows dip 0.1 (-5057.51) at the start of a frame (a mid-frame sample, each followed by the usual value). So the hover is not in the entity position: if the original floats his feet above the surface while skating it is in the pose or the model's own offset, or an effect, none of which the log records. The log has no ground height and no pose, and only flat ground at the start, so how high he glides over the sidewalk is not measurable from it.

## 20. Jump and JumpDash: decoded and measured (live log `tools/live_run.csv` of 2026-10-05, 777 rows, 13.8 s: run, jump, dash, landing, jump from a slope, dash, landing; the earlier recording of section 19 was overwritten by mistake)
Evidence labels as above: D = decompile (`FUN_800877c0` Jump Enter, `FUN_800871d8` Jump Update, `FUN_80088804` JumpDash Enter, `FUN_800882a4` its Update; constants read from `main.dol`: 0x805f2778 = 2.0, 0x805f277c = 0, 0x805f2790 = 0.0001, 0x805f2794 = 0.75, 0x805f27a0 = 0.4, 0x805f27a4 = 2.0, 0x805f27a8 = 180, 0x805f27b0 = 430, 0x805f27b4 = 220, 0x805f27b8 = 6.5, table 0x804b0558 rows of 12 bytes = 100, 270, flag), L = the log.
- **Jump start (D, L).** The jump direction `+0x20` is the vector the command passes (observed: the ground normal `+0x1c8`, (0, 0.8813, -0.4725) on a ramp, (0, 1, 0) on flat ground) or the up axis if its squared length is below 0.0001. The velocity's part along it is removed and `100 x direction` added (a projection with `FUN_8040f990`, then `+=`): L first jump: vy 0.0375 -> exactly 100.0000 on the start row with the planar speed (298.53) unchanged; second jump from the ramp: the first row after the start reads (2.6, 87.1, -40.2) from (2.5, 1.8, 3.4), i.e. about 25 degrees forward. The flags: clear 2 (ground), set 4 and 0, bit 0x35 set. Animation index 5 (`sh_jump_a` + `sh_jump_fa`), or 6 (`sh_fw_jump`) if the direction is more horizontal than vertical about the up axis (D). Substate 0 -> 1 on the first Update; hold timer `+0x2c` = 0.75, button bits `+0x30` = 0x13 (0, 1, 4) (L: bhv word 11 read 0.75 on the start row, 12 ticks later 0.55).
- **Jump Update (D, L).** While bit 4 of `+0x30` is set and the timer is not below 0: timer -= dt and the acceleration gets 270 x direction. L: vy followed `v' = -50 + (v + 50) e^(-dt)` row for row (100, 97.5208, 95.0826, ...) while the button was held (a net of -320 + 270), and at the release (substate 3, t = 5.7522) `-320 + ...` (52.24 -> 46.09 -> 40.04): the boost ended at the release, not at the timer (0.317 s was left). Stick: acceleration 220 x stick (110 with flag 0x1e) when bit 1 of `+0x30` is set, heading turns at 6.5 x dt when the stick is pushed (flag 0x1b). Planar speed in the air (L, stick held): 298.53 -> 287.27 -> 276.49 -> ..., toward 220 / 3 = 73.3 with the drag 3 (matches within 0.4). The ground resolve runs the whole time; when flag 2 is set the Jump sends command 3 (Landing). Substates (L): 1 for the first 13 frames (anim 5), 2 (anim 12, `jump_b`/`jump_fb` then `jump_c`), 3 after the release.
- **JumpDash Enter (D, L).** Sets bit 5, clears 0x32, calls PlayMotion(5, 6) (`sh_fw_jump`), sets `+0x1c` = 0. **Update (D, L):** the first Update (substate 0) sets `+0x20` = 0.4 and `+0x24` = 2.0 (L: bhv words 8 and 9 read 0.4 / 1.9833 on the first row and counted down by 1/60 a frame; the dash lasted 0.4 s = 24 ticks) and adds **180 x the model's forward** (the orientation's forward, horizontal, `FUN_800796dc`) to the velocity; then (the same Update) `+0x24` -= dt and, while it is not below 0, the acceleration gets **430 x forward**; every Update the acceleration gets **220 x stick**; heading turn 6.5 x dt (the function ignores a zero stick, H); the ground resolve; `+0x20` -= dt, at 0 or below command 2 (end: the Fall behavior, L: Fall 0.4 s after), and with flag 2 command 3 (Landing). L: the velocity takes the form (planar before + 180 along the facing) then the integrator with k = 3 and the acceleration 430 + 220 = 650 along the facing: planar 124.9 -> 300.58 -> 296.48 -> 292.58 (asymptote 216.7 = 650 / 3, fitted from both dashes: 216.5 and 217.6), and the second dash 15.7 -> 196.14 -> 197.13 -> ... rising to 208. The vertical is the air's: gravity -320 with k = 1, no hold boost (vy 16.82 -> 11.25 -> 5.78 -> 0.39), no change from the dash itself.
- **What starts it: not read.** Both dashes began in Jump substate 3 (the hold over), 0.10 s and 0.08 s after the release, with a new press of the button (the command comes from the control object, not from the Jump Update). Hypothesis: the jump button pressed again in the air after a jump; whether it also works from a fall is not observed, nor a second dash in one air time (no second dash happened in either).
- **Motion (L).** Animation index 6 = motion number 83 = `sh_fw_jump`, range 0-15: channel time 0, 1, 2 ... 14 then back to 0 (it loops while the dash lasts, 24 frames), time scale 1, fade 5 frames (weight 0.2 per frame as read in the fade block). Fall afterwards (idx 3, `jump_d`, fade 10).
- **Other facts the recording shows (L).** The Landing behavior's sub-state word reads 0x3dcccccd (0.1, a timer) and the behavior was entered while he was still falling down a slope (flag 2 absent for 11 rows, vy -108 .. -135), the planar speed fell by 0.951 a frame there (= e^(-1/20): the air drag 3), Ground resumed at t = 6.4519 with the velocity re-projected on the ground (vy -135 -> -91, planar 142.6 -> 170.7, speed3d 196 -> 194: the speed is about kept, the direction follows the slope). A Fall (substate word a float) began at t = 10.04 with vy 27.7 going up from a ramp at planar 22.8: Fall can start with an upward velocity (a ramp launch, no jump), and Landing came 0.17 s later.
