# Polish notes: Shadow (2005) player port

Known differences from the original that are NOT being chased now. The bar is "faithful enough": a player finds errors only by playing both games side by side, repeatedly. These come back in the polish pass after both games (Sonic Adventure and Shadow) are playable. Evidence and numbers are in `docs/STH2005_PLAYER_REFERENCE.md` (section 20, entries dated 2026-10-07); this file only lists what is open, how it shows up in play, and where to start. Compact twin: `docs/ai/POLISH.md`.

Rule (user, 2026-10-07): a mismatch that does not visibly affect feel or what the player can do is not worth time; it stays here. Order by visibility: the slow uphill walk (item 2) is arguably the most visible open mismatch, so it comes first when the polish pass starts; item 1 (ramp-edge jump) is a window of about one tick and low.

Evidence labels: O = observed from a recording and a comparison run, H = hypothesis, unknown = not known. Last full rerun: 2026-10-07 17:25, all six replays.

## 1. Slope transitions: the port loses a little path at each one, so the jump on a ramp edge comes early
- **Shows up as:** jumping right at the top of a ramp (live_uphill_jump). The port takes off with the vy 55 too high (267 against 212) and the apex 25 units higher.
- **Evidence (O, 2026-10-07 flips.py over live_uphill_jump.csv and port_run.txt):** the ground normal is NOT detected late in itself. The normal's changes (0.872, 0.854, 0.866, 0.949, 0.995 ... 0.881, 0.949, 1.0) happen at nearly the same places in both runs, 1 to 12 units apart along the path, which is the path drift (9.9 at 3.8 s) and not a probe delay. The jump press is by replay time, so it lands about 2 ticks before the port reaches the edge flip (the original's press is 2 frames after its flip) and the port jumps along the old 0.8813 normal (+98 vy instead of +57). The jump rule itself matches (+64.7 against +63.7).
- **Where the path is lost:** in steps at slope changes, not steadily: drift 1.2 at 1.9 s, 4.5 at 2.4, 6.5 at 3.0, 8.3 at 3.2, 9.9 at 3.8, each step with a dpl dip (-2.6 at 2.4 s, -6.3 at 3.2 s) and the earlier live_slopes note of dpl -27 to -38 at slope changes (1.8, 2.6, 3.3, 5.9 s).
- **Exploit question (O, flips.py positions, 2026-10-07):** the jump height gap is mostly a replay artifact: the replay presses by clock, the port is a few units behind on the path (speed word about 1.4 % low), so the press lands before the port's edge. The normal flips happen at the same places in both runs: first three within 1 unit, ramp tops 2 to 5 units later in the port (under one tick, 6 units a tick at 360). So a player pressing at the same place gets nearly the original's jump; the window where the port gives the bigger boost is at most about one tick (about 17 ms) before an edge. Earlier "a player could clear gaps" was overstated. Real small advantage left: the slow uphill walk (+7 to +8 planar).
- **Checked and closed (O, 2026-10-07, per-tick trans.py over live_uphill_jump 2.3 to 2.6 s and 3.05 to 3.32 s):** no fault found at the transitions themselves. At each ramp crest both runs dip in 3D speed (original 409.3 to 405.4, about 4; port 406.3 to 400.3, about 6; the other crest 393.2 to 388.2 against 390.2), so the port loses about 2 units more per crest, 0.5 %. The larger gap is the speed word on the flat after the crest: the port's stays at 7965 (2.3 to 2.6 s) and 8243.5 (3.05 to 3.3 s), the original's is 8002 and 8359 (a step up at 2.8 to 2.93 s), about 1.4 % less speed (404.4 against 410.3 at 3.25 s). The growth of the word while the stick is turning on the ramp is the lead if this is reopened; not a ground-normal or transition problem. Closed by the user's rule (nothing found at the transitions): no code change.

## 2. Slow uphill walk plateau (fixed 2026-10-07, rerun checked)
- **Shows up as:** walking uphill from a standstill at full stick: the port settled 7 to 8 planar too fast (live_uphill_walk), with drift that also moved the fence recordings (headon +12, glance +14 to +20).
- **Found (O, live_slopes frames 5.579 to 5.596 s):** the original's speed falls by exactly the cosine between the two planes at a plane change (0.9781 = 0.9720 x 1.0063): the uphill step is a plain projection. The port's length-kept turn was wrong at every speed. `player.lua` now projects plainly. The gate and the 150 guess are gone.
- **Rerun (O, 2026-10-07):** uphill_walk plateau +4.8 flat (was about +8; the press inflates part of it), headon ramp +6 and glance +7 to +8 (were +12 to +20), slopes drift unchanged (10.3 at 3.5 s). Left over, small: uphill rows on live_slopes run 4 to 13 slow (speed-word gap). Item 3 (swing loss) is separate and still open.

