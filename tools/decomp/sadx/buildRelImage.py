"""Builds a relocated image of one SADX .rel for Ghidra: rel/<name>.img (the decompressed module with its relocations applied, laid out
at BASE + file offset) and rel/<name>.layout (sections, one per line: name address file_offset size exec, then "entry <address>" lines for
function candidates). Only relocations into the module itself or the DOL (module 0, absolute addend) are applied; others keep their file value.
Types (Dolphin/YAGCD, external): 1 ADDR32, 4 ADDR16_LO, 5 ADDR16_HI, 6 ADDR16_HA, 10 REL24, 201 NOP, 202 SECTION, 203 END.
Gekko paired-single words (opcodes 4, 56, 57, 60, 61) in executable sections are replaced by nop, because Ghidra's PowerPC has no Gekko
extension and stops a function at them (their semantics are lost). Usage: buildRelImage.py <disc path> <name>"""
import struct, sys
import relTool as r

BASE = 0x80400000

def main():
    path, name = sys.argv[1], sys.argv[2]
    d = bytearray(r.load(path))
    mid, nsec, sectab, impt, imps = r.be(d, 0), r.be(d, 0xC), r.be(d, 0x10), r.be(d, 0x28), r.be(d, 0x2C)
    bssSize = r.be(d, 0x20)
    secs = []
    end = len(d)
    for i in range(nsec):
        o, s = r.be(d, sectab + i * 8), r.be(d, sectab + i * 8 + 4)
        secs.append((o & ~1, s, bool(o & 1)))
    addr = {}
    bssEnd = (BASE + end + 0x1F) & ~0x1F
    for i, (o, s, x) in enumerate(secs):
        if s == 0: continue
        if o == 0:
            addr[i] = bssEnd; bssEnd += (s + 0x1F) & ~0x1F
        else:
            addr[i] = BASE + o
    entries = set()
    def inExec(a):
        return any(x and addr.get(i, 0) <= a < addr.get(i, 0) + s for i, (o, s, x) in enumerate(secs))
    for i in range(imps // 8):
        tm, cur = r.be(d, impt + i * 8), r.be(d, impt + i * 8 + 4)
        sec = 0; off = 0
        while True:
            delta, ty, sc, ad = struct.unpack_from(">HBBI", d, cur); cur += 8
            if ty == 203: break
            off += delta
            if ty == 202: sec = sc; off = 0; continue
            if ty == 201: continue
            if tm == mid:
                if sc not in addr: continue
                S = addr[sc] + ad
            elif tm == 0:
                S = ad
            else:
                continue
            at = secs[sec][0] + off
            P = addr[sec] + off
            if ty == 1: struct.pack_into(">I", d, at, S & 0xFFFFFFFF)
            elif ty == 4: struct.pack_into(">H", d, at, S & 0xFFFF)
            elif ty == 5: struct.pack_into(">H", d, at, (S >> 16) & 0xFFFF)
            elif ty == 6: struct.pack_into(">H", d, at, ((S + 0x8000) >> 16) & 0xFFFF)
            elif ty == 10:
                w = struct.unpack_from(">I", d, at)[0]
                struct.pack_into(">I", d, at, (w & 0xFC000003) | ((S - P) & 0x03FFFFFC))
            else: continue
            if tm == mid and (ty == 10 or (ty == 1 and secs[sec][2] is False)) and inExec(S): entries.add(S)
    nops = 0
    for i, (o, sz, x) in enumerate(secs):
        if not x: continue
        for at in range(o, o + sz, 4):
            w = struct.unpack_from(">I", d, at)[0]
            if (w >> 26) in (4, 56, 57, 60, 61):
                struct.pack_into(">I", d, at, 0x60000000); nops += 1
    print("paired-single words replaced by nop:", nops)
    pro = r.be(d, 0x34)
    if d[0x30] and d[0x30] in addr: entries.add(addr[d[0x30]] + pro)
    open(f"rel/{name}.img", "wb").write(d)
    with open(f"rel/{name}.layout", "w") as f:
        for i, (o, s, x) in enumerate(secs):
            if s: f.write(f"sec{i} 0x{addr[i]:X} 0x{o:X} 0x{s:X} {int(x)}\n")
        for e in sorted(entries): f.write(f"entry 0x{e:X}\n")
    print(name, "module", mid, "sections", {i: hex(a) for i, a in addr.items()}, "entries", len(entries))
main()
