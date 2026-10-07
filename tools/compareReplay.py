"""
Compares a recording of the original with the port's replay of it.

    python3 tools/compareReplay.py tools/live_run.csv port_stdout.txt live_run [step_seconds] [--ai]

--ai prints a compact form instead of the tables (for Claude, see print_compact): one summary line, then one row per step with only the original's values and the port's differences, quiet stretches collapsed.

The port's lines are the tab separated REPLAY lines of Scripts/shadow/replay.lua (the other lines of the file are ignored; the last replay of that name counts). Only quantities that do not depend on the axes are compared, since the port's world
is the original's turned (docs/ARCHITECTURE.md): planar speed, vertical speed and height above the start (the port's height is taken at SETTLE_TIME, after the spawn has dropped onto the ground), and the path drift: the port's horizontal distance from the start minus the original's, with the original's (x, z) turned into the port's axes ((x, z) goes to (z, -x), hypothesis, see replay.lua), as dx, dz and their length. The original's rows are put on a frame clock (frame_times: gaps rounded to whole 1/60 s frames, rows of one frame share a time) instead of the wall clock; the port's are per tick.
"""
import csv
import sys


SETTLE_TIME = 0.25


def frame_times(rows):
    """Recording rows (dicts of the CSV) to game-frame times in seconds: the wall clock, plus the frames Dolphin ran inside one poll. Observed on live_run: the jump's first three speeds (vy 100, 97.52, 95.08) were read less than 8 ms apart, so the game ran about 1.5 frames faster than the wall clock there and every later row is that much later in game time (the port's jump was two frames out of step before this). A chain of three or more consecutive rows, each under 8 ms after the one before with a different (planar speed, vy), adds (rows - 1) - (its length in seconds x 60) frames to every later row; chains in the first 0.05 s are the logger's start-up and are ignored. The wall clock alone matched the running recordings (live_accel, live_half: 127 distinct speed steps in 126 wall-clock frames); counting frames from position or velocity changes overcounted them by 5 to 8 per cent (the game's values are read in several polls), so that was dropped. Hypothesis: the game runs 60 frames a second."""
    # With a vi_count column (the logger's video retrace count, 0x805f1024; H: +1 per game tick, observed on live_slopes 2026-10-07: +1 per position change in 507 of 598), the time is the count's difference to the first row over 60, which replaces everything below.
    if rows and rows[0].get("vi_count"):
        first = int(rows[0]["vi_count"])
        return [(int(x["vi_count"]) - first) / 60 for x in rows]

    def key(x):
        return round(float(x["planar"]), 3), round(float(x["vel_y"]), 3)

    stamps = [float(x["t"]) for x in rows]
    extra = [0.0] * len(rows)
    start = 0
    for i in range(1, len(rows) + 1):
        if i < len(rows) and key(rows[i - 1]) != key(rows[i]) and stamps[i] - stamps[i - 1] < 0.008:
            continue
        if i - start >= 3 and stamps[start] - stamps[0] > 0.05:
            for j in range(i, len(rows)):
                extra[j] += (i - start - 1) - (stamps[i - 1] - stamps[start]) * 60
        start = i
    return [(stamps[i] - stamps[0]) + extra[i] / 60 for i in range(len(rows))]


def at(series, t):
    best = series[0]
    for row in series:
        if row[0] > t:
            break
        best = row
    return best


