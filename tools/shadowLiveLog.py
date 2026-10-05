"""
Live logger of the original Shadow the Hedgehog player, read from a running Dolphin through dolphin-memory-engine (pip install dolphin-memory-engine, in a venv; Linux needs
kernel.yama.ptrace_scope=0 or root). Run with the game running:

    .venv/bin/python tools/shadowLiveLog.py [out.csv] [--seconds N]

Stop with Ctrl+C. One row is written whenever the player's position or velocity changes (a new game frame, polled every 2 ms; every 0.25 s while he stands still).
Layout (external: decompile of main.dol, see docs/SHADOW_PLAYER.md; observed on a RAM dump): the player object has the vtable 0x80521c50 at +0x38 and the name "Shadow" at +0x3c; +0x00 position, +0x0c orientation (euler),
+0x18 velocity, +0x24 acceleration (zeroed at the end of every frame by the executer, so it is mostly 0 when sampled), +0x64 the character index, +0xa0 / +0xa4 32 bit flag sets, +0xa8 the bit set
(bit n = word n >> 5 at +0xa8 + 4 x (n >> 5), bit n & 31: FUN_8006cd78), +0xb8 the stick (also cleared each frame), +0xd0 the position of the frame before, +0xe8 gravity direction, +0xf4 up axis,
+0x1c8 the ground normal, +0x22c the executer; the executer has the player at +0x28 and the current behavior at +0x30, whose vtable says which one (below), +0x1c its sub-state, +0x24 / +0x28 / +0x2c the
Ground behavior's min / max / current speed. Every value of a column is as read; nothing is converted.
"""
import csv
import math
import struct
import sys
import time

import dolphin_memory_engine as dme

PLAYER_VTABLE = 0x80521C50
MEM1_BASE = 0x80000000
MEM1_SIZE = 0x01800000

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


def bit_list(words):
    bits = []
    for index, word in enumerate(words):
        for bit in range(32):
            if word >> bit & 1:
                bits.append(f"0x{index * 32 + bit:x}")
    return " ".join(bits)


def main():
    out_path = "shadow_live.csv"
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
    player = find_player()
    if player is None:
        sys.exit("No Shadow player object found (is a stage running with Shadow?).")
    print(f"player at 0x{player:08x}")

    header = ["t", "behavior", "substate", "bhv_words", "pos_x", "pos_y", "pos_z", "rot_x", "rot_y", "rot_z", "vel_x", "vel_y", "vel_z", "planar", "speed3d",
              "acc_x", "acc_y", "acc_z", "stick_x", "stick_y", "stick_z", "prev_x", "prev_y", "prev_z", "up_x", "up_y", "up_z", "gravdir_x", "gravdir_y", "gravdir_z",
              "ground_x", "ground_y", "ground_z", "char_index", "radius", "flags_a0", "flags_a4", "flag_bits", "exec_words",
              "anim_idx", "anim_prev_idx", "anim_motion_a", "anim_motion_b", "anim_cur_a", "anim_cur_b", "anim_blend_param", "anim_rate", "anim_w43c", "anim_weight", "anim_w448", "anim_raw", "mixer_raw", "mixer_head", "channels_raw", "slots_raw", "fade_raw"]
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
                key = (position, velocity, bhv_vtable, u32(bhv_blob, 0x1C), anim_blob, mixer_blob, channels_blob, fades)
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
                            " ".join(f"{u32(slots_blob, o):08x}" for o in range(0, len(slots_blob), 4)), " ".join(f"{u32(fades, o):08x}" for o in range(0, len(fades), 4))]
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
