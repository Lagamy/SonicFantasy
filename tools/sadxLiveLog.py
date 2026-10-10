"""
Live logger of the original SADX (GameCube) Sonic, read from a running Dolphin through dolphin-memory-engine (pip install dolphin-memory-engine, in a venv; Linux needs
kernel.yama.ptrace_scope=0 or root). Run with the game running, Sonic in a stage:

    .venv/bin/python tools/sadxLiveLog.py [out.csv] [--seconds N]

Stop with Ctrl+C. One row is written whenever the player's position, velocity, state, stick or logged raw words change (a new game frame, polled every 2 ms; every 0.25 s while he stands still).
The columns that tools/makeReplay.py, compareReplay.py and digestRun.py share with tools/shadowLiveLog.py keep their names (frame, t, behavior, substate, pos_*, rot_*, vel_*, planar, speed3d, vi_count,
pad_buttons, pad_pressed, pad_sx, pad_sy, cam_*); every value is as read, nothing is converted, so vel_* / planar / speed3d / fwd_speed are per game frame (not per second) and rot_* are the game's 16 bit angles (65536 = a turn).
Layout (external: decompile of _Main.rel in sadxDecomp/decompiled/pseudocode.c, FUN_805af2b0 = Sonic's main function, and the same function's callees FUN_804dac14, FUN_804e4e00, FUN_804de8a0, FUN_804e6230;
checked on the RAM dump sadxDecomp/mem1.raw, Emerald Coast act 1, at rest; hypotheses marked):
- the task (found by the three function pointers at +0x10 / +0x14 / +0x18, see find_sonic) has the entity at +0x20 (w1) and the movement block at +0x24 (w2).
- w1: +0x00 u8 action (the state: the switch of FUN_805af2b0; names mostly unknown, logged as S<hex>), +0x01 u8 (logged as substate, hypothesis: the next action), +0x04 u16 status flags (bit 0x100 = ball,
  0x2000, 0x4000 are read in FUN_804de8a0), +0x08 u8 character index (0 = Sonic), +0x14 / +0x18 / +0x1c rotation x / y / z (the game's 16 bit angles; y is his heading), +0x20 / +0x24 / +0x28 position,
  +0x38 pointer to the collision record (logged raw as col_raw).
- w2: +0x00 pointer to w3, +0x04 / +0x08 / +0x0c the velocity per frame (hypothesis: world axes; at rest on the dump it read (0.03, -0.08, 2.87) with w3 +0x38 = 2.868, so z carried the forward speed
  and y was one frame of the 0.08 gravity), +0x10.. the acceleration vector (hypothesis), +0x20 u16 the target heading the stick asks for (FUN_804de8a0 copies the stick's angle there).
- w3 (= *w2): +0x38 forward speed per frame (FUN_804dac14 multiplies the heading direction by it), +0x3c / +0x40 the other local speed components (hypothesis), +0x7e a counter bumped while the ball flag is set,
  +0x9c.. the per-character physics record (33 words copied from _Main 0x2C61E8, Sonic's is id 0), +0x114 the gravity (0.08), +0x10c / +0x11c collision size and center height.
- Two address spaces (external: tools/decomp/sadx/buildRelImage.py; decompile addresses of _Main.rel are image address = 0x80400000 + file offset, its sections placed one after another, the bss
  after the file): code and data sections of the module sit at decompile address - CODE_SHIFT (0x133FC0 on the RAM dump: exec 0x8047B2F0 vs 0x805AF2B0), the module's BSS is allocated elsewhere
  (observed on the dump: 0x1FF860 lower than the decompile; it holds the stick table, pad records, the camera pointer and the per-player blocks). The logger takes CODE_SHIFT from Sonic's
  exec pointer and BSS_SHIFT from the lis/addi pair at +0x2c / +0x38 of FUN_804799ec (the function that fills the stick table, which loads decompile address 0x8094C110 there). DOL globals
  (vi_count) are absolute.
- The game's own stick (decompile FUN_804799ec, FUN_804e6230; BSS 0x8094C330 + 8 x character index): u32 angle, float magnitude. FUN_804799ec turns the pad's s16 stick x / y (from the pad record's
  +0x1c / +0x1e, a signed byte shifted left 8) into atan2(...) minus the camera entity's yaw (+0x18), after a dead zone of 3072 per axis; magnitude = (length / 29440)^3 capped at 1.0 (decompile
  constants 3072.0, 2.55e13 = 29440^3, 1.0). So the angle is the stick's direction in the WORLD, the camera already applied: a replay feeds it without the camera. Logged as stick_angle (0..65535)
  and stick_mag. Observed on the dump: 0x3f8d (= Sonic's heading) and 0.9999 (full stick).
- The pad record (decompile FUN_80479cbc copies the pad block *0x8094C510 there, FUN_8045xxxx the replay path; BSS 0x8094C370 + 0x34 x character index; held mask of the buttons = (&DAT_8094c378)
  [index x 0xd]): +0x08 buttons held, +0x0c their complement, +0x10 pressed this frame, +0x14 released, +0x18 / +0x1a u16 (0..255: the analog triggers, hypothesis), +0x1c / +0x1e s16 main stick x / y
  (y -127 = up on the dump: the stick pushed forward), +0x2c the previous frame's held word. The game's own button masks: jump 0x4, action 0x402 (decompile constants). Logged as pad_buttons,
  pad_pressed, pad_released, pad_l, pad_r, pad_sx, pad_sy and the whole 13 words as pad_raw.
- The camera: entity pointer BSS 0x808B3D10 (non-null on the dump, 0x807E3240): +0x14 / +0x18 rotation x / y (16 bit angles, y = yaw the stick is turned by), +0x20 / +0x24 / +0x28 position = the eye.
  The look-at point is in the camera output block that the pointer at module data 0x80614A1C holds (dump: 0x806B4960; +0x0c eye (= the entity's position), +0x18 look-at, which lay 8.4 above and
  70 behind the eye's player on the dump; hypothesis for the target being the look-at point). Logged raw as cam_rot_x / cam_rot_y (signed) and cam_eye_* / cam_target_* floats.
- Named w3 columns (decompile FUN_804d0974, FUN_804dac14, FUN_804cf4f4; values on the dump): +0x3c up speed, +0x40 side speed (the frame's local axes), +0x00 work0 (3.416 = fwd + 20 x w2+0x38; the
  animation rate input), w2 +0x38 grip_excess, +0x50 / +0x54 / +0x58 ground normal (0, 1, -0 on the dump), +0x1c slope_cos (= minus the dot of gravity direction and that normal; 1.0 in the air),
  +0x44 / +0x48 / +0x4c wall normal (FUN_804dac14 writes it every step: zero without a wall; filled when the collision record's flags +4 have 0x10 and 0x4 and status 0x4 is clear),
  +0x5c / +0x60 flags_5c / flags_60 (this step's and the previous step's surface/state flags, 0x80100001 on the dump), +0x1e4 flags_1e4, +0x04 flags_04, +0x7e counter_7e, the motion struct at +0x120
  (+0x124 u16 motion id now, +0x126 the id of the step before, +0x130 f32 frame; dump: id 0xc run, frame 11.55) and the collision record's u16 flags at +4 (col_flags).
- vi_count: the SDK's VI retrace count, u32 at 0x800C44E4 (external: FUN_80046210, the VI interrupt handler, does DAT_800c44e4 += 1 once per retrace; the dump read 27211). Unknown whether SADX runs one
  game frame per retrace (it may run 30 a second): the first recording shows it (count steps per position change).
"""
import array
import csv
import math
import struct
import sys
import time

