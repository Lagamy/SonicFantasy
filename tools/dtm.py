"""
Reads and writes Dolphin movie (.dtm) files, to record a run from a savestate, replay it, and feed the same inputs to our own player.

    python3 tools/dtm.py dump run.dtm [first [count]]
    python3 tools/dtm.py synth template.dtm out.dtm "60:128,128" "120:128,255,A" ...

dump prints the header fields and, per frame, buttons and sticks. synth copies the header of a template movie that Dolphin recorded (so the fields we do not know
stay as Dolphin wrote them) and replaces its inputs with segments "frames:stickX,stickY[,buttons]" (stick 0..255, 128 neutral; buttons a string of letters from
S A B X Y Z U D L R l r for start, the face buttons, the d-pad up / down, and the L / R digital buttons). The movie must be from the same savestate as the template.

Layout: EXTERNAL, from the Dolphin wiki's DTM page as remembered, NOT checked on a file of this Dolphin build (2609): little-endian; header of 0x100 bytes: 0x00 "DTM\\x1A",
0x04 game id (6 bytes), 0x0A is Wii, 0x0B controller bit mask (bits 0-3 GameCube ports), 0x0C starts from a savestate, 0x0D u64 VI count, 0x15 u64 input count,
0x1D u64 lag count; inputs from 0x100, 8 bytes per present GameCube controller per frame: byte 0 bits 0-7 Start A B X Y Z DpadUp DpadDown, byte 1 bits 0-4 DpadLeft
DpadRight L R GetOrigin, byte 2 / 3 the L / R analog, 4 / 5 main stick x / y, 6 / 7 C-stick x / y. Every check below fails loudly if the file does not fit.
"""
import struct
import sys

HEADER = 0x100
BUTTONS0 = ["S", "A", "B", "X", "Y", "Z", "U", "D"]
BUTTONS1 = ["l", "r", "L", "R", "O"]


def read(path):
    data = open(path, "rb").read()
    if data[:4] != b"DTM\x1a":
        sys.exit(f"{path}: not a DTM (magic {data[:4]!r})")
    ports = [i for i in range(4) if data[0x0B] >> i & 1]
    frames = struct.unpack_from("<Q", data, 0x15)[0]
    if len(data) != HEADER + frames * 8 * len(ports):
        sys.exit(f"{path}: size {len(data)} does not match {frames} frames x {len(ports)} GameCube controllers (expected {HEADER + frames * 8 * len(ports)}); the layout guess is wrong for this file")
    return data, ports, frames


def describe(data, ports, frames):
    print(f"game id {data[4:10]!r} wii {data[0x0A]} controllers {ports} from savestate {data[0x0C]}")
    print(f"VI {struct.unpack_from('<Q', data, 0x0D)[0]} inputs {frames} lag {struct.unpack_from('<Q', data, 0x1D)[0]}")


def frame_at(data, ports, index, port=0):
    offset = HEADER + (index * len(ports) + ports.index(port)) * 8
    return data[offset:offset + 8]


def dump(path, first, count):
    data, ports, frames = read(path)
    describe(data, ports, frames)
    print("frame buttons          Lan Ran  Sx  Sy  Cx  Cy")
    for index in range(first, min(frames, first + count)):
        raw = frame_at(data, ports, index)
        names = "".join(n for i, n in enumerate(BUTTONS0) if raw[0] >> i & 1) + "".join(n for i, n in enumerate(BUTTONS1) if raw[1] >> i & 1)
        print(f"{index:5d} {names:<14s} {raw[2]:4d}{raw[3]:4d}{raw[4]:4d}{raw[5]:4d}{raw[6]:4d}{raw[7]:4d}")


def synth(template, out, segments):
    data, ports, frames = read(template)
    if ports != [0]:
        sys.exit("synth supports a template with only port 1 connected")
    body = bytearray()
    for segment in segments:
        count, rest = segment.split(":")
        fields = rest.split(",")
        letters = fields[2] if len(fields) > 2 else ""
        raw = bytearray([0, 0, 0, 0, int(fields[0]), int(fields[1]), 128, 128])
        for i, n in enumerate(BUTTONS0):
            if n in letters:
                raw[0] |= 1 << i
        for i, n in enumerate(BUTTONS1):
            if n in letters:
                raw[1] |= 1 << i
        if "L" in letters:
            raw[2] = 255
        if "R" in letters:
            raw[3] = 255
        body += bytes(raw) * int(count)
    total = len(body) // 8
    header = bytearray(data[:HEADER])
    struct.pack_into("<Q", header, 0x0D, total)
    struct.pack_into("<Q", header, 0x15, total)
    struct.pack_into("<Q", header, 0x1D, 0)
    open(out, "wb").write(bytes(header) + bytes(body))
    print(f"{out}: {total} frames")


def main():
    args = sys.argv[1:]
    if len(args) >= 2 and args[0] == "dump":
        dump(args[1], int(args[2]) if len(args) > 2 else 0, int(args[3]) if len(args) > 3 else 1 << 30)
    elif len(args) >= 4 and args[0] == "synth":
        synth(args[1], args[2], args[3:])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
