"""
Live logger of the original SADX (GameCube) Sonic, read from a running Dolphin through dolphin-memory-engine (pip install dolphin-memory-engine, in a venv; Linux needs
kernel.yama.ptrace_scope=0 or root). Run with the game running, Sonic in a stage:

    .venv/bin/python tools/sadxLiveLog.py [out.csv] [--seconds N] [--buttons HEX]
    .venv/bin/python tools/sadxLiveLog.py --find-buttons

Stop with Ctrl+C. One row is written whenever the player's position, velocity, state, stick or logged raw words change (a new game frame, polled every 2 ms; every 0.25 s while he stands still).
The columns that tools/makeReplay.py, compareReplay.py and digestRun.py share with tools/shadowLiveLog.py keep their names (frame, t, behavior, substate, pos_*, rot_*, vel_*, planar, speed3d, vi_count,
pad_buttons); every value is as read, nothing is converted, so vel_* / planar / speed3d / fwd_speed are per game frame (not per second) and rot_* are the game's 16 bit angles (65536 = a turn).
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
- The game's own stick: 8 byte records at 0x8094C330 + 8 x character index: u32 angle (the stick's direction in the world, the camera already applied; FUN_804e6230 returns it and the magnitude for
  FUN_804de8a0) and float magnitude. Logged as stick_angle (0..65535) and stick_mag. Hypothesis: that is what Sonic's steering reads, so a replay can feed it without the camera.
- The camera: the pointer at 0x808B3D10 (FUN_804de8a0 rotates by its +0x18 y and +0x14 x angles; hypothesis: it is an entity like w1, position at +0x20); it was null on the RAM dump, so the cam_* columns
  are empty until a stage runs it. cam_rot_x / cam_rot_y are the u32 words at +0x14 / +0x18 (raw), cam_x / cam_y / cam_z the floats at +0x20..+0x28.
- vi_count: the SDK's VI retrace count, u32 at 0x800C44E4 (external: FUN_80046210, the VI interrupt handler, does DAT_800c44e4 += 1 once per retrace; the dump read 27211). Unknown whether SADX runs one
  game frame per retrace (it may run 30 a second): the first recording shows it (count steps per position change).
- pad_buttons (only with --buttons ADDR): the u32 at ADDR, raw. The address of the controller's button word is not known: --find-buttons finds it (hold A / release A / tap A).
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
STICK_TABLE = 0x8094C330
CAMERA_POINTER = 0x808B3D10
# Sonic's display and destroy functions lie 0x5320 and 0x4CB4 below his main function in the module (0x805AF2B0 exec, 0x805A9F90 display, 0x805AA5FC destroy in the decompile's addresses); the three words
# side by side in the task (+0x10, +0x14, +0x18) are a signature that does not need the module's load address (observed on the RAM dump: 0x8047B2F0 / 0x80475FD0 / 0x8047663C).
DISPLAY_BELOW_EXEC = 0x5320
DESTROY_BELOW_EXEC = 0x4CB4
W1_SIZE, W2_SIZE, W3_SIZE, COL_SIZE = 0x60, 0x30, 0x130, 0x100


def u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def s32(data, offset):
    return struct.unpack_from(">i", data, offset)[0]


def f32(data, offset):
    return struct.unpack_from(">f", data, offset)[0]


def valid(address):
    return MEM1_BASE <= address < MEM1_BASE + MEM1_SIZE


def read_mem1():
    chunk = 0x100000
    return b"".join(dme.read_bytes(MEM1_BASE + start, chunk) for start in range(0, MEM1_SIZE, chunk))


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


def read_camera():
    # The camera pointer's target: rotation words +0x14 / +0x18 and position floats +0x20..+0x28; empty cells when the pointer is null or outside MEM1.
    pointer = u32(dme.read_bytes(CAMERA_POINTER, 4), 0)
    if not valid(pointer) or not valid(pointer + 0x30):
        return None
    blob = dme.read_bytes(pointer, 0x30)
    return u32(blob, 0x14), u32(blob, 0x18), f32(blob, 0x20), f32(blob, 0x24), f32(blob, 0x28)


def words_hex(blob):
    return " ".join(f"{u32(blob, o):08x}" for o in range(0, len(blob) - len(blob) % 4, 4))


def find_buttons():
    # Finds the controller's button word from MEM1 snapshots: one with every button released, one with A held; a candidate is an aligned u32 that differs by exactly one bit that is set while A is held. Then A
    # is tapped for 8 seconds, MEM1 is sampled over and over, and the candidates are listed by how often that bit toggled (the busiest first; a real button word toggles once per tap). The bit is A's mask for
    # makeReplay, the address is --buttons.
    snapshots = []
    for prompt in ("release every button and the stick", "hold the A (jump) button"):
        input(f"{prompt}. Press Enter here, then click the Dolphin window and hold it for the 5 seconds that follow: ")
        time.sleep(5)
        snapshots.append(read_mem1())
    released, held = (array.array("I", s[: MEM1_SIZE - MEM1_SIZE % 4]) for s in snapshots)
    if sys.byteorder == "little":
        released.byteswap()
        held.byteswap()
    candidates = []
    for k in range(len(released)):
        a, b = released[k], held[k]
        diff = a ^ b
        if diff and diff & (diff - 1) == 0 and b & diff:
            candidates.append((k, diff))
    print(f"{len(candidates)} candidates after the two snapshots")
    input("now press Enter here, click the Dolphin window and tap A about once a second (sampling starts after 3 seconds and lasts 8): ")
    time.sleep(3)
    toggles = {c: 0 for c in candidates}
    last = {}
    end = time.time() + 8
    samples = 0
    while time.time() < end:
        now = array.array("I", read_mem1())
        if sys.byteorder == "little":
            now.byteswap()
        for k, bit in candidates:
            state = bool(now[k] & bit)
            if (k, bit) in last and last[(k, bit)] != state:
                toggles[(k, bit)] += 1
            last[(k, bit)] = state
        samples += 1
    rows = sorted(((n, c) for c, n in toggles.items() if n >= 2), reverse=True)
    print(f"{samples} samples; {len(rows)} candidates whose bit toggled at least twice (toggles, address, bit mask)")
    for n, (k, bit) in rows[:30]:
        print(f"{n:3d}  0x{MEM1_BASE + 4 * k:08x}  mask 0x{bit:08x}")


def main():
    out_path = "sadx_live.csv"
    seconds = None
    buttons = None
    args = sys.argv[1:]
    while args:
        arg = args.pop(0)
        if arg == "--seconds":
            seconds = float(args.pop(0))
        elif arg == "--buttons":
            buttons = int(args.pop(0), 16)
        elif arg == "--find-buttons":
            dme.hook()
            if not dme.is_hooked():
                sys.exit("Dolphin is not hooked.")
            find_buttons()
            return
        else:
            out_path = arg

    dme.hook()
    if not dme.is_hooked():
        sys.exit("Dolphin is not hooked: start it with the game running (and check kernel.yama.ptrace_scope).")
    print("game id:", dme.read_bytes(MEM1_BASE, 6))
    task = find_sonic()
    if task is None:
        sys.exit("No Sonic task found (is a stage running with Sonic?).")
    print(f"Sonic's task at 0x{task:08x}")

    header = ["frame", "t", "behavior", "substate", "status", "pos_x", "pos_y", "pos_z", "rot_x", "rot_y", "rot_z", "vel_x", "vel_y", "vel_z", "planar", "speed3d", "fwd_speed",
              "acc_x", "acc_y", "acc_z", "target_angle", "char_index", "stick_angle", "stick_mag"]
    if buttons is not None:
        header += ["pad_buttons"]
    header += ["cam_rot_x", "cam_rot_y", "cam_x", "cam_y", "cam_z", "vi_count", "w1_raw", "w2_raw", "w3_raw", "col_raw", "stick_raw"]
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
                stick = dme.read_bytes(STICK_TABLE + 8 * character, 8)
                pad = u32(dme.read_bytes(buttons, 4), 0) if buttons is not None else None
                camera = read_camera()
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
                    if buttons is not None:
                        row += [f"{pad:08x}"]
                    row += [camera[0], camera[1], f"{camera[2]:.4f}", f"{camera[3]:.4f}", f"{camera[4]:.4f}"] if camera else [""] * 5
                    row += [u32(dme.read_bytes(VI_RETRACE_COUNT, 4), 0), words_hex(w1), words_hex(w2), words_hex(w3), words_hex(col), words_hex(stick)]
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