import dolphin_memory_engine as dme

MEM1_BASE = 0x80000000
MEM1_SIZE = 0x01800000
VI_RETRACE_COUNT = 0x800C44E4
# Decompile addresses (module image, see the docstring); the BSS ones need BSS_SHIFT, the module data one CODE_SHIFT.
EXEC_ADDRESS = 0x805AF2B0
STICK_TABLE = 0x8094C330
PAD_RECORDS = 0x8094C370
PAD_RECORD_SIZE = 0x34
CAMERA_POINTER = 0x808B3D10
CAMERA_BLOCK_POINTER = 0x80614A1C
# FUN_804799ec loads decompile address 0x8094C110 into r6 with a lis (+0x2c) / addi (+0x38) pair: the module's relocated BSS shows there.
STICK_FUNCTION = 0x804799EC
BSS_ANCHOR = 0x8094C110
# Sonic's display and destroy functions lie 0x5320 and 0x4CB4 below his main function in the module (0x805AF2B0 exec, 0x805A9F90 display, 0x805AA5FC destroy in the decompile's addresses); the three words
# side by side in the task (+0x10, +0x14, +0x18) are a signature that does not need the module's load address (observed on the RAM dump: 0x8047B2F0 / 0x80475FD0 / 0x8047663C).
DISPLAY_BELOW_EXEC = 0x5320
DESTROY_BELOW_EXEC = 0x4CB4
W1_SIZE, W2_SIZE, W3_SIZE, COL_SIZE = 0x60, 0x40, 0x200, 0x100


