"""
Live logger of the original Shadow's ball effects (Jump_ballA / Jump_ballB of common/CommonEffect.one), read from a running Dolphin through dolphin-memory-engine (same setup as shadowLiveLog.py:
pip install dolphin-memory-engine in a venv; Linux needs kernel.yama.ptrace_scope=0 or root). Run with the stage running, before the jump:

    .venv/bin/python tools/shadowEffectLog.py [out.csv] [--seconds N] [--no-mem2] [--vtable HEX] [--range START-END]
    python3 tools/shadowEffectLog.py --digest out.csv

Stop with Ctrl+C. It writes tools/shadow_effect.csv by default. A row is written whenever the player's behavior or any effect object's bytes change (polled every 2 ms; every 0.25 s otherwise); one row per live
ball effect object, or a single row with an empty "obj" when none is alive. A second shadowEffectLog.py (or shadowLiveLog.py) can run at the same time: it only reads.

How the objects are found (external: decompile of main.dol, nothing here was run yet; every part below is a hypothesis until the first recording):
- FUN_801d0afc makes the effect object of an EFD record by its kind; kind 4 (model effect: the two balls) is built by FUN_801d1238, which sets the vtable word to 0x8053c934 (the first word of the object; the
  behaviors of shadowLiveLog.py show the stored word can be the symbol + 0xC, so both 0x8053c934 and 0x8053c940 are searched; --vtable HEX replaces the list) and is 0x16c bytes big. The vtable is the class of ALL
  model effects (the balls and every other model effect), so each object is told apart by a word of it that equals the address of its EFD record or that record's resource pointer (+0x3c): the records are found at
  start by searching the game's memory for the names "Jump_ballA" / "Jump_ballB" (a record has the name in its first 32 bytes, u16 kind 4 at +0x20, id 0x23 / 0x24 at +0x24, disc file). Other model effects show
  with the label "?" (leave them out when reading); a label may be "?" for the first rows of an object and settle later.
- A background thread scans MEM1 (and MEM2 unless --no-mem2) for the vtable word about every second, so the polling loop never waits for a scan. An object is "new" in the first row after the scan finds it,
  "alive" while its first word is still the vtable word, and "gone" in the row after that stops being true (the destructor changes the vtable; a freed block that is never touched stays "alive": hypothesis,
  tell it by its bytes not changing).
Columns: frame (row number), t (s), vi_count (u32 at 0x805f1024, +1 per video frame: align with other logs by it), behavior (name by the vtable, as shadowLiveLog.py) and substate (+0x1c), the player's pos / rot / vel,
anim28 / anim40 (the u32 at +0x28 / +0x40 of the animation object player+0x230: where the decompile keeps the jump's ball A and the dash trail's ball B handles), joint0 (0x38 bytes at animation object + 0xd0: the
node FUN_800aa760 returns for joint index 0), bhv_raw (0x70 bytes of the current behavior: Fall keeps its handle at +0x20 and its matrix at +0x28, FUN_80080784), event (new / alive / gone / none), obj (address), label,
col_r/g/b/a (the four floats at +0x134..+0x140: FUN_80027b9c turns the record's bytes +0x39, +0x3a, +0x3b, +0x38 into them), f15c (float at +0x15c), tail (the bytes from +0x160: FUN_801d1238 builds a matrix there, layout not known) and words (the whole object, 91 big-endian words, space separated). Nothing is converted; "words" is the answer, the others are shortcuts to look for it.
Digest: --digest writes out.ai.txt (compact) and out.txt (readable), printing only their paths; per object it gives its life, the behavior at its start and end, the colour / alpha track and the words that changed (offset, distinct values, first, last, as a float, monotonic or not).
"""
import contextlib
import csv
import io
import math
import struct
import sys
import threading
import time

MEM1_BASE = 0x80000000
MEM1_SIZE = 0x01800000
MEM2_BASE = 0x90000000
MEM2_SIZE = 0x04000000
VI_RETRACE_COUNT = 0x805F1024
OBJECT_SIZE = 0x16C
VTABLE_WORDS = (0x8053C934, 0x8053C940)
RECORD_IDS = {0x23: "A", 0x24: "B"}
SCAN_CHUNK = 0x100000
BHV_BYTES = 0x70
POLL_SECONDS = 0.002
IDLE_WRITE_SECONDS = 0.25


