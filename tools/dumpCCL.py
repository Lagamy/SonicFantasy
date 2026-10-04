"""
Extracts every .CCL (Shadow the Hedgehog collision) entry from the .ONE archives of build/Disks/Shadow2005.iso.

Run from anywhere:  python3 tools/dumpCCL.py [outDir]      writes <outDir>/<archive with / as _>__<entry>.ccl (default: stdout summary only)
                    python3 tools/dumpCCL.py -x NAME        hex dump of the entries called NAME (e.g. EGGPAWN.CCL)
                    python3 tools/dumpCCL.py -p NAME        the entries called NAME parsed (records, blocks, tail, name)
                    python3 tools/dumpCCL.py --check        parses every entry strictly and lists those that break the layout
Reads the disc only; the only files written are the .ccl dumps into outDir.

The disc file table, the "One Ver 0.60" layout and PRS are the same as in tools/genResourceMap.py and
src/Importers/STH2005/ONE.cpp / Prs.cpp (a port of them; the "One Ver 0.50" archives are skipped).
"""
import os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/"
ISO = ROOT + "build/Disks/Shadow2005.iso"


def be(b, o, f=">I"):
    return struct.unpack_from(f, b, o)[0]


def disc_files():
    """(open file, [(path, offset, size)]) from the GameCube file table (big-endian FST at 0x424/0x428)."""
    f = open(ISO, "rb")
    h = f.read(0x440)
    fst_off, fst_size = be(h, 0x424), be(h, 0x428)
    f.seek(fst_off)
    fst = f.read(fst_size)
    n = be(fst, 8)
    strs = fst[n * 12:]
    files = []

    def name(o):
        return strs[o:strs.index(b"\0", o)].decode("latin1")

    def walk(s, e, p):
        i = s
        while i < e:
            w0, a, b = struct.unpack_from(">III", fst, i * 12)
            nm = p + name(w0 & 0xFFFFFF)
            if w0 >> 24 == 1:
                walk(i + 1, b, nm + "/")
                i = b
            else:
                files.append((nm, a, b))
                i += 1

    walk(1, n, "")
    return f, files


def prs(src):
    """PRS decompression, same algorithm as STH2005::decompressPrs."""
    out = bytearray()
    p = 0
    control = 0
    bits = 0

    def byte():
        nonlocal p
        v = src[p]
        p += 1
        return v

    def bit():
        nonlocal control, bits
        if bits == 0:
            control = byte()
            bits = 8
        v = control & 1
        control >>= 1
        bits -= 1
        return v

    while True:
        if bit():
            out.append(byte())
            continue
        if bit():
            v = byte()
            v |= byte() << 8
            if v == 0:
                break
            off = (v >> 3) - 8192
            size = v & 7
            size = byte() + 1 if size == 0 else size + 2
        else:
            size = bit() << 1
            size |= bit()
            size += 2
            off = byte() - 256
        for _ in range(size):
            out.append(out[len(out) + off])
    return bytes(out)


def ccl_entries():
    """Yields (archive path, entry name, decompressed bytes) for every .CCL of every One Ver 0.60 archive."""
    f, files = disc_files()
    for path, off, size in files:
        if not path.lower().endswith(".one"):
            continue
        f.seek(off)
        data = f.read(size)
        if data[0x0C:0x18] != b"One Ver 0.60":
            continue
        count = be(data, 0x1C, "<I")
        entries = []
        for i in range(count + 2):
            e = 0x40 + i * 56
            if be(data, e + 52, "<I") != 1:
                continue
            entries.append((data[e:e + 44].split(b"\0")[0].decode("latin1"), be(data, e + 44, "<I"), be(data, e + 48, "<I")))
        for i, (name, usize, eoff) in enumerate(entries):
            if not name.upper().endswith(".CCL"):
                continue
            end = entries[i + 1][2] if i + 1 < len(entries) else len(data) - 0xC
            d = prs(data[0xC + eoff:0xC + end])
            if len(d) != usize:
                raise RuntimeError(f"{path} {name}: {len(d)} bytes, expected {usize}")
            yield path, name, d


