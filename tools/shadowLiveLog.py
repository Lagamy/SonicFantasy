"""
Live logger of the original Shadow the Hedgehog player, read from a running Dolphin through dolphin-memory-engine (pip install dolphin-memory-engine, in a venv; Linux needs
kernel.yama.ptrace_scope=0 or root). Run with the game running:

    .venv/bin/python tools/shadowLiveLog.py [out.csv] [--seconds N] [--pad HEX]
    .venv/bin/python tools/shadowLiveLog.py --find-pad

Stop with Ctrl+C. One row is written whenever the player's position or velocity changes (a new game frame, polled every 2 ms; every 0.25 s while he stands still).
Layout (external: decompile of main.dol, see docs/SHADOW_PLAYER.md; observed on a RAM dump): the player object has the vtable 0x80521c50 at +0x38 and the name "Shadow" at +0x3c; +0x00 position, +0x0c orientation (euler),
+0x18 velocity, +0x24 acceleration (zeroed at the end of every frame by the executer, so it is mostly 0 when sampled), +0x64 the character index, +0xa0 / +0xa4 32 bit flag sets, +0xa8 the bit set
(bit n = word n >> 5 at +0xa8 + 4 x (n >> 5), bit n & 31: FUN_8006cd78), +0xb8 the stick (also cleared each frame), +0xd0 the position of the frame before, +0xe8 gravity direction, +0xf4 up axis,
+0x1c8 the ground normal, +0x22c the executer; the executer has the player at +0x28 and the current behavior at +0x30, whose vtable says which one (below), +0x1c its sub-state, +0x24 / +0x28 / +0x2c the
Ground behavior's min / max / current speed. The camera's eye and look-at point (cam_eye_*, cam_target_*: read_camera) are logged on every recording. Every value of a column is as read; nothing is converted.
"""
import array
import csv
import math
import struct
import sys
import time

import dolphin_memory_engine as dme

PLAYER_VTABLE = 0x80521C50
MEM1_BASE = 0x80000000
MEM1_SIZE = 0x01800000
CAMERA_MANAGER = 0x8056EE14
VI_RETRACE_COUNT = 0x805F1024

# Behavior vtables (typeinfo walk of main.dol, see SHADOW_PLAYER.md). An object's first word is the vtable address + 0xC (observed on the live game: Ground 0x80520690 reads 0x8052069C).
BEHAVIORS = {
    0x80520690: "Ground", 0x805205E0: "Fall", 0x80520804: "Jump", 0x80520844: "JumpDash", 0x8052073C: "HomingAttack",
    0x80520794: "HomingJump", 0x80520890: "Landing", 0x805207C8: "Idle", 0x80520A80: "SpinDash", 0x80520A20: "Sliding",
    0x80520494: "Damage", 0x805206CC: "GroundAttack", 0x805203B8: "AirAttack", 0x80520BE8: "WallJump", 0x805208C0: "Launch",
    0x805204F8: "Dead", 0x80520B40: "TurnOver", 0x80520640: "Grind", 0x80520414: "ChaosBlast", 0x80520454: "ChaosControl",
    0x805204C8: "DarkSpin", 0x80520528: "DriveVehicle", 0x80520570: "EdgeHang", 0x805205AC: "ExternalControl",
    0x80520920: "LineHang", 0x80520950: "Ottotto", 0x80520998: "PickupObject", 0x805209F0: "PickupWeapon",
    0x80520A50: "SomethingHang", 0x80520AB0: "Stream", 0x80520AF8: "Throw", 0x80520B88: "WallHang", 0x805208F0: "LightDash",
}


def u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def f32(data, offset):
    return struct.unpack_from(">f", data, offset)[0]


def vec(data, offset):
    return struct.unpack_from(">fff", data, offset)


def find_player():
    pattern = struct.pack(">I", PLAYER_VTABLE)
    chunk = 0x100000
    for start in range(0, MEM1_SIZE, chunk):
        data = dme.read_bytes(MEM1_BASE + start, chunk + 4)
        at = data.find(pattern)
        while at >= 0:
            base = MEM1_BASE + start + at - 0x38
            name = dme.read_bytes(base + 0x3C, 8)
            if name.startswith(b"Shadow"):
                return base
            at = data.find(pattern, at + 4)
    return None