def u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def f32(data, offset):
    return struct.unpack_from(">f", data, offset)[0]


def in_memory(address):
    return MEM1_BASE <= address < MEM1_BASE + MEM1_SIZE or MEM2_BASE <= address < MEM2_BASE + MEM2_SIZE


class Scanner(threading.Thread):
    """Searches the game's memory for 4 byte aligned words equal to one of the patterns, again and again; `found` is the last complete result."""

    def __init__(self, dme, regions, words):
        super().__init__(daemon=True)
        self.dme = dme
        self.regions = list(regions)
        self.patterns = [struct.pack(">I", w) for w in words]
        self.found = set()
        self.stop_flag = False
        self.pause = 0.5

    def run(self):
        while not self.stop_flag:
            found = set()
            for base, size in list(self.regions):
                for start in range(0, size, SCAN_CHUNK):
                    try:
                        data = self.dme.read_bytes(base + start, min(SCAN_CHUNK + 4, size - start))
                    except RuntimeError:
                        self.regions.remove((base, size))
                        break
                    for pattern in self.patterns:
                        at = data.find(pattern)
                        while at >= 0:
                            if at % 4 == 0:
                                found.add(base + start + at)
                            at = data.find(pattern, at + 1)
            self.found = found
            time.sleep(self.pause)


def find_records(dme, regions):
    """The EFD records of the two balls, by name: {record address: "A" / "B"}. The record is checked (u16 kind 4 at +0x20, id at +0x24) so a stray copy of the name is not taken."""
    records = {}
    for base, size in regions:
        for start in range(0, size, SCAN_CHUNK):
            try:
                data = dme.read_bytes(base + start, min(SCAN_CHUNK + 0x40, size - start))
            except RuntimeError:
                break
            for name in (b"Jump_ballA\0", b"Jump_ballB\0"):
                at = data.find(name)
                while at >= 0:
                    if at + 0x28 <= len(data) and struct.unpack_from(">H", data, at + 0x20)[0] == 4 and u32(data, at + 0x24) in RECORD_IDS:
                        records[base + start + at] = RECORD_IDS[u32(data, at + 0x24)]
                    at = data.find(name, at + 1)
    return records


