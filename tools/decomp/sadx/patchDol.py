"""Writes main_nops.dol: main.dol with every Gekko paired-single word (opcodes 4, 56, 57, 60, 61) of its text sections replaced by nop
(Ghidra's PowerPC has no Gekko extension; the semantics of those instructions are lost)."""
import struct
d = bytearray(open("main.dol", "rb").read())
n = 0
for i in range(7):
    off, size = struct.unpack_from(">I", d, i * 4)[0], struct.unpack_from(">I", d, 0x90 + i * 4)[0]
    for at in range(off, off + size, 4):
        w = struct.unpack_from(">I", d, at)[0]
        if (w >> 26) in (4, 56, 57, 60, 61):
            struct.pack_into(">I", d, at, 0x60000000); n += 1
open("main_nops.dol", "wb").write(d)
print("replaced", n)
