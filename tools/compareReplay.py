"""
Compares a recording of the original with the port's replay of it.

    python3 tools/compareReplay.py tools/live_run.csv port_stdout.txt live_run [step_seconds]

The port's lines are the tab separated REPLAY lines of Scripts/shadow/replay.lua (the other lines of the file are ignored; the last replay of that name counts). Only quantities that do not depend on the axes are compared, since the port's world
is the original's turned (docs/ARCHITECTURE.md): planar speed, vertical speed and height above the start (the port's height is taken at SETTLE_TIME, after the spawn has dropped onto the ground). The original's rows are wall-clock samples (about one frame of alignment), the port's are per tick.
"""
import csv
import sys


SETTLE_TIME = 0.25


def at(series, t):
    best = series[0]
    for row in series:
        if row[0] > t:
            break
        best = row
    return best


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    step = float(sys.argv[4]) if len(sys.argv) > 4 else 0.1
    rows = list(csv.DictReader(open(sys.argv[1])))
    t0, y0 = float(rows[0]["t"]), float(rows[0]["pos_y"])
    original = [(float(x["t"]) - t0, (float(x["vel_x"]) ** 2 + float(x["vel_z"]) ** 2) ** 0.5, float(x["vel_y"]), float(x["pos_y"]) - y0, x["behavior"]) for x in rows]
    port = []
    for line in open(sys.argv[2]):
        f = line.rstrip("\n").split("\t")
        if f[0] == "REPLAY" and f[1] == "begin" and f[2] == sys.argv[3]:
            port = []
        elif f[0] == "REPLAY" and f[1] == sys.argv[3]:
            t, x, y, z, vx, vy, vz = map(float, f[3:10])
            port.append((t, (vx * vx + vz * vz) ** 0.5, vy, y, f[10], x, z))
    if not port:
        sys.exit("no REPLAY lines for " + sys.argv[3])
    y_start = at(port, SETTLE_TIME)[3]
    print("    t | planar speed orig   port     diff | vy orig    port     diff | height orig   port     diff | behavior orig / motion port")
    worst = [0.0, 0.0, 0.0]
    t = 0.0
    while t <= min(original[-1][0], port[-1][0]):
        o, p = at(original, t), at(port, t)
        d = (p[1] - o[1], p[2] - o[2], (p[3] - y_start) - o[3])
        worst = [max(w, abs(v)) for w, v in zip(worst, d)]
        print(f"{t:5.2f} | {o[1]:19.2f} {p[1]:7.2f} {d[0]:8.2f} | {o[2]:7.2f} {p[2]:7.2f} {d[1]:8.2f} | {o[3]:11.2f} {p[3] - y_start:7.2f} {d[2]:8.2f} | {o[4]} / {p[4]}")
        t += step
    print(f"largest differences: planar speed {worst[0]:.2f}, vertical speed {worst[1]:.2f}, height {worst[2]:.2f}")


main()
