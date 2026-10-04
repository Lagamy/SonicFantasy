"""
Lists the live tasks of an SADX RAM dump (Dolphin: Memory window > Dump MRAM, a 24 MiB mem1.raw) that were created from a stage's SET file: for each SET record that is in memory as an object's
work struct, the task's exec, display and destroy functions as offsets into _Stg01.rel or _Main.rel, so the object's real display code can be read (tools/genSAEntityKeys.py's header says what
for).

Run from anywhere:  python3 tools/sadxLiveTasks.py [mem1.raw]    (default sadxDecomp/mem1.raw; the dump must be of Emerald Coast act 1, whose SET file is Set0100S.bin)
Reads the dump and the disc (build/Disks/SADX.iso) only; prints to stdout.

How (external, the decompile of _Main.rel / _Stg01.rel; the module addresses are found in the dump itself): MEM1 starts at 0x80000000. The stage's object list is found by the pointer to the name
"RING   "; its entries' function pointers give the module bases (a function's file offset is known from the relocated module). A task is found by its work struct: the SET record's x and z
floats, 8 bytes apart, at work + 0x20 and + 0x28 (position), and a word that points at the work struct at task + 0x20; then the exec / display / destroy pointers at task + 0x10 / + 0x14 / + 0x18
and the angles at work + 0x14, + 0x18, + 0x1C. Only tasks whose functions are in a module are printed (the rest are other data that happen to hold the same floats).
"""
import os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dumpCCL

BASE = 0x80000000
STG_SIZE, MAIN_SIZE = 2778080, 2139000  # decompressed _Stg01.rel and _Main.rel
# file offsets of two object list entries' functions, to tell the module bases from the dump: YASI0 in _Stg01.rel, RING in _Main.rel (tools/genSAEntityKeys.py's object list)
YASI0_OFFSET, RING_OFFSET = 0x15620, 0xEB8A8


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else ROOT + "sadxDecomp/mem1.raw"
    d = open(path, "rb").read()
    u32 = lambda a: struct.unpack_from(">I", d, a - BASE)[0]
    f32 = lambda a: struct.unpack_from(">f", d, a - BASE)[0]

    # the object list entry of RING: its name pointer points at "RING   "
    ring_name = BASE + d.find(b"RING   \0")
    entry = BASE + d.find(struct.pack(">I", ring_name)) - 16
    names = {}
    for k in range(84):
        p = u32(entry + 20 * k + 16)
        o = p - BASE
        names[k] = d[o:d.index(b"\0", o)].decode("latin1").strip()
    main_base = u32(entry + 12) - RING_OFFSET
    stg_base = u32(entry + 20 * 32 + 12) - YASI0_OFFSET
    modules = {"main": (main_base, main_base + MAIN_SIZE), "stg": (stg_base, stg_base + STG_SIZE)}
    print(f"object list at 0x{entry:08X}; _Main.rel at 0x{main_base:08X}, _Stg01.rel at 0x{stg_base:08X}")

    def where(a):
        for name, (lo, hi) in modules.items():
            if lo <= a < hi:
                return f"{name}+0x{a - lo:X}"
        return None

    dumpCCL.ISO = ROOT + "build/Disks/SADX.iso"
    fh, files = dumpCCL.disc_files()
    for p, o, s in files:
        if p == "Set0100S.bin":
            fh.seek(o)
            S = fh.read(s)
    n = struct.unpack_from(">I", S, 0)[0]
    count = 0
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
                ex, di, de = u32(task + 0x10), u32(task + 0x14), u32(task + 0x18)
                if where(ex) and where(di):
                    ang = struct.unpack_from(">3i", d, work + 0x14 - BASE)
                    print(f"record {i:3d} type {raw & 0xFFF:3d} {names.get(raw & 0xFFF, '?'):12s} exec {where(ex):14s} disp {where(di):14s} dest {where(de) or hex(de):14s} "
                          f"angles {[a & 0xFFFF for a in ang]} (record {[v & 0xFFFF for v in (rx, ry, rz)]}) y {f32(work + 0x24):.1f} (record {struct.unpack_from('>f', S, o + 12)[0]:.1f})")
                    count += 1
    print(count, "live tasks")


if __name__ == "__main__":
    main()