def print_facing(rows, times, extra, step):
    """Facing table (port logs older than this have no such columns and skip it): both in the port's axes, degrees. Original: its velocity heading atan2(-vel_x, vel_z) and its facing -rot_y - pi/2 - pi/2 (rot_y in radians; hypothesis, see docs/REPLAY_COMPARISON.md);
    port: the velocity heading, the control heading and the model facing, with the speed and rate the turn used."""
    import math
    orig = []
    for i, x in enumerate(rows):
        vx, vz = float(x["vel_x"]), float(x["vel_z"])
        orig.append((times[i], math.degrees(math.atan2(-vx, vz)), math.degrees(-float(x["rot_y"]) - math.pi), (vx * vx + vz * vz) ** 0.5))
    print("\nfacing (degrees, port axes)\n    t | orig vel-heading  facing  planar | port vel-heading  control  facing  turnSpeed  rate")
    count = 0
    t = 0.0
    while t <= min(orig[-1][0], extra[-1][0]):
        o, e = at(orig, t), at(extra, t)
        print(f"{t:5.2f} | {o[1]:16.1f} {o[2]:7.1f} {o[3]:7.1f} | {math.degrees(math.atan2(e[6], e[5])):16.1f} {math.degrees(e[1]):8.1f} {math.degrees(e[2]):7.1f} {e[3]:9.1f} {e[4]:5.2f}")
        count += 1
        t = count * step


REVERSAL_DEGREES = 170
TURN_LOOK = 0.05


def stick_reversals(rows, times, extra):
    """Stick reversals: rows where the pad stick, held on both rows, points REVERSAL_DEGREES or more away from the row before. Observed (live_jumpdash_stick 1.66 s, 2026-10-06): after a stick flip to almost exactly the opposite way the heading is about 180 degrees from the stick, and which way
    the turn goes is decided by a sub-degree difference in the heading (the port's was 1.4 degrees off and turned the other way, then everything after differed). So the comparison after such a row says nothing about the rules unless both turned the same way. Turn sides in the port's
    axes over TURN_LOOK seconds: the original's facing -rot_y, the port's control heading."""
    import math

    def wrap(a):
        return (a + math.pi) % (2 * math.pi) - math.pi

    found = []
    for i in range(1, len(rows)):
        ax, ay = float(rows[i - 1]["pad_fx"]), float(rows[i - 1]["pad_fy"])
        bx, by = float(rows[i]["pad_fx"]), float(rows[i]["pad_fy"])
        if math.hypot(ax, ay) < 0.01 or math.hypot(bx, by) < 0.01:
            continue
        turn = abs(math.degrees(wrap(math.atan2(by, bx) - math.atan2(ay, ax))))
        if turn < REVERSAL_DEGREES:
            continue
        later = next((j for j in range(i, len(rows)) if times[j] >= times[i] + TURN_LOOK), len(rows) - 1)
        original = -wrap(float(rows[later]["rot_y"]) - float(rows[i - 1]["rot_y"]))
        port = None
        if any(e[1] for e in extra):
            before, after = nearest(extra, times[i - 1]), nearest(extra, times[later])
            port = wrap(after[1] - before[1])
        found.append((times[i], turn, original, port))
    return found


def reversal_verdict(original, port):
    side = lambda a: "+" if a > 0 else "-"
    return "no port headings" if port is None else ("same side" if side(original) == side(port) else "SIDES DIFFER")


def find_reversals(rows, times, extra):
    """(time, verdict) of every stick reversal, for the --ai summary."""
    return [(t, reversal_verdict(original, port)) for t, _, original, port in stick_reversals(rows, times, extra)]


def print_reversals(rows, times, extra):
    import math
    found = stick_reversals(rows, times, extra)
    if not found:
        return
    print("\nstick reversals (the turn side after one is decided by sub-degree heading differences; the comparison after a row whose sides differ is not evidence)")
    for t, turn, original, port in found:
        verdict = reversal_verdict(original, port)
        port_text = "" if port is None else f", port {math.degrees(port):+.1f}"
        print(f"{t:6.3f}: stick turned {turn:.1f} degrees; turn over {TURN_LOOK} s: original {math.degrees(original):+.1f}{port_text} degrees: {verdict}")


