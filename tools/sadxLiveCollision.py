"""
Lists, for each live SET task of an SADX RAM dump (see sadxLiveTasks.py: Emerald Coast act 1, Set0100S.bin), its position and angles and the live collision entries of the task, to compare
with what the engine draws (CollisionView / saObjectRotation).

Run from anywhere:  python3 tools/sadxLiveCollision.py [mem1.raw]    (default sadxDecomp/mem1.raw). Reads the dump and the disc only; prints to stdout.

Layout (external, the pseudocode of the collision init, 0x8041E728 in sadxDecomp/decompiled: task + 0x20 = work, work + 0x38 = collision work (0xA8 bytes: u16 mode, u16 +4 flags,
u16 +6 entry count, f32 +8 bounding radius, u32 +0xC pointer to a copy of the table, entries of 0x30 bytes: u8 kind, form, push, damage, u32 attr, vec3 center @8, f32 a, b, c, d @0x14,
s32 angles @0x24). The work's own fields as in sadxLiveTasks.py: angles at +0x14/+0x18/+0x1C, position at +0x20/+0x24/+0x28, scale at +0x2C/+0x30/+0x34.
Per entry the output gives the live center and angles as the dump holds them, and the center minus the task position, so a table value that the game rewrites (a world space center) shows.
"""
import os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dumpCCL

BASE = 0x80000000
RING_OFFSET, YASI0_OFFSET = 0xEB8A8, 0x15620


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else ROOT + "sadxDecomp/mem1.raw"
    d = open(path, "rb").read()
    u32 = lambda a: struct.unpack_from(">I", d, a - BASE)[0]
    f32 = lambda a: struct.unpack_from(">f", d, a - BASE)[0]
    inram = lambda a: BASE <= a < BASE + len(d) - 4

    ring_name = BASE + d.find(b"RING   \0")
    entry = BASE + d.find(struct.pack(">I", ring_name)) - 16
    names = {}
    for k in range(84):
        o = u32(entry + 20 * k + 16) - BASE
        names[k] = d[o:d.index(b"\0", o)].decode("latin1").strip()

    dumpCCL.ISO = ROOT + "build/Disks/SADX.iso"
    fh, files = dumpCCL.disc_files()
    for p, o, s in files:
        if p == "Set0100S.bin":
            fh.seek(o)
            S = fh.read(s)
    n = struct.unpack_from(">I", S, 0)[0]
    seen = set()
    for i in range(n):
        o = 0x20 + i * 32
        raw, rx, ry, rz = struct.unpack_from(">Hhhh", S, o)
        xb, zb = S[o + 8:o + 12], S[o + 16:o + 20]
        pos = 0
        while True:
            j = d.find(xb, pos)
            if j < 0:
                break
            pos = j + 1
            if j % 4 or d[j + 8:j + 12] != zb:
                continue
            work = BASE + j - 0x20
            ptr = struct.pack(">I", work)
            q = 0
            while True:
                k = d.find(ptr, q)
                if k < 0:
                    break
                q = k + 1
                if k % 4:
                    continue
                task = BASE + k - 0x20
                if not (inram(u32(task + 0x10)) and inram(u32(task + 0x14))) or (i, work) in seen:
                    continue
                seen.add((i, work))
                ang = [a & 0xFFFF for a in struct.unpack_from(">3i", d, work + 0x14 - BASE)]
                wp = [f32(work + 0x20), f32(work + 0x24), f32(work + 0x28)]
                sc = [f32(work + 0x2C), f32(work + 0x30), f32(work + 0x34)]
                print(f"record {i:3d} type {raw & 0xFFF:3d} {names.get(raw & 0xFFF, '?'):12s} pos ({wp[0]:.1f} {wp[1]:.1f} {wp[2]:.1f}) angles {ang} (record {[v & 0xFFFF for v in (rx, ry, rz)]}) scale ({sc[0]:.2f} {sc[1]:.2f} {sc[2]:.2f})")
                cw = u32(work + 0x38)
                if not inram(cw):
                    continue
                count, table = struct.unpack_from(">H", d, cw - BASE + 6)[0], u32(cw + 0xC)
                if not inram(table) or count == 0 or count > 16:
                    continue
                print(f"    collision work 0x{cw:08X} mode {struct.unpack_from('>H', d, cw - BASE)[0]} flags 0x{struct.unpack_from('>H', d, cw - BASE + 4)[0]:04X} radius {f32(cw + 8):.2f} entries {count}")
                for e in range(count):
                    a = table + 0x30 * e - BASE
                    kind, form, push, dmg, attr = struct.unpack_from(">BBBBI", d, a)
                    c = struct.unpack_from(">3f", d, a + 8)
                    abcd = struct.unpack_from(">4f", d, a + 0x14)
                    ea = [v & 0xFFFF for v in struct.unpack_from(">3i", d, a + 0x24)]
                    rel = [c[m] - wp[m] for m in range(3)]
                    print(f"      [{e}] kind {kind} form {form} attr 0x{attr:X} center ({c[0]:.2f} {c[1]:.2f} {c[2]:.2f}) minus pos ({rel[0]:.2f} {rel[1]:.2f} {rel[2]:.2f}) "
                          f"abcd ({abcd[0]:.2f} {abcd[1]:.2f} {abcd[2]:.2f} {abcd[3]:.2f}) angles {ea}")


if __name__ == "__main__":
    main()