def read_camera(player_blob):
    # The camera the player's stick is turned by (external, decompile of main.dol: FUN_800a13c4 builds the world stick for command 1, which FUN_800a78fc copies to player +0xb8;
    # FUN_80093dd0 takes the camera from FUN_80093f74). FUN_80009548 returns the camera manager, a static object at 0x8056ee14; its +0x04 is a vector of 8 byte shared pointers
    # (count at +0x08, data at +0x0c, FUN_8000dfd0), indexed by the player's +0x54 (-1 reads as 0, and 0 when the player's +0x5c is 2). FUN_8000a3bc gives the camera's
    # (+0xa8) - (+0x9c), the direction the stick's forward is turned to. Observed on shadowDecomp/mem1.raw: +0xa8 lies within 4 units of the player (the look-at point) and +0x9c
    # 60 units away (the eye). Returns the 24 raw bytes from +0x9c (eye, then target), or b"" when a pointer is outside MEM1.
    def valid(address):
        return MEM1_BASE <= address < MEM1_BASE + MEM1_SIZE
    index = struct.unpack_from(">i", player_blob, 0x54)[0]
    if index == -1 or u32(player_blob, 0x5C) == 2:
        index = 0
    manager = dme.read_bytes(CAMERA_MANAGER, 0x10)
    count, data = u32(manager, 0x08), u32(manager, 0x0C)
    if not 0 <= index < count or not valid(data):
        return b""
    unit = u32(dme.read_bytes(data + 8 * index, 4), 0)
    return dme.read_bytes(unit + 0x9C, 0x18) if valid(unit) else b""


def bit_list(words):
    bits = []
    for index, word in enumerate(words):
        for bit in range(32):
            if word >> bit & 1:
                bits.append(f"0x{index * 32 + bit:x}")
    return " ".join(bits)


def read_mem1():
    chunk = 0x100000
    return b"".join(dme.read_bytes(MEM1_BASE + start, chunk) for start in range(0, MEM1_SIZE, chunk))


def signed(value):
    return value - 256 if value > 127 else value


def find_pad():
    # Finds the game's copy of the controller stick from three snapshots of MEM1 (released, held fully right, held fully up), then keeps only what keeps following the stick while it is
    # circled. "int" candidates are two adjacent big-endian s16 (x then y) on a 4 byte boundary: external, the decompile of main.dol (FUN_80358938, the game's pad wrapper around PADRead
    # FUN_803801fc, which keeps its PADStatus on the stack, and FUN_80358b64) writes a 16 byte record per pad into a caller buffer: u32 status, u32 mapped buttons, s16 stick x at +8, s16 -(stick y)
    # at +10 (up reads negative), s16 c-stick x at +12, s16 -(c-stick y) at +14, each value moved one toward 0 when negative. So x of the stick is at record + 8 and the pad's record start is printed.
    # The first runs scanned for raw PADStatus bytes (not what the game keeps) and found only constantly changing data. "float": two consecutive big-endian floats about 0/0, +-1/0 and 0/+-1 (a
    # normalized stick, like the player's +0xb8). The live test also demands that every sample stays inside the stick's range. Dolphin keeps working with the window unfocused here, so the waits are
    # only for getting the stick into position.
    snapshots = []
    for prompt in ("release the stick and every button", "hold the main stick fully RIGHT", "hold the main stick fully UP"):
        input(f"{prompt}. Press Enter here, then click the Dolphin window and hold it for the 5 seconds that follow: ")
        time.sleep(5)
        snapshots.append(read_mem1())
    released, right, up = snapshots
    candidates = []
    halves = [array.array("h", snap[: MEM1_SIZE - MEM1_SIZE % 4]) for snap in snapshots]
    for h in halves:
        h.byteswap()
    h0, h1, h2 = halves
    for k in range(0, len(h0) - 1, 2):
        if abs(h0[k]) > 8 or abs(h0[k + 1]) > 8:
            continue
        if h1[k] >= 40 and abs(h1[k + 1]) <= 15 and abs(h2[k]) <= 15 and abs(h2[k + 1]) >= 40:
            candidates.append(("int", k * 2))
    words = [array.array("f", snap[: MEM1_SIZE - MEM1_SIZE % 4]) for snap in snapshots]
    for w in words:
        w.byteswap()
    r0, r1, r2 = words
    for k in range(len(r0) - 1):
        if abs(r0[k]) < 0.05 and abs(r0[k + 1]) < 0.05 and 0.5 < abs(r1[k]) < 1.5 and abs(r1[k + 1]) < 0.2 and abs(r2[k]) < 0.2 and 0.5 < abs(r2[k + 1]) < 1.5:
            candidates.append(("float", k * 4))
    print(f"{len(candidates)} candidates after the three snapshots")
    input("now press Enter here, click the Dolphin window and move the main stick in big slow circles, edge to edge (sampling starts after 3 seconds and lasts 8): ")
    time.sleep(3)
    seen = {c: ([], []) for c in candidates}
    end = time.time() + 8
    while time.time() < end:
        for kind, i in candidates:
            if kind == "int":
                pair = struct.unpack(">hh", dme.read_bytes(MEM1_BASE + i, 4))
                seen[(kind, i)][0].append(pair[0])
                seen[(kind, i)][1].append(pair[1])
            else:
                pair = struct.unpack(">ff", dme.read_bytes(MEM1_BASE + i, 8))
                seen[(kind, i)][0].append(pair[0])
                seen[(kind, i)][1].append(pair[1])
        time.sleep(0.01)
    # Every candidate is listed with what it did while the stick circled, the busiest first, so a too strict test cannot hide the pad: a real one has both values moving over a few dozen steps,
    # inside the stick's range, and x and y never both large at once beyond the circle (x^2 + y^2 stays bounded).
    rows = []
    for (kind, i), (xs, ys) in seen.items():
        radius = max(x * x + y * y for x, y in zip(xs, ys)) ** 0.5
        rows.append((len(set(xs)) + len(set(ys)), kind, i, xs, ys, radius))
    # The second run's top 40 were all noise (an s16 or float buffer that changes every frame, 0x8060xxxx to 0x8064xxxx: -32768..32767 and floats of 1e38), which also had the most distinct values.
    # Only candidates whose largest radius is a stick's (s16 up to 300, floats up to 3) are listed, the closest to a stick's 100 first.
    rows = [r for r in rows if r[5] <= (300 if r[1] == "int" else 3) and r[0] >= 6]
    rows.sort(key=lambda r: abs(r[5] - (100 if r[1] == "int" else 1)))
    print(f"{len(rows)} candidates inside a stick's range (distinct values of x + y, ranges, largest radius)")
    for distinct, kind, i, xs, ys, radius in rows[:40]:
        print(f"{kind} 0x{MEM1_BASE + i:08x}  distinct {distinct:3d}  x {min(xs):.2f}..{max(xs):.2f}  y {min(ys):.2f}..{max(ys):.2f}  radius {radius:.1f}" + ("  record start 0x%08x" % (MEM1_BASE + i - 8) if kind == "int" else ""))