def u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def s32(data, offset):
    return struct.unpack_from(">i", data, offset)[0]


def f32(data, offset):
    return struct.unpack_from(">f", data, offset)[0]


def valid(address):
    return MEM1_BASE <= address < MEM1_BASE + MEM1_SIZE


def find_sonic():
    # The task of Sonic: a place in MEM1 where an exec pointer into the module range is followed by the display and destroy pointers at the fixed distances below it, and whose entity (+0x20 of the task)
    # has character index 0. Returns the task's address, or None.
    chunk = 0x100000
    for start in range(0, MEM1_SIZE, chunk):
        data = dme.read_bytes(MEM1_BASE + start, chunk + 12)
        words = array.array("I", data)
        if sys.byteorder == "little":
            words.byteswap()
        for i in range(len(words) - 2):
            x = words[i]
            if 0x80400000 <= x < 0x80800000 and words[i + 1] == x - DISPLAY_BELOW_EXEC and words[i + 2] == x - DESTROY_BELOW_EXEC:
                task = MEM1_BASE + start + 4 * i - 0x10
                entity = u32(dme.read_bytes(task + 0x20, 4), 0)
                if valid(entity) and dme.read_bytes(entity + 8, 1)[0] == 0:
                    return task
    return None


def bss_shift(code_shift):
    # Decompile address minus runtime address of the module's BSS, read from the relocated lis r6 / addi r6, r6 pair in FUN_804799ec; None if the words are not that pair.
    function = STICK_FUNCTION - code_shift
    lis, addi = u32(dme.read_bytes(function + 0x2C, 4), 0), u32(dme.read_bytes(function + 0x38, 4), 0)
    if lis >> 16 != 0x3CC0 or addi >> 16 != 0x38C6:
        return None
    low = addi & 0xFFFF
    return BSS_ANCHOR - (((lis & 0xFFFF) << 16) + (low - 0x10000 if low & 0x8000 else low))


def read_camera(bss, code):
    # The camera entity (pointer in BSS): rotation words +0x14 / +0x18, eye +0x20..+0x28; the look-at point is +0x18..+0x20 of the block the module-data pointer names. None when a pointer is not
    # in MEM1 (no camera yet).
    pointer = u32(dme.read_bytes(CAMERA_POINTER - bss, 4), 0)
    block = u32(dme.read_bytes(CAMERA_BLOCK_POINTER - code, 4), 0)
    if not valid(pointer) or not valid(pointer + 0x30) or not valid(block) or not valid(block + 0x24):
        return None
    entity = dme.read_bytes(pointer, 0x30)
    target = struct.unpack_from(">fff", dme.read_bytes(block, 0x24), 0x18)
    return (s32(entity, 0x14), s32(entity, 0x18)) + struct.unpack_from(">fff", entity, 0x20) + target


def words_hex(blob):
    return " ".join(f"{u32(blob, o):08x}" for o in range(0, len(blob) - len(blob) % 4, 4))