def nearest(series, t):
    # Observed (live_skid, 0.8 to 1.1 s): the sample times are multiples of 0.1 s, exactly tick times of the port, and the original's rows lie 0 to 1 tick before them (the stamps drift against the tick grid), so taking the port's last row at or before t compared a tick that depended on float noise (a false -2.9 planar). The port's row is the one nearest the original row's own time.
    return min(series, key=lambda row: abs(row[0] - t))


# The bounds of a quiet sample. QUIET_PLANAR is just over one tick of full-stick acceleration (2.92), the known 1 to 2 frame alignment of the recordings; the facing is only judged at a planar speed of FACING_FROM or more (at rest the logged facing is a leftover).
QUIET_PLANAR = 3.5
QUIET_VY = 2.0
QUIET_HEIGHT = 1.0
QUIET_DRIFT = 1.0
QUIET_FACING = 2.0
FACING_FROM = 30.0
GROUND_PRESS_VY = -11.34


def print_compact(name, step, samples, reversals, worst, worst_drift):
    """The --ai form. A sample is (t, orig planar, d planar, orig vy, d vy, orig height, d height, drift, orig behavior, port motion, d facing or None). A sample is quiet when every difference is under its QUIET_* bound (d vy also counts as quiet at the known ground
    press GROUND_PRESS_VY) and the drift moved less than QUIET_DRIFT since the last printed row; consecutive quiet samples are one line `t0-t1 ok`. The first sample over each bound is named in the summary."""
    import math

    def quiet(s, drift_ref):
        dvy_ok = abs(s[4]) < QUIET_VY or abs(s[4] - GROUND_PRESS_VY) < QUIET_VY
        return abs(s[2]) < QUIET_PLANAR and dvy_ok and abs(s[6]) < QUIET_HEIGHT and abs(s[7] - drift_ref) < QUIET_DRIFT and (s[10] is None or abs(s[10]) < QUIET_FACING)

    first = {}
    for s in samples:
        for label, value, bound in (("pl", abs(s[2]), 5), ("h", abs(s[6]), 2), ("drift", s[7], 10), ("face", abs(s[10] or 0), 5)):
            if value > bound and label not in first:
                first[label] = s[0]
    firsts = " ".join(f"{k}@{v:.2f}" for k, v in first.items()) or "none"
    revs = " ".join(f"{t:.2f}:{verdict}" for t, verdict in reversals) or "none"
    print(f"# {name} step {step} | max dpl {worst[0]:.1f} dvy {worst[1]:.1f} dh {worst[2]:.1f} drift {worst_drift:.1f} | first over (pl>5 h>2 drift>10 face>5): {firsts} | stick reversals: {revs}")
    print("# d = port - orig; dvy -11.3 on ground = known press; face = port control heading - orig facing (deg); beh = orig behavior/port motion")
    print("t pl dpl vy dvy h dh drift face beh")
    run_start = None
    drift_ref = 0.0
    previous = None
    for s in samples:
        if quiet(s, drift_ref):
            if run_start is None:
                run_start = s[0]
            previous = s
            continue
        if run_start is not None:
            print(f"{run_start:.2f}-{previous[0]:.2f} ok")
            run_start = None
        drift_ref = s[7]
        face = "-" if s[10] is None else f"{s[10]:.1f}"
        print(f"{s[0]:.2f} {s[1]:.1f} {s[2]:.1f} {s[3]:.1f} {s[4]:.1f} {s[5]:.1f} {s[6]:.1f} {s[7]:.1f} {face} {s[8]}/{s[9]}")
    if run_start is not None:
        print(f"{run_start:.2f}-{previous[0]:.2f} ok")