## 3. Sharp stick swing at low speed
- **Size (rerun 2026-10-07):** large, not small: at 9.40 to 9.50 s the original's planar speed falls 89 to 21, the port stays about 90 (dpl +65..+80, drift 1192 by 13.8 s).
- **Shows up as:** live_fence_slow, 1.3 to 1.5 s: the original's 3D speed falls 145.9 to 136 while the stick swings, the port's stays. Also the missing speed-word drop at 9.43 s (the port's growth angle peaks at 0.506 against the original's 0.751).
- **Cause:** unknown. H: the heading turns more in the port, or the original reads the angle after the tick's turn.
- **Where to start:** a per-tick heading and angle comparison for live_fence_slow 9.33 to 9.5 s.

## 4. Fence kick, three small proposals (not applied, your call: good enough)
- A: remove the press from vy on ground loss. B: two more Ground ticks after a wall-blocked ground loss. C: remove the animation debounce on a wall kick. See REFERENCE section 20.

## 5. Fence recordings still off after the early part
- **live_fence_headon:** port +12 planar from 0.9 s on the ramp (item 2); at 4.1 s the kick region dpl is -224 because the port arrives about 30 units early and hits the fence sooner. The earlier rerun had max dpl 78; why it is 224 now is unknown.
- **live_fence_glance (traced, O, 2026-10-07, compare + port wall flag + CSV planar):** the big numbers are a lead, not a different wall rule. The port runs +14 to +20 planar over the original from 1.1 s (the slow uphill plateau, item 2), drift 6 at 1.3 s, 20 at 3.0, 54 at 4.5, 117 at 5.2 s with the port ahead. The port's fence hit drops its planar speed at 5.6 to 5.7 s; the original's drop is at 5.937 s (360.9 to 144.7), so the port arrives about 0.3 s early. dpl -295 is that offset, not a different kick. Fixing item 2 should shrink it; the kick itself after the hit was not compared tick by tick.

## 6. live_slopes drift
- Drift 10 to 12 from 3.4 s, 22 at the end (was 0.8 at 2.0 s and 3.9 at 6.0 s in the 02:54 run; now 2.7 and 8.0). Probably the press-displacement edits; not isolated. The port also falls at 9.0 s, 0.5 s after the original.

## What is close (O, last runs)
Flat ground (start, acceleration, run, speed word), the stick swing at speed (hold threshold 110), the jump rule, air gravity and drag, fast slopes in-plane speed (within about 1 to 2) and heading (within about 1 degree), the replayed start from a moving state.

## Not compared at all
Camera behaviour, animation, sound.

- Deliberate difference: slope no-idle rule and Ground weight limit (animation.lua mixGround, player.lua SLOPE_*) are the user's own smoothing. Not a bug.

## SADX Sonic: decoded but not ported, not visible (2026-10-10, air / jump / spin dash / roll / homing task)
Visibility rule (CLAUDE.md): not chased now. Evidence and numbers: `docs/SADX_SONIC_REFERENCE.md` ("Air", "Jump", "Spin dash and roll", "Homing attack").
- **Frame tilt levelling in the air** (`FUN_804dd970`: w1+0x14/+0x1c move 0x800 a frame to level, world velocity kept): only the local axes of the first frames after a slope launch differ; the port's air step uses flat axes. Not visible (a 1 to 2 degree change of the stick's push axis for a few frames).
- **Local speeds within +-0.001 zeroed after the move** (`FUN_804d0974`, air): below a pixel a second. Not visible.
- **Homing target's collision centre**: the game aims at the collision record's centre (+8..+0x10, transformed by the entity matrix); the port uses the entity's position. The vertical share of the aim differs by the centre's height (a few units over 5.4 per frame): not visible unless a tall enemy is homed at point blank (then: look at the vertical share first).
- **Speed jitter after 180 homing frames** (0.7 + 0.1 x rand): ported with `math.random`, the sequence is not the game's. Not visible (3 s of homing).
- **Aim assist `FUN_804c7e7c`** (air cases 8, 0xC, 0x1C, 0x1D; list `w3+0x78`, count `w3+0x12` = 0 in the dump): needs the stage's points; not ported. Unknown whether any stage of the port fills it.
- **Object push in the roll and the friction states** (`w3+0x64` + 0x78..0x80, moving platforms): not visible until moving platforms exist in the port.
- **Roll upside down** (gravity y x -8 above RUN_SPEED when the normal's y < 0): needs loops / ceilings, which are not ported (wall running).
- **Light Speed Attack** (case 6): needs the two upgrades (`w3+4`), which the stages here don't give. Not visible. (The old port's homing was its rules.)

