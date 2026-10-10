"""
Compact view of one run, for reading (by Claude or by hand) instead of the raw files: a live recording of the original (tools/shadowLiveLog.py CSV, about 4 KB a row, 90 per cent raw memory dumps) or one replay of the port (the REPLAY lines in the port's stdout).

    python3 tools/digestRun.py tools/live_slopes.csv [--from 5.6] [--to 6.0] [--every 1]
    python3 tools/digestRun.py tools/port_run.txt live_slopes [--from 5.6] [--to 6.0] [--every 1]
    python3 tools/digestRun.py --game sadx tools/sadx_1.csv [--from T] [--to T] [--every N]
    python3 tools/digestRun.py --game sadx tools/port_run.txt sadx_1 [--from T] [--to T] [--every N]

Both are printed in the port's axes, so the two digests of one recording line up column by column: the original's (x, z) is turned to (z, -x) (hypothesis, see Scripts/shadow/replay.lua) and its facing is -rot_y - pi. Positions are relative to the run's first row.
The original's rows are on the frame clock of compareReplay.py (frame_times) and a row equal to the one before it (the logger reads each frame several times) is left out. Angles in degrees. --every N keeps every Nth printed row.
--contacts (original only) adds a line under each row: the flag words +0xa0 / +0xa4, the set bits of +0xa8, and the words of hit_raw (+0x68..+0xa0) and ground_raw (+0x170..+0x1f0) that
changed since the printed row before (the first row: every non-zero one), as +offset=hex, offsets from the player object. Use a short --from / --to window.
--words 1c8,1cc,... (with --contacts) keeps only those offsets (hex) in the changed-word list.
--game sadx: the SADX Sonic (sadxLiveLog.py CSV or Scripts/sadx/replay.lua lines), in the original's own axes, one row per game tick, velocities per SECOND (the logged per-frame value x 60), angles in degrees, work0 per frame. Both forms have the same columns:
t beh x y z vx vy vz pl s3 face gx gy gz w0 sa sm a b (face = rot_y / the port's heading; sa sm = the world stick's angle and magnitude; a b = jump, action held), the port's adds gr wall wx wy wz. Rows are paired as compareReplay.py's sadx_port does: row t is the original's
tick t and the port's result of step t - 1. --contacts and --absolute are not used.
--absolute (original only) prints positions as they are (still in port axes) instead of from the first row: original level space is (x, y, z) = (-z, y, x) of the printed one.
"""
import csv
import math
import struct
import sys

from compareReplay import frame_times, sadx_original, sadx_port


def wrap_degrees(a):
    return (a + 180) % 360 - 180


def word(words, i):
    """Word i of a logged word list (big-endian hex) as a float, or None."""
    parts = words.split()
    if i >= len(parts):
        return None
    return struct.unpack(">f", bytes.fromhex(parts[i]))[0]


def number(v):
    return "-" if v is None else f"{v:.4g}"