def main():
    out_path = "sadx_live.csv"
    seconds = None
    args = sys.argv[1:]
    while args:
        arg = args.pop(0)
        if arg == "--seconds":
            seconds = float(args.pop(0))
        else:
            out_path = arg

    dme.hook()
    if not dme.is_hooked():
        sys.exit("Dolphin is not hooked: start it with the game running (and check kernel.yama.ptrace_scope).")
    print("game id:", dme.read_bytes(MEM1_BASE, 6))
    task = find_sonic()
    if task is None:
        sys.exit("No Sonic task found (is a stage running with Sonic?).")
    code = EXEC_ADDRESS - u32(dme.read_bytes(task + 0x10, 4), 0)
    bss = bss_shift(code)
    if bss is None:
        sys.exit("The module's BSS shift was not found (FUN_804799ec does not start with the expected lis/addi).")
    print(f"Sonic's task at 0x{task:08x}, code shift 0x{code:x}, bss shift 0x{bss:x}")

    header = ["frame", "t", "behavior", "substate", "status", "pos_x", "pos_y", "pos_z", "rot_x", "rot_y", "rot_z", "vel_x", "vel_y", "vel_z", "planar", "speed3d", "fwd_speed",
              "acc_x", "acc_y", "acc_z", "target_angle", "char_index", "stick_angle", "stick_mag",
              "pad_buttons", "pad_pressed", "pad_released", "pad_l", "pad_r", "pad_sx", "pad_sy",
              "cam_rot_x", "cam_rot_y", "cam_eye_x", "cam_eye_y", "cam_eye_z", "cam_target_x", "cam_target_y", "cam_target_z",
              "up_speed", "side_speed", "work0", "grip_excess", "ground_nx", "ground_ny", "ground_nz", "slope_cos", "wall_nx", "wall_ny", "wall_nz",
              "flags_04", "flags_5c", "flags_60", "flags_1e4", "counter_7e", "anim_id", "anim_prev", "anim_frame", "col_flags",
              "vi_count", "w1_raw", "w2_raw", "w3_raw", "col_raw", "stick_raw", "pad_raw"]
    start = time.time()
    last_key = None
    last_write = 0.0
    rows = 0
    with open(out_path, "w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(header)
        try:
            while seconds is None or time.time() - start < seconds:
                head = dme.read_bytes(task, 0x28)
                w1_address, w2_address = u32(head, 0x20), u32(head, 0x24)
                if not valid(w1_address) or not valid(w2_address):
                    time.sleep(0.002)
                    continue
                w1 = dme.read_bytes(w1_address, W1_SIZE)
                w2 = dme.read_bytes(w2_address, W2_SIZE)
                w3_address, col_address = u32(w2, 0), u32(w1, 0x38)
                w3 = dme.read_bytes(w3_address, W3_SIZE) if valid(w3_address) else b"\0" * W3_SIZE
                col = dme.read_bytes(col_address, COL_SIZE) if valid(col_address) else b"\0" * COL_SIZE
                character = w1[8]
                stick = dme.read_bytes(STICK_TABLE - bss + 8 * character, 8)
                pad = dme.read_bytes(PAD_RECORDS - bss + PAD_RECORD_SIZE * character, PAD_RECORD_SIZE)
                camera = read_camera(bss, code)
                key = (w1, w2, w3, col, stick, pad, camera)
                now = time.time()
                if key != last_key or now - last_write > 0.25:
                    last_key, last_write = key, now
                    position = struct.unpack_from(">fff", w1, 0x20)
                    velocity = struct.unpack_from(">fff", w2, 0x04)
                    row = [rows, f"{now - start:.4f}", f"S{w1[0]:02x}", w1[1], f"{struct.unpack_from('>H', w1, 4)[0]:04x}"]
                    row += [f"{x:.4f}" for x in position] + [s32(w1, 0x14), s32(w1, 0x18), s32(w1, 0x1C)]
                    row += [f"{x:.5f}" for x in velocity] + [f"{math.hypot(velocity[0], velocity[2]):.5f}", f"{math.sqrt(sum(v * v for v in velocity)):.5f}", f"{f32(w3, 0x38):.5f}"]
                    row += [f"{x:.5f}" for x in struct.unpack_from(">fff", w2, 0x10)] + [struct.unpack_from(">H", w2, 0x20)[0], character, u32(stick, 0) & 0xFFFF, f"{f32(stick, 4):.4f}"]
                    row += [f"{u32(pad, 8):08x}", f"{u32(pad, 0x10):08x}", f"{u32(pad, 0x14):08x}"] + list(struct.unpack_from(">HH", pad, 0x18)) + list(struct.unpack_from(">hh", pad, 0x1C))
                    row += [camera[0], camera[1]] + [f"{x:.4f}" for x in camera[2:]] if camera else [""] * 8
                    row += [f"{f32(w3, 0x3C):.5f}", f"{f32(w3, 0x40):.5f}", f"{f32(w3, 0x00):.5f}", f"{f32(w2, 0x38):.5f}"]
                    row += [f"{x:.4f}" for x in struct.unpack_from(">fff", w3, 0x50)] + [f"{f32(w3, 0x1C):.4f}"] + [f"{x:.4f}" for x in struct.unpack_from(">fff", w3, 0x44)]
                    row += [f"{u32(w3, 0x04):08x}", f"{u32(w3, 0x5C):08x}", f"{u32(w3, 0x60):08x}", f"{u32(w3, 0x1E4):08x}", struct.unpack_from(">H", w3, 0x7E)[0]]
                    row += list(struct.unpack_from(">HH", w3, 0x124)) + [f"{f32(w3, 0x130):.4f}", f"{struct.unpack_from('>H', col, 4)[0]:04x}"]
                    row += [u32(dme.read_bytes(VI_RETRACE_COUNT, 4), 0), words_hex(w1), words_hex(w2), words_hex(w3), words_hex(col), words_hex(stick), words_hex(pad)]
                    writer.writerow(row)
                    rows += 1
                    if rows % 120 == 0:
                        handle.flush()
                time.sleep(0.002)
        except KeyboardInterrupt:
            pass
    print(f"{rows} rows written to {out_path}")


if __name__ == "__main__":
    main()
