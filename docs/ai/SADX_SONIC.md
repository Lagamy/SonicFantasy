# SADX Sonic original: compact (for Claude)
Twin of `docs/SADX_SONIC_REFERENCE.md` (human wins). Evidence: D decompile (`sadxDecomp/decompiled/pseudocode.c`, addr = 0x80400000 + file offset), M RAM dump `sadxDecomp/mem1.raw` (act 1, at rest), H hypothesis, U unknown. Nothing measured live yet (2026-10-07).

## Task / structs
- Task +0x10/+0x14/+0x18 = FUN_805af2b0 (main) / FUN_805a9f90 (display) / FUN_805aa5fc (destroy) (D); display = main-0x5320, destroy = main-0x4CB4 → signature w/o module base (M: 1 match, task 0x807af2f8).
- task+0x20 = w1 entity, +0x24 = w2 movement, w3 = *w2 (M: 0x807e3420 / 0x807e34e0 / 0x807e3da0).
- w1: +0 u8 action/state (switch in FUN_805af2b0; 'V','W' forced by FUN_8044764c; 4/5 spin dash, 6 homing per player.lua), +1 u8 (H next action), +4 u16 status (0x100 ball, 0x2000, 0x4000), +8 u8 char index (0 Sonic), +0x14/18/1c i32 rot x/y/z 16-bit angles (y heading), +0x20/24/28 pos f32, +0x38 ptr collision record.
- w2: +4/+8/+0xc velocity per frame (D FUN_804dac14; M (0.032,-0.080,2.868); H world axes), +0x10.. accel (H), +0x20 u16 stick's target heading (D FUN_804de8a0).
- w3: +0x38 forward speed/frame (M 2.868 = w2 z), +0x3c/+0x40 other local speed (H), +0x5c/+0x60 flags (0x20000/0x4000 pick accel fn), +0x7e counter while status&0x100, +0x9c.. physics record (33 words from _Main 0x2C61E8; offsets in player.lua are relative to w3-0x9c), +0x10c col size 4.0, +0x114 gravity 0.08, +0x11c center h 5.4, +0x64 ptr (U).

## Input
- Processed stick (D FUN_804e6230): 8 B records at 0x8094C330 + 8×char: u32 BAMS world angle (H camera applied), f32 magnitude. M: zeros.
- Returns no-input if magnitude 0.0 (exc. status 0x100 + stage 0x902/0x903) or per-player block (table 0x809A7AA0) short +10 ≠ 0 and status 0x2000 clear (U meaning).
- Camera: pointer 0x808B3D10, +0x18 yaw, +0x14 pitch (D); M null; H entity-like, pos +0x20.
- Button word: U. 0x28-byte records with 0xFFFFFFFF at +0 from 0x8094C378 (M, layout unread). `sadxLiveLog.py --find-buttons`.

## Time
- VI retrace count u32 0x800C44E4 (D FUN_80046210 `+=1` per retrace; M 27211). U: game steps per retrace (port notes say SADX anim 30). Port converts 1 frame = 1 tick = 1/60 s (H).

## Status
- `tools/sadxLiveLog.py` written 2026-10-07, not run live. Next: user records (`--find-buttons` first), check vi_count steps per position change, velocity axes, w2 pre/post move. Then plan tasks 2 (Lua replay driver `Scripts/sadx/replay.lua`, stick fed as world angle/magnitude) and 3 (`--game sadx` for makeReplay/compareReplay/digestRun).