def original(path, start, end, every, contacts, absolute, words_wanted):
    rows = list(csv.DictReader(open(path)))
    previous = {}
    times = frame_times(rows)
    x0, y0, z0 = (0.0, 0.0, 0.0) if absolute else (float(rows[0][k]) for k in ("pos_x", "pos_y", "pos_z"))
    pad = "pad_fx" in rows[0]
    camera = "cam_eye_x" in rows[0]
    print(f"# original {path} | port axes, pos from {'origin' if absolute else 'start'} | w11 w12 w13 = bhv words (Ground: W speed word, turn rate, angle rad) | g = ground normal" + (" | cam = camera heading (target - eye), port axes" if camera else ""))
    print("t beh/sub x y z vx vy vz pl s3 gx gy gz face w11 w12 w13 anim" + (" fx fy A" if pad else "") + (" cam" if camera else ""))
    last = None
    shown = 0
    for i, r in enumerate(rows):
        t = times[i]
        if t < start or t > end:
            continue
        f = {k: float(r[k]) for k in ("pos_x", "pos_y", "pos_z", "vel_x", "vel_y", "vel_z", "ground_x", "ground_y", "ground_z", "rot_y")}
        key = (r["behavior"], r["substate"], f["pos_x"], f["pos_y"], f["pos_z"], f["vel_x"], f["vel_y"], f["vel_z"], r.get("pad_fx"), r.get("pad_fy"), r.get("pad_buttons"), r.get("cam_eye_x"), r.get("cam_eye_z"), r.get("cam_target_x"), r.get("cam_target_z"))
        if key == last:
            continue
        last = key
        shown += 1
        if (shown - 1) % every:
            continue
        pl = math.hypot(f["vel_x"], f["vel_z"])
        s3 = math.sqrt(pl * pl + f["vel_y"] ** 2)
        face = wrap_degrees(math.degrees(-f["rot_y"] - math.pi))
        cells = [f"{t:.3f}", f"{r['behavior']}/{r['substate']}",
                 f"{f['pos_z'] - z0:.2f}", f"{f['pos_y'] - y0:.2f}", f"{-(f['pos_x'] - x0):.2f}",
                 f"{f['vel_z']:.2f}", f"{f['vel_y']:.2f}", f"{-f['vel_x']:.2f}", f"{pl:.2f}", f"{s3:.2f}",
                 f"{f['ground_z']:.3f}", f"{f['ground_y']:.3f}", f"{-f['ground_x']:.3f}", f"{face:.1f}",
                 number(word(r["bhv_words"], 11)), number(word(r["bhv_words"], 12)), number(word(r["bhv_words"], 13)), r["anim_idx"]]
        if pad:
            cells += [f"{float(r['pad_fx']):.2f}", f"{float(r['pad_fy']):.2f}", str(int(r["pad_buttons"], 16) & 1)]
        if camera:
            cells.append("-" if not r["cam_eye_x"] else f"{wrap_degrees(math.degrees(math.atan2(float(r['cam_target_z']) - float(r['cam_eye_z']), float(r['cam_target_x']) - float(r['cam_eye_x']))) - 90):.1f}")
        print(" ".join(cells))
        if contacts:
            words = {}
            for column, base in (("hit_raw", 0x68), ("ground_raw", 0x170)):
                for n, w in enumerate(r[column].split()):
                    words[base + 4 * n] = w
            changed = [f"+{o:x}={w}" for o, w in words.items() if ((o not in previous and w != "00000000") or (o in previous and previous[o] != w)) and (words_wanted is None or o in words_wanted)]
            previous = words
            print(f"  a0={r['flags_a0']} a4={r['flags_a4']} bits={r['flag_bits'] or '-'} | " + (" ".join(changed) or "same"))


def port(path, name, start, end, every):
    run = []
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        if f[0] == "REPLAY" and f[1] == "begin" and f[2] == name:
            run = []
        elif f[0] == "REPLAY" and f[1] == name:
            run.append(f)
    if not run:
        sys.exit("no REPLAY lines for " + name)
    x0, y0, z0 = (float(v) for v in run[0][4:7])
    print(f"# port {path} {name} (last run) | pos from start | head = control heading, face = model facing, rate = turn rate | gr = grounded, g = ground normal, wall = touching a wall, w = wall normal")
    print("t motion x y z vx vy vz pl s3 head face rate gr gx gy gz wall wx wy wz")
    for n, f in enumerate(r for r in run if start <= float(r[3]) <= end):
        if n % every:
            continue
        x, y, z, vx, vy, vz = map(float, f[4:10])
        pl = math.hypot(vx, vz)
        cells = [f"{float(f[3]):.3f}", f[10], f"{x - x0:.2f}", f"{y - y0:.2f}", f"{z - z0:.2f}", f"{vx:.2f}", f"{vy:.2f}", f"{vz:.2f}", f"{pl:.2f}", f"{math.sqrt(pl * pl + vy * vy):.2f}"]
        if len(f) >= 15:
            cells += [f"{wrap_degrees(math.degrees(float(f[11]))):.1f}", f"{wrap_degrees(math.degrees(float(f[12]))):.1f}", f"{float(f[14]):.2f}"]
        if len(f) >= 23:
            cells += [f[15], f"{float(f[16]):.3f}", f"{float(f[17]):.3f}", f"{float(f[18]):.3f}", f[19],
                      f"{float(f[20]):.3f}", f"{float(f[21]):.3f}", f"{float(f[22]):.3f}"]
        print(" ".join(cells))


