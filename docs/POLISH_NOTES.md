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