def hexdump(d):
    for o in range(0, len(d), 16):
        print(f"{o:04x}  {d[o:o + 16].hex(' ')}")


"""
.CCL layout (big-endian, checked by parse_ccl on all 1289 entries of the disc). The structure follows the game's own loader (external: the
Ghidra decompile of the retail main.dol, shadowDecomp/, FUN_8040e61c and FUN_8040e818); the meaning of the values comes from
the game's Range shape classes (see below) or is a hypothesis where said.
  0x00 u32 file size.  0x04 u32 A: size of section 1.  0x08 u32 B: size of section 2.  0x0C u32 C: entries of section 3 (1 in every file).
  0x10 u32 D: entries of section 4 (0 in every file).  0x14 u32 0x00003142: the game reads byte 0x17 == 'B' as "big-endian" (else it
  byte-swaps every field); byte 0x16 is '1', probably a version.  0x18 and 0x1C u32 0.
  From 0x20: section 1 (A bytes), section 2 (B bytes), section 3 (C x 8 bytes), section 4 (D x 8 bytes), then the names.
  Section 1: N records of 16 bytes {u32 type, u32 pointer, u32 flags, u32 id}, a 16 byte struct of zeros, then the data blocks in record order,
    the last one ending at A. A pointer is an offset from the start of section 1 (the game adds the section's address to it).
  Section 2: relocation stream, one byte per record padded with zeros to 4 bytes. Top 2 bits 01: 6 bit word count, 10: 14 bit (2 bytes),
    11: 30 bit (4 bytes), 00 is skipped. The counts add up to the word position (from the start of section 1) of each pointer to relocate:
    here exactly the pointer of each record ('A' = 1 word to the first, 'D' = 4 words, the record size, to each next one).
  Section 3: entries {u32 offset in section 1, u32 offset in the names}: a named group of records; here {0, 0}.
  Names: the group's name, NUL terminated: the entry name in lower case without ".CCL", or a build path ending in it (3 files).
  A record's type is the game's Range kind (the constructors of its Range classes pass 0 for Sphere, 1 Box, 2 Capsule, 3 Cylinder; Plane
  is a Box subclass with kind 4); its flags and id are passed on to the base class, their meaning is unknown. The block, in floats:
    type 0 Sphere (4): center xyz, radius.
    type 1 Box (10): center xyz, 3 sizes (the game's bounding radius is the length of that vector: half lengths), 3 angles in radians
      (hypothesis: from their values and the Euler helper the game builds the matrix with), a zero.
    type 2 Capsule (9) and type 3 Cylinder (9): center xyz, f3, f4, 3 angles, a zero. The game's bounding radius is f3 + f4 for a capsule and
      sqrt(f3^2 + f4^2) for a cylinder, and scaling an object scales f4 with x and f3 with z. Hypothesis: f4 is the radius and f3 half the
      length along the shape's own z axis.
    type 4 Plane (10): laid out like a Box; the second size is always 0 in the files.
  Not decoded: what flags and id mean, and the order the angles are applied in (the game chooses it with a per-shape mode).
"""
BLOCK_SIZE = {0: 16, 1: 40, 2: 36, 3: 36, 4: 40}
RECORDS = 0x20