## SADX Sonic animation: decoded but not ported, not (clearly) visible (2026-10-10, animation task)
Evidence: `docs/SADX_SONIC_REFERENCE.md` "Animation".
- ~~**Blend between motions**~~ ported 2026-10-11 (SAInstance::beginBlend, sf.animate's fourth argument; not run): 1 / step frames (4 for most, 16 for Stand, 1 for Landing and Turn Around). Remaining: the looping modes' "frame2" start (0 used) is unknown.
- **Idle ids 1, 2, 4 to 8**: standing after a landing is id 2 (jump) or 1 (fall), a wait after 30 s id 4; `Sonic.json` has only id 0 (Stand). Port stands with id 0 everywhere. Unknown whether 0 and 2 look different.
- **Surface flag 4 -> id 0x47** (coasting without a stick on such a surface) and the status 0x800 motions 0x22 / 0x26: need surface flags / that status, which the port does not have.
- **Id 0x92 and the effect objects** (byte `DAT_809a9269`, H Super Sonic): not in the port.
- **Negative speed**: the game plays the running motion backwards at a negative forward speed; the port plays it forwards (the engine now plays a negative rate backwards, used for Wait Back only; not tried for the run).

## SADX Sonic brake state 0xD: not exact, not (clearly) visible (2026-10-10, brake state task)
Evidence: `docs/SADX_SONIC_REFERENCE.md` "Brake state".
- ~~**Brake motion loops**~~ fixed 2026-10-11 (not run): `"loop": false` in `Sonic.json` for `brake`, `turnAround` and `landing` (16 frames, 16 ticks); the instance plays them once and holds the last frame (hypothesis: the game holds the last frame too; the brake state lasts 36 steps against its 33 frames, Turn Around 18 against 16).
- **Landing into the state**: the port decides it from its own ground contact (`ROLL_GAP_TICKS` ticks in the air before it counts); the game from the ground bit of the step. Only differs on the short hops of uneven ground, where the game's landing brake is a single frame of 0.18 deceleration.
- **Heading snap at the stop** (`FUN_804de8a0`'s blend before the direct write): the forward speed is 0 there, so the local side speed is the only thing that could differ; not visible.

## SADX Sonic replay comparison, task 5: not visible (2026-10-10)
Evidence: `docs/SADX_SONIC_REFERENCE.md` "Replay comparison, task 5".
- **dvy +4.8 on every tick**: log convention (the port's line holds the velocity before the engine's gravity step); positions agree. If it is in the way, subtract 4.8 in `compareReplay.py --game sadx`.
- **Landing 1 tick late and -2.3 planar on the landing tick** (4.5 s): the game's floor snap catches him from 3.5 above (post-move gap about 2), the port touches next tick; dpl -6 afterwards, drift 3 to 5. Not noticeable.
- **Port motion after a landing at speed**: `fall` for 4 ticks after the contact, `jump` for 1, then `run` (original: run at once). About 0.08 s of the wrong pose.
- **Slope rows 1.6 to 2.4 s**: port height 0.2 to 0.5 below the original (dh -0.5), vy 0.0 on a ground slope where the original logs -6. Under half a unit.

## SADX Sonic replay comparison, rerun 3: not judged visible (2026-10-11)
Evidence: `docs/SADX_SONIC_REFERENCE.md` "Air speed in the first 0.5 s of a ramp jump".
- **sadx_3 wall landing 11.9 to 12.1 s**: dpl -14 and -23 a second for 0.2 s; looked at 2026-10-11, not changed (see "Ground wall hit" in SADX_SONIC_REFERENCE.md): the original keeps sliding for 5 ticks with the heading turning 45 to 75 degrees a tick, the port stops in 3; one event at the end of the recording; the wall branch of FUN_804dac14 was decoded the same day and does not produce the observed turn (see the reference), so the sadx_3 case stays. The decoded rules themselves (stop at 15, head-on stop at 3.7, slide under 1.39) were ported the same day as `wallHit`, untested against a recording; its call site is commented out in player.lua (off).
- **sadx_1 landing 4.5 s**: dpl -4.2, drift 9.8 units by 8 s (documented under "Landing speed on a changed normal"); one event. Looked at 2026-10-11: the port lands on the same tick but 0.6 units further along, on the next triangle's normal, so no landing rule differs; the 0.6 comes from a dpl blip at 2.9 to 3.2 s.
- **sadx_1 7.5 to 9 s**: air speed within 2 %, final drift 3.8 units.

## SADX Sonic homing attack, no target (2026-10-12)
Evidence: `docs/SADX_SONIC_REFERENCE.md` "Homing attack with no target".
- **Missing ribbon (observed, from the user)**: the port's dash is the same length as the original (replay diff 0.0 planar on every dash tick), but it has no ribbon/trail behind Sonic, which is why it looks shorter side by side. Visual only; decompiled 2026-10-12 (SADX_SONIC_REFERENCE.md "Homing attack effect"): the only effect spawned is a 10-frame `s_ball_a`/`s_ball_b` burst fixed at the dash start (hypothesis: this is the "ribbon"); a first port with Sonic's ball models was wrong and removed (s_ball_a/b are 16x16 blue gradient textures, see the doc's Correction). Second effect (observed, user): aura around the ball while the spin dash charges + a trail; decompiled 2026-10-12 (same doc, "Dash aura and dash trail tasks"): aura = random-oriented 5-frame `s_ball_a/b` shells, trail = 20-frame `s_dash01..23` flipbook, but spawned only in states 7 and 0x2f, conflict with the observation open (Dolphin breakpoint test proposed); burst and aura ported 2026-10-12 as Graphics::DashSpheres (`sf.spawnDashSphere`, not built or run; the geometry is a radius-4.5 UV sphere, there is no UV scroll, the fade is a constant-material alpha, see the doc's "Correction and port"); blend (additive), grey tint and the aura in the charge are hypotheses/observation to check against the original; the trail is not ported.