def sadx_row(o):
    pl = math.hypot(o["v"][0], o["v"][2])
    cells = [f"{o['t']:.3f}", o["beh"], f"{o['pos'][0]:.2f}", f"{o['pos'][1]:.2f}", f"{o['pos'][2]:.2f}", f"{o['v'][0]:.2f}", f"{o['v'][1]:.2f}", f"{o['v'][2]:.2f}", f"{pl:.2f}",
             f"{math.sqrt(pl * pl + o['v'][1] ** 2):.2f}", f"{wrap_degrees(math.degrees(o['rot'])):.1f}", f"{o['g'][0]:.3f}", f"{o['g'][1]:.3f}", f"{o['g'][2]:.3f}", f"{o['work0']:.3f}",
             f"{wrap_degrees(math.degrees(o['stick'][0])):.1f}", f"{o['stick'][1]:.2f}", str(o["buttons"][0]), str(o["buttons"][1])]
    return cells


def sadx(path, name, start, end, every):
    columns = "t beh x y z vx vy vz pl s3 face gx gy gz w0 sa sm a b"
    if name is None:
        rows = sadx_original(path)
        print("# sadx original " + path + " | original axes, absolute positions, velocities per second, last row of each tick")
        print(columns)
        extra = lambda o: []
    else:
        by_tick = sadx_port(path, name)
        rows = [by_tick[k] for k in sorted(by_tick)]
        for o in rows:
            o["t"] = o["tick"] / 60
        print(f"# sadx port {path} {name} (last run) | original axes, velocities per second, row t = the port's result of step t - 1 | gr = grounded, wall = touching a wall, w = wall normal")
        print(columns + " gr wall wx wy wz")
        extra = lambda o: [o["grounded"], o["wall"]] + [f"{v:.3f}" for v in o["wn"]]
    n = 0
    for o in rows:
        if o["t"] < start or o["t"] > end:
            continue
        n += 1
        if (n - 1) % every:
            continue
        print(" ".join(sadx_row(o) + extra(o)))


def main():
    args = sys.argv[1:]
    game = None
    if "--game" in args:
        i = args.index("--game")
        game = args[i + 1]
        del args[i:i + 2]
    options = {"--from": 0.0, "--to": math.inf, "--every": 1, "--words": ""}
    for flag in list(options):
        if flag in args:
            i = args.index(flag)
            options[flag] = args[i + 1] if flag == "--words" else int(args[i + 1]) if flag == "--every" else float(args[i + 1])
            del args[i:i + 2]
    contacts = "--contacts" in args
    absolute = "--absolute" in args
    args = [a for a in args if a not in ("--contacts", "--absolute")]
    words_wanted = {int(w, 16) for w in options["--words"].split(",")} if options["--words"] else None
    if game == "sadx":
        sadx(args[0], args[1] if len(args) > 1 else None, options["--from"], options["--to"], options["--every"])
    elif len(args) == 1 and args[0].endswith(".csv"):
        original(args[0], options["--from"], options["--to"], options["--every"], contacts, absolute, words_wanted)
    elif len(args) == 2:
        port(args[0], args[1], options["--from"], options["--to"], options["--every"])
    else:
        sys.exit(__doc__)


main()