def main():
    out_path = "shadow_live.csv"
    seconds = None
    pad = None
    args = sys.argv[1:]
    while args:
        arg = args.pop(0)
        if arg == "--seconds":
            seconds = float(args.pop(0))
        elif arg == "--pad":
            pad = int(args.pop(0), 16)
        elif arg == "--find-pad":
            dme.hook()
            if not dme.is_hooked():
                sys.exit("Dolphin is not hooked.")
            find_pad()
            return
        else:
            out_path = arg

    dme.hook()
    if not dme.is_hooked():
        sys.exit("Dolphin is not hooked: start it with the game running (and check kernel.yama.ptrace_scope).")
    print("game id:", dme.read_bytes(MEM1_BASE, 6))
    player = find_player()
    if player is None:
        sys.exit("No Shadow player object found (is a stage running with Shadow?).")
    print(f"player at 0x{player:08x}")

    header = ["t", "behavior", "substate", "bhv_words", "pos_x", "pos_y", "pos_z", "rot_x", "rot_y", "rot_z", "vel_x", "vel_y", "vel_z", "planar", "speed3d",
              "acc_x", "acc_y", "acc_z", "stick_x", "stick_y", "stick_z", "prev_x", "prev_y", "prev_z", "up_x", "up_y", "up_z", "gravdir_x", "gravdir_y", "gravdir_z",
              "ground_x", "ground_y", "ground_z", "char_index", "radius", "flags_a0", "flags_a4", "flag_bits", "exec_words",
              "anim_idx", "anim_prev_idx", "anim_motion_a", "anim_motion_b", "anim_cur_a", "anim_cur_b", "anim_blend_param", "anim_rate", "anim_w43c", "anim_weight", "anim_w448", "anim_raw", "mixer_raw", "mixer_head", "channels_raw", "slots_raw", "fade_raw", "hit_raw", "ground_raw"]
    # With --pad ADDR (the controller object's slot 0, 0x8056ED4C: external, the decompile of FUN_8040d9ec, which copies the pad wrapper's records out of a stack buffer into 0x2c byte slots; observed
    # from you: the wrapper's own record buffer is a scratch heap block, 0x8060B400, reused for other data between calls, so it cannot be logged). Slot layout, external (same function): u32 buttons held at +0
    # (the wrapper's mapped bits), u32 ~held, u32 pressed this poll, u32 released, float stick x / y at +0x10 / +0x14 (the s16 pair divided by FUN_8040dd60's range, after FUN_8040de1c), s16 stick x / y at
    # +0x18 / +0x1a (y negated: up reads negative), float c-stick x / y at +0x1c / +0x20, s16 c-stick x / y at +0x24 / +0x26, u32 status at +0x28. Here x / y floats and the s16s are logged; a change of
    # any of them writes a row. "frame" counts the rows written so far, so rows can be aligned by index, not only by the wall clock t.
    # hit_raw is the player's +0x68..+0xa0 and ground_raw +0x170..+0x1f0, kept raw (external, the decompile of FUN_8009728c / FUN_800986f0: the contact's attribute word, *(*(triangle + 0xc) + 0x1c), is only read
    # into locals there and not stored; the flag set at +0xa4 gets bit 0 / 1 / 2 / 4 from attribute bits 0x8000 / 0x4 / 0x20 / 0x400 of each contact, and +0x68 is a list FUN_800986f0 appends a record to
    # (the player, a copy of the contact, the triangle) while FUN_8009904c holds for it, hypothesis: it is the game's hit list. +0x17c .. +0x1e4 is the ground normal smoothing (FUN_8009339c). Layouts
    # not decoded yet: these columns are for finding them in a recording.
    header = ["frame"] + header
    if pad is not None:
        header += ["pad_raw", "pad_buttons", "pad_pressed", "pad_sx", "pad_sy", "pad_cx", "pad_cy", "pad_fx", "pad_fy"]
    # The camera's eye (+0x9c) and look-at point (+0xa8), always logged (read_camera); empty cells when the camera cannot be reached. makeReplay.py turns them into the camera heading.
    header += ["cam_eye_x", "cam_eye_y", "cam_eye_z", "cam_target_x", "cam_target_y", "cam_target_z"]
    # vi_count: the u32 at VI_RETRACE_COUNT, read at the poll that writes the row (not part of the change key). Hypothesis (decompile of FUN_8037d434, not verified live): the retrace count,
    # +1 per video frame, so it should rise by 1 per game tick; rows from the same tick share a value. Last column, so older column positions stay.
    header += ["vi_count"]
    start = time.time()
    last_key = None
    last_write = 0.0
    rows = 0
    with open(out_path, "w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(header)
        try:
            while seconds is None or time.time() - start < seconds:
                blob = dme.read_bytes(player, 0x290)
                executer = u32(blob, 0x22C)
                exec_blob = dme.read_bytes(executer, 0x50) if MEM1_BASE <= executer < MEM1_BASE + MEM1_SIZE else b"\0" * 0x50
                behavior = u32(exec_blob, 0x30)
                bhv_blob = dme.read_bytes(behavior, 0x48) if MEM1_BASE <= behavior < MEM1_BASE + MEM1_SIZE else b"\0" * 0x48
                bhv_vtable = u32(bhv_blob, 0)
                position, velocity = vec(blob, 0x00), vec(blob, 0x18)
                anim = u32(blob, 0x230)
                anim_blob = dme.read_bytes(anim + 0x3E0, 0xC0) if MEM1_BASE <= anim < MEM1_BASE + MEM1_SIZE else b"\0" * 0xC0
                mixer = u32(anim_blob, 0x08)
                mixer_blob = dme.read_bytes(mixer, 0x140) if MEM1_BASE <= mixer < MEM1_BASE + MEM1_SIZE else b"\0" * 0x140
                def valid(address):
                    return MEM1_BASE <= address < MEM1_BASE + MEM1_SIZE
                # The animation library's mixer (+0x3e8 of the animation object; what FUN_80416dc8 / FUN_80416014 / FUN_80416148 take): +0x00 a number that reads 30, +0x10 the array of channels of 0x130 bytes,
                # +0x20 the channel count (2), +0x28 the table of channel numbers per slot (-1 = none). Per channel: +0x108 the time scale (FUN_80416dc8), +0x120 a pointer to a small block whose +0x0c / +0x10 are the
                # blend weights (FUN_80416014) and +0x18 the fade time (FUN_80416148). The playback time is not identified yet: the raw channels are kept to find it.
                mixer_head = mixer_blob[:0x40]
                channels = u32(mixer_head, 0x10)
                slot_table = u32(mixer_head, 0x28)
                channel_count = min(u32(mixer_head, 0x20), 4)
                channels_blob = dme.read_bytes(channels, 0x130 * channel_count) if valid(channels) and channel_count else b""
                slots_blob = dme.read_bytes(slot_table, 16) if valid(slot_table) else b""
                fades = b"".join(dme.read_bytes(u32(channels_blob, 0x130 * i + 0x120), 0x40) if valid(u32(channels_blob, 0x130 * i + 0x120)) else b"\0" * 0x40 for i in range(channel_count))
                pad_blob = dme.read_bytes(pad, 0x2c) if pad is not None else b""
                camera_blob = read_camera(blob)
                key = (position, velocity, bhv_vtable, u32(bhv_blob, 0x1C), anim_blob, mixer_blob, channels_blob, fades, pad_blob, camera_blob, blob[0x68:0xA0], blob[0x170:0x1F0])
                now = time.time()
                if key != last_key or now - last_write > 0.25:
                    last_key, last_write = key, now
                    bits = [u32(blob, 0xA8 + 4 * i) for i in range(4)]
                    row = [f"{now - start:.4f}", BEHAVIORS.get(bhv_vtable - 0xC, f"0x{bhv_vtable:08x}"), u32(bhv_blob, 0x1C), " ".join(f"{u32(bhv_blob, o):08x}" for o in range(0, 0x48, 4))]
                    row += [f"{x:.4f}" for x in position + vec(blob, 0x0C) + velocity]
                    row += [f"{math.hypot(velocity[0], velocity[2]):.4f}", f"{math.sqrt(sum(v * v for v in velocity)):.4f}"]
                    row += [f"{x:.4f}" for x in vec(blob, 0x24) + vec(blob, 0xB8) + vec(blob, 0xD0) + vec(blob, 0xF4) + vec(blob, 0xE8) + vec(blob, 0x1C8)]
                    row += [u32(blob, 0x64), f"{f32(blob, 0x274):.3f}", f"{u32(blob, 0xA0):08x}", f"{u32(blob, 0xA4):08x}", bit_list(bits), " ".join(f"{u32(exec_blob, o):08x}" for o in range(0x28, 0x50, 4))]
                    # The animation object (player +0x230, vtable 0x80522260): +0x42c / +0x428 the animation index now and before (PlayMotion, FUN_800b5210), +0x420 / +0x424 the two motion indices
                    # (into SHADOW.MTP's table) it wants, +0x440 / +0x444 the ones the mixer has, +0x430 the first argument of PlayMotion, +0x438 the rate from the speed, +0x448 the weight between the two, +0x44c the
                    # blend weight; the mixer (+0x3e8) is the animation library's own state, kept raw to find the playback time in. Offsets of anim_blob start at +0x3e0.
                    def ao(offset):
                        return offset - 0x3E0
                    row += [u32(anim_blob, ao(0x42C)), u32(anim_blob, ao(0x428)), u32(anim_blob, ao(0x420)), u32(anim_blob, ao(0x424)), u32(anim_blob, ao(0x440)), u32(anim_blob, ao(0x444)),
                            f"{f32(anim_blob, ao(0x430)):.4f}", f"{f32(anim_blob, ao(0x438)):.4f}", f"{f32(anim_blob, ao(0x43C)):.4f}", f"{f32(anim_blob, ao(0x44C)):.4f}", f"{f32(anim_blob, ao(0x448)):.4f}",
                            " ".join(f"{u32(anim_blob, o):08x}" for o in range(0, 0xC0, 4)), " ".join(f"{u32(mixer_blob, o):08x}" for o in range(0, 0x140, 4)),
                            " ".join(f"{u32(mixer_head, o):08x}" for o in range(0, 0x40, 4)), " ".join(f"{u32(channels_blob, o):08x}" for o in range(0, len(channels_blob), 4)),
                            " ".join(f"{u32(slots_blob, o):08x}" for o in range(0, len(slots_blob), 4)), " ".join(f"{u32(fades, o):08x}" for o in range(0, len(fades), 4)),
                            " ".join(f"{u32(blob, o):08x}" for o in range(0x68, 0xA0, 4)), " ".join(f"{u32(blob, o):08x}" for o in range(0x170, 0x1F0, 4))]
                    row.insert(0, rows)
                    if pad is not None:
                        row += [pad_blob.hex(" "), f"{u32(pad_blob, 0):08x}", f"{u32(pad_blob, 8):08x}"] + list(struct.unpack_from(">hh", pad_blob, 0x18)) + list(struct.unpack_from(">hh", pad_blob, 0x24)) + list(struct.unpack_from(">ff", pad_blob, 0x10))
                    row += [f"{x:.4f}" for x in struct.unpack(">6f", camera_blob)] if camera_blob else [""] * 6
                    row += [u32(dme.read_bytes(VI_RETRACE_COUNT, 4), 0)]
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