def parse_ccl(d):
    """Strict parse: returns a dict, raises ValueError if the file does not follow the layout above exactly."""
    def need(cond, msg):
        if not cond:
            raise ValueError(msg)

    need(len(d) >= 0x40, "too short")
    need(be(d, 0) == len(d), "size word differs from the file size")
    size1, size2, groups, extra = be(d, 4), be(d, 8), be(d, 0xC), be(d, 0x10)
    need((groups, extra, be(d, 0x14), be(d, 0x18), be(d, 0x1C)) == (1, 0, 0x3142, 0, 0), "constant header words differ")
    need(RECORDS + size1 + size2 + 8 < len(d), "sections run past the file")

    stream = d[RECORDS + size1:RECORDS + size1 + size2]
    pointers = []
    word = 0
    i = 0
    while i < len(stream):
        b = stream[i]
        i += 1
        kind = b & 0xC0
        if kind == 0x40:
            count = b & 0x3F
        elif kind == 0x80:
            count = ((b & 0x3F) << 8) | stream[i]
            i += 1
        elif kind == 0xC0:
            count = ((b & 0x3F) << 24) | (stream[i] << 16) | (stream[i + 1] << 8) | stream[i + 2]
            i += 3
        else:
            continue
        word += count
        pointers.append(word * 4)
    count = len(pointers)
    need(count > 0 and pointers == [4 + 16 * k for k in range(count)], "relocated words are not the pointers of consecutive records")

    shapes = []
    block = 16 * (count + 1)
    for k in range(count):
        kind, offset, flags, ident = struct.unpack_from(">IIII", d, RECORDS + 16 * k)
        need(kind in BLOCK_SIZE, f"record {k}: unknown type {kind}")
        need(offset == block, f"record {k}: pointer {offset:#x}, expected {block:#x}")
        need(block + BLOCK_SIZE[kind] <= size1, f"record {k}: block runs past section 1")
        shapes.append({"type": kind, "flags": flags, "id": ident, "floats": struct.unpack_from(f">{BLOCK_SIZE[kind] // 4}f", d, RECORDS + block)})
        block += BLOCK_SIZE[kind]
    need(block == size1, "blocks do not end at the end of section 1")
    need(not any(d[RECORDS + 16 * count:RECORDS + 16 * (count + 1)]), "the struct after the records is not zeros")
    need(len(stream) >= count and not any(stream[count:]) and len(stream) % 4 == 0, "relocation stream padding differs")

    group = RECORDS + size1 + size2
    need(be(d, group) == 0 and be(d, group + 4) == 0, "the group entry is not {0, 0}")
    names = d[group + 8:]
    end = names.find(b"\0")
    need(end > 0 and not any(names[end:]) and len(names) - end <= 4, "name is not one NUL terminated string (up to 3 NUL padding bytes)")
    return {"sectionSizes": (size1, size2), "shapes": shapes, "name": names[:end].decode("latin1")}


def print_ccl(path, name, d):
    c = parse_ccl(d)
    print(f"{path} > {name}: {len(c['shapes'])} records, name = {c['name']}")
    for i, s in enumerate(c["shapes"]):
        print(f"  [{i}] type {s['type']} flags {s['flags']:08x} id {s['id']:#x}  " + " ".join(f"{x:g}" for x in s["floats"]))


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        bad = 0
        n = 0
        for path, name, d in ccl_entries():
            n += 1
            try:
                parse_ccl(d)
            except ValueError as e:
                bad += 1
                print(f"{path} > {name}: {e}")
        print(f"{n} entries, {bad} do not follow the layout")
    elif len(sys.argv) > 2 and sys.argv[1] == "-p":
        for path, name, d in ccl_entries():
            if name.upper() == sys.argv[2].upper():
                print_ccl(path, name, d)
    elif len(sys.argv) > 2 and sys.argv[1] == "-x":
        for path, name, d in ccl_entries():
            if name.upper() == sys.argv[2].upper():
                print(f"{path} > {name} ({len(d)} bytes)")
                hexdump(d)
    else:
        out_dir = sys.argv[1] if len(sys.argv) > 1 else None
        if out_dir:
            os.makedirs(out_dir, exist_ok=True)
        n = 0
        for path, name, d in ccl_entries():
            n += 1
            if out_dir:
                open(os.path.join(out_dir, path.replace("/", "_") + "__" + name + ".ccl"), "wb").write(d)
        print(f"{n} .CCL entries")