def main():
    compact = "--ai" in sys.argv
    if compact:
        sys.argv.remove("--ai")
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    step = float(sys.argv[4]) if len(sys.argv) > 4 else 0.1
    rows = list(csv.DictReader(open(sys.argv[1])))
    t0, y0 = float(rows[0]["t"]), float(rows[0]["pos_y"])
    x0x, x0z = float(rows[0]["pos_x"]), float(rows[0]["pos_z"])
    times = frame_times(rows)
    original = [(times[i], (float(x["vel_x"]) ** 2 + float(x["vel_z"]) ** 2) ** 0.5, float(x["vel_y"]), float(x["pos_y"]) - y0, x["behavior"], float(x["pos_z"]) - x0z, -(float(x["pos_x"]) - x0x)) for i, x in enumerate(rows)]
    port, extra = [], []
    for line in open(sys.argv[2]):
        f = line.rstrip("\n").split("\t")
        if f[0] == "REPLAY" and f[1] == "begin" and f[2] == sys.argv[3]:
            port, extra = [], []
        elif f[0] == "REPLAY" and f[1] == sys.argv[3]:
            t, x, y, z, vx, vy, vz = map(float, f[3:10])
            port.append((t, (vx * vx + vz * vz) ** 0.5, vy, y, f[10], x, z))
            extra.append((t, *(map(float, f[11:15]) if len(f) >= 15 else (0.0, 0.0, 0.0, 0.0)), vx, vz))
    if not port:
        sys.exit("no REPLAY lines for " + sys.argv[3])
    # A REPLAY line's position is taken before the tick's move and its velocity after the behaviours, the original's row has both after the frame's move: the port's position of a row is the next row's (observed, live_slopes: a constant 1.1 tick path lag from 0.3 s, 3.3 units at 1.0 s, gone with this shift).
    port = [row[:3] + (nxt[3],) + row[4:5] + (nxt[5], nxt[6]) for row, nxt in zip(port, port[1:] + port[-1:])]
    y_start = at(port, SETTLE_TIME)[3]
    if not compact:
        print("    t | planar speed orig   port     diff | vy orig    port     diff | height orig   port     diff | behavior orig / motion port | path drift dx dz dist")
    import math
    has_facing = any(e[1] for e in extra)
    facing = [(times[i], math.degrees(-float(x["rot_y"]) - math.pi)) for i, x in enumerate(rows)]
    samples = []
    worst = [0.0, 0.0, 0.0]
    worst_drift = 0.0
    px0, pz0 = port[0][5], port[0][6]
    count = 0
    t = 0.0
    while t <= min(original[-1][0], port[-1][0]):
        o = at(original, t)
        p = nearest(port, o[0])
        d = (p[1] - o[1], p[2] - o[2], (p[3] - y_start) - o[3])
        worst = [max(w, abs(v)) for w, v in zip(worst, d)]
        drift = (p[5] - px0 - o[5], p[6] - pz0 - o[6])
        dist = (drift[0] ** 2 + drift[1] ** 2) ** 0.5
        worst_drift = max(worst_drift, dist)
        face = None
        if has_facing:
            if o[1] >= FACING_FROM:
                face = (math.degrees(nearest(extra, o[0])[1]) - at(facing, t)[1] + 180) % 360 - 180
        samples.append((t, o[1], d[0], o[2], d[1], o[3], d[2], dist, o[4], p[4], face))
        if not compact:
            print(f"{t:5.2f} | {o[1]:19.2f} {p[1]:7.2f} {d[0]:8.2f} | {o[2]:7.2f} {p[2]:7.2f} {d[1]:8.2f} | {o[3]:11.2f} {p[3] - y_start:7.2f} {d[2]:8.2f} | {o[4]} / {p[4]} | {drift[0]:8.1f} {drift[1]:8.1f} {dist:8.1f}")
        count += 1
        t = count * step
    if compact:
        print_compact(sys.argv[3], step, samples, find_reversals(rows, times, extra), worst, worst_drift)
        return
    if has_facing:
        print_facing(rows, times, extra, step)
    print_reversals(rows, times, extra)
    print(f"largest differences: planar speed {worst[0]:.2f}, vertical speed {worst[1]:.2f}, height {worst[2]:.2f}, path drift {worst_drift:.1f}")


if __name__ == "__main__":
    main()