def record(argv):
    import dolphin_memory_engine as dme
    from shadowLiveLog import BEHAVIORS, find_player

    out_path = "tools/shadow_effect.csv"
    seconds = None
    use_mem2 = True
    scan_range = None
    words = VTABLE_WORDS
    args = list(argv)
    while args:
        arg = args.pop(0)
        if arg == "--seconds":
            seconds = float(args.pop(0))
        elif arg == "--no-mem2":
            use_mem2 = False
        elif arg == "--range":
            low, high = args.pop(0).split("-")
            scan_range = (int(low, 16), int(high, 16) - int(low, 16))
        elif arg == "--vtable":
            words = (int(args.pop(0), 16),)
        else:
            out_path = arg

    dme.hook()
    if not dme.is_hooked():
        sys.exit("Dolphin is not hooked: start it with the game running (and check kernel.yama.ptrace_scope).")
    player = find_player()
    if player is None:
        sys.exit("No Shadow player object found (is a stage running with Shadow?).")
    regions = [(MEM1_BASE, MEM1_SIZE)] + ([(MEM2_BASE, MEM2_SIZE)] if use_mem2 else [])
    records = find_records(dme, regions)
    # --range limits the background scan to one stretch of memory (the heap block the objects of a first recording sat in, e.g. 80a00000-80d00000), which makes a scan take milliseconds: an object is seen on the tick it appears.
    if scan_range is not None:
        regions = [scan_range]
    print(f"player at 0x{player:08x}; EFD records: " + (", ".join(f"{label}@0x{address:08x}" for address, label in records.items()) or "none found (labels will be ?)"))
    # A record's own address and its resource pointer (+0x3c) are the words an object is matched by.
    references = {}
    for address, label in records.items():
        references[address] = label
        references[u32(dme.read_bytes(address + 0x3C, 4), 0)] = label
    scanner = Scanner(dme, regions, words)
    if scan_range is not None:
        scanner.pause = 0.0
    scanner.start()

    header = ["frame", "t", "vi_count", "behavior", "substate", "pos_x", "pos_y", "pos_z", "rot_x", "rot_y", "rot_z", "vel_x", "vel_y", "vel_z", "anim28", "anim40", "joint0", "bhv_raw",
              "event", "obj", "label", "col_r", "col_g", "col_b", "col_a", "f15c", "tail", "words"]
    start = time.time()
    known = {}
    labels = {}
    last_key = None
    last_write = 0.0
    frame = 0
    with open(out_path, "w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(header)
        try:
            while seconds is None or time.time() - start < seconds:
                blob = dme.read_bytes(player, 0x290)
                executer = u32(blob, 0x22C)
                exec_blob = dme.read_bytes(executer, 0x50) if in_memory(executer) else b"\0" * 0x50
                behavior = u32(exec_blob, 0x30)
                bhv_blob = dme.read_bytes(behavior, BHV_BYTES) if in_memory(behavior) else b"\0" * BHV_BYTES
                anim = u32(blob, 0x230)
                anim_blob = dme.read_bytes(anim + 0x28, 0x18) if in_memory(anim) else b"\0" * 0x18
                joint0 = dme.read_bytes(anim + 0xD0, 0x38) if in_memory(anim) else b"\0" * 0x38

                events = {}
                for address in scanner.found:
                    if address not in known:
                        known[address] = None
                        events[address] = "new"
                objects = {}
                for address in list(known):
                    data = dme.read_bytes(address, OBJECT_SIZE)
                    if u32(data, 0) not in words:
                        del known[address]
                        events[address] = "gone"
                        continue
                    objects[address] = data
                    if address not in labels or labels[address] == "?":
                        match = [references[u32(data, o)] for o in range(0, OBJECT_SIZE, 4) if u32(data, o) in references]
                        labels[address] = match[0] if match else "?"

                key = (behavior, u32(bhv_blob, 0x1C), tuple(sorted(objects.items())), tuple(sorted(events.items())), blob[:0x30], joint0)
                now = time.time()
                if key != last_key or now - last_write > IDLE_WRITE_SECONDS:
                    last_key, last_write = key, now
                    head = [frame, f"{now - start:.4f}", u32(dme.read_bytes(VI_RETRACE_COUNT, 4), 0), BEHAVIORS.get(u32(bhv_blob, 0) - 0xC, f"0x{u32(bhv_blob, 0):08x}"), u32(bhv_blob, 0x1C)]
                    head += [f"{f32(blob, o):.4f}" for o in (0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20)]
                    head += [f"{u32(anim_blob, 0):08x}", f"{u32(anim_blob, 0x18 - 4):08x}", joint0.hex(" "), bhv_blob.hex(" ")]
                    rows = []
                    for address, event in sorted(events.items()):
                        if event == "gone":
                            rows.append([event, f"0x{address:08x}", labels.get(address, "?")] + [""] * 7)
                    for address, data in sorted(objects.items()):
                        colour = [f"{f32(data, o):.4f}" for o in (0x134, 0x138, 0x13C, 0x140)]
                        rows.append([events.get(address, "alive"), f"0x{address:08x}", labels[address]] + colour +
                                    [f"{f32(data, 0x15C):.4f}", data[0x160:].hex(" "), " ".join(f"{u32(data, o):08x}" for o in range(0, OBJECT_SIZE, 4))])
                    for row in rows or [["none", "", "", "", "", "", "", "", "", ""]]:
                        writer.writerow(head + row)
                    frame += 1
                    if frame % 120 == 0:
                        handle.flush()
                time.sleep(POLL_SECONDS)
        except KeyboardInterrupt:
            pass
    scanner.stop_flag = True
    print(f"{frame} ticks written to {out_path}")


def digest(path, ai):
    """Per object life: when, behavior at the start and end, the colour / alpha track and the words that changed. Reads only the CSV."""
    with open(path, newline="") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        sys.exit("empty log")
    vi0 = int(rows[0]["vi_count"])
    objects = {}
    for row in rows:
        if row["obj"]:
            objects.setdefault(row["obj"], []).append(row)
    behaviors = []
    for row in rows:
        if not behaviors or behaviors[-1][1] != row["behavior"]:
            behaviors.append((int(row["vi_count"]) - vi0, row["behavior"], row["t"]))
    if ai:
        print("behaviors vi:name " + " ".join(f"{vi}:{name}" for vi, name, _ in behaviors))
    else:
        print("Behavior changes (vi_count since the first row: name, t seconds):\n  " + ", ".join(f"{vi}:{name} ({t}s)" for vi, name, t in behaviors))
    if not objects:
        print("no effect object was found: check the start line (EFD records, vtable words) or run with --vtable")
    for address, orows in objects.items():
        live = [r for r in orows if r["event"] != "gone"]
        label = next((r["label"] for r in reversed(orows) if r["label"] != "?"), "?")
        first, last = orows[0], orows[-1]
        print(("obj %s %s " % (address, label) if ai else f"\nObject {address} (label {label}: A = Jump_ballA, B = Jump_ballB, ? = another model effect):\n  ") +
              f"vi {int(first['vi_count']) - vi0}..{int(last['vi_count']) - vi0} events {first['event']}..{last['event']} behaviors {first['behavior']}..{last['behavior']} rows {len(orows)}")
        track = []
        for r in live:
            colour = (r["col_r"], r["col_g"], r["col_b"], r["col_a"])
            if not track or track[-1][1] != colour:
                track.append((int(r["vi_count"]) - vi0, colour))
        print(("  colour " if ai else "  colour (r,g,b,a floats as they changed, vi): ") + " ".join(f"{vi}:{','.join(c)}" for vi, c in track[:40]) + (" ..." if len(track) > 40 else ""))
        columns = list(zip(*[[int(w, 16) for w in r["words"].split()] for r in live])) if live else []
        changed = [(i, col) for i, col in enumerate(columns) if len(set(col)) > 1]
        print(f"  words changed {len(changed)}" if ai else f"  Words that changed: {len(changed)} (offset: distinct values, first -> last, direction):")
        for i, col in changed[:60]:
            floats = [struct.unpack(">f", struct.pack(">I", v))[0] for v in col]
            use_float = all(math.isfinite(f) and (f == 0 or 1e-6 < abs(f) < 1e6) for f in floats)
            deltas = [b - a for a, b in zip(floats if use_float else col, (floats if use_float else col)[1:]) if b != a]
            direction = "up" if deltas and all(d > 0 for d in deltas) else "down" if deltas and all(d < 0 for d in deltas) else "mixed"
            shown = f"{floats[0]:.4f} -> {floats[-1]:.4f} (float)" if use_float else f"{col[0]:08x} -> {col[-1]:08x}"
            print(f"    +0x{i * 4:03x}: {len(set(col))} values, {shown}, {direction}")


def main():
    argv = sys.argv[1:]
    if "--digest" in argv:
        argv.remove("--digest")
        argv = [a for a in argv if a != "--ai"]
        if len(argv) != 1:
            sys.exit("usage: python3 tools/shadowEffectLog.py --digest out.csv")
        # Both forms are written to files next to the CSV (out.ai.txt compact for Claude, out.txt for reading); only their paths are printed.
        for ai, suffix in ((True, ".ai.txt"), (False, ".txt")):
            buffer = io.StringIO()
            with contextlib.redirect_stdout(buffer):
                digest(argv[0], ai)
            target = argv[0].removesuffix(".csv") + suffix
            with open(target, "w") as handle:
                handle.write(buffer.getvalue())
            print(f"{target}: {len(buffer.getvalue().splitlines())} lines")
    else:
        record(argv)


if __name__ == "__main__":
    main()
