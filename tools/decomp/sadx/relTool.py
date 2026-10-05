"""Decompress a SADX GameCube .rel (SaCompGC, same algorithm as src/Importers/GameCube/SaComp.cpp) from the disc and print its header,
sections and relocation statistics. Usage: relTool.py <disc path> <out file>"""
import struct, sys, collections

ISO = "/home/lagamyrose/Github/SonicFantasy/build/Disks/SADX.iso"

def be(b, o, f=">I"):
    return struct.unpack_from(f, b, o)[0]

def disc_file(path):
    f = open(ISO, "rb")
    f.seek(0x424)
    fo, fs = struct.unpack(">II", f.read(8))
    f.seek(fo); fst = f.read(fs)
    n = be(fst, 8); strs = fst[n * 12:]
    def name(o): return strs[o:strs.index(b"\0", o)].decode("latin1")
    def walk(s, e, p):
        i = s
        while i < e:
            w0, a, b = struct.unpack_from(">III", fst, i * 12)
            nm = p + name(w0 & 0xFFFFFF)
            if w0 >> 24 == 1:
                r = walk(i + 1, b, nm + "/")
                if r: return r
                i = b
            else:
                if nm.lower() == path.lower(): return (a, b)
                i += 1
    a, b = walk(1, n, "")
    f.seek(a); return f.read(b)

def decompress(raw):
    tag = next(o for o in range(0, 0x100, 4) if raw[o:o + 8] == b"SaCompGC")
    size = be(raw, tag + 8) & 0x0FFFFFFF
    dbits, lbits = raw[tag + 12], raw[tag + 13]
    pos = tag + 16
    cur = 0; avail = 0
    out = bytearray()
    def read(n):
        nonlocal cur, avail, pos
        if n == 0: return 0
        if avail >= n:
            v = cur & ((1 << n) - 1); cur >>= n; avail -= n; return v
        missing = n - avail
        v = cur << missing
        w = be(raw, pos); pos += 4
        v |= w & ((1 << missing) - 1)
        avail = 32 - missing; cur = w >> missing
        return v
    while len(out) < size:
        if read(1):
            out.append(read(8)); continue
        d = read(dbits) + 1; l = read(lbits) + 2
        for _ in range(l): out.append(out[-d])
    return bytes(out)

def load(path):
    raw = disc_file(path)
    return decompress(raw) if any(raw[o:o + 8] == b"SaCompGC" for o in range(0, 0x100, 4)) else raw

if __name__ == "__main__":
    d = load(sys.argv[1])
    if len(sys.argv) > 2: open(sys.argv[2], "wb").write(d)
    mid, nsec, sectab, impt, imps = be(d, 0), be(d, 0xC), be(d, 0x10), be(d, 0x28), be(d, 0x2C)
    print("size", hex(len(d)), "module id", mid, "ver", be(d, 0x1C), "sections", nsec, "bss size", hex(be(d, 0x20)))
    print("prolog sec/off", d[0x30], hex(be(d, 0x34)), "epilog", d[0x31], hex(be(d, 0x38)))
    for i in range(nsec):
        o, s = be(d, sectab + i * 8), be(d, sectab + i * 8 + 4)
        print(i, "exec" if o & 1 else "data", hex(o & ~1), hex(s))
    stat = collections.Counter()
    for i in range(imps // 8):
        tm, cur = be(d, impt + i * 8), be(d, impt + i * 8 + 4)
        while True:
            dl, ty, sc, ad = struct.unpack_from(">HBBI", d, cur); cur += 8
            if ty == 203: break
            stat[(tm, ty)] += 1
    for k, v in sorted(stat.items()): print("import module", k[0], "type", k[1], v)
