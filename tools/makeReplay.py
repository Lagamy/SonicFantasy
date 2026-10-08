"""
Turns a recording of tools/shadowLiveLog.py (made with --pad) into a Lua replay for Scripts/shadow/replay.lua, or checks the camera heading of a recording.

    python3 tools/makeReplay.py tools/live_run.csv Scripts/shadow/replays/live_run.lua
    python3 tools/makeReplay.py tools/live_run.csv --check-camera [--ai]

Per recording: start = the first row's position. Per row: t (seconds from the first row, in game frames: frame_times), the stick as camera-relative forward and right parts (pad_fx right when positive, pad_fy forward when negative), a = the jump button (bit 0 of pad_buttons), phi = the camera's heading.
phi, recordings with the camera columns (cam_eye_*, cam_target_*, since 2026-10-06): the heading over the original's x / z of target - eye, phi = atan2(dz, dx) (external, decompile of FUN_80093dd0: the stick's forward is turned to that direction's yaw; see shadowLiveLog.py read_camera). A row whose camera cells are empty holds the last known phi (the first known before it).
phi, older recordings: read only from the rows that hold the player's world-space stick vector (stick_x / stick_z, player +0xb8, set for part of a frame) with the stick held: its angle minus the pad stick's angle (right = +pad_fx, which had the smaller spread on every recording where it was compared), observed; between those rows phi is interpolated linearly (hypothesis: the camera turns smoothly) and held before the first and after the last. A recording that holds the stick with no such row is refused. A recording with the stick never held gets phi 0 (it is not used).
heading0, the way he faces at the start, is the angle of the velocity at the first row moving faster than START_SPEED (hypothesis).
velocity = {vx, vy, vz}, written only when the first row's planar speed is above START_SPEED (the recording began already moving, e.g. live_uphill_jump: planar 22 at row 0): the first row's velocity in the original's axes, which replay.lua gives the player at the start instead of 0. Recordings from rest have no such field.
--check-camera prints, on every row holding both, the camera phi next to the stick-vector phi and their difference (degrees), with the 3D speed, the ground normal's y and "fast" when the player's +0xa0 bit 0 is set (s3 >= 250: the game then turns the stick in the ground's basis +0x1e4, external, so the two are expected to differ on slopes). --ai: one summary line (count, mean and max difference, the same for slow flat rows, and for the camera one frame earlier), then only the rows more than 1 degree apart, quiet stretches collapsed to "t0-t1 ok (n)". Writes nothing.
"""
import csv
import math
import sys

START_SPEED = 10


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


def stick_points(rows, times):
    """(row index, phi) for every row holding the world stick vector with the pad stick held (see the docstring), and the rows holding the stick."""
    held = [i for i, x in enumerate(rows) if math.hypot(float(x["pad_fx"]), float(x["pad_fy"])) > 0.3]
    points = []
    for i in held:
        x = rows[i]
        sx, sz = float(x["stick_x"]), float(x["stick_z"])
        if math.hypot(sx, sz) > 0.1:
            points.append((i, math.atan2(sz, sx) - math.atan2(float(x["pad_fx"]), -float(x["pad_fy"]))))
    return points, held


def camera_pitch(row):
    """Vertical part of the camera's target - eye per unit of its horizontal length, or None when the row has no camera."""
    if not row.get("cam_eye_x"):
        return None
    dx, dz = float(row["cam_target_x"]) - float(row["cam_eye_x"]), float(row["cam_target_z"]) - float(row["cam_eye_z"])
    return (float(row["cam_target_y"]) - float(row["cam_eye_y"])) / max(math.hypot(dx, dz), 1e-6)


def camera_phi(row):
    """Heading of the camera's target - eye over the original's x / z, or None when the row has no camera."""
    if not row.get("cam_eye_x"):
        return None
    return math.atan2(float(row["cam_target_z"]) - float(row["cam_eye_z"]), float(row["cam_target_x"]) - float(row["cam_eye_x"]))


def wrap_degrees(a):
    return (a + 180) % 360 - 180


def check_camera(rows, times, ai):
    points, _ = stick_points(rows, times)
    cams = [camera_phi(x) for x in rows]
    pairs = [(i, cams[i], phi) for i, phi in points if cams[i] is not None]
    if not pairs:
        sys.exit("no row holds both the camera and the world stick vector")

    def earlier(i):
        """Camera phi of the last row at least half a frame before row i (hypothesis: the stick is turned by the camera of the frame before)."""
        j = i
        while j > 0 and times[j] > times[i] - 0.5 / 60:
            j -= 1
        return cams[j]

    cells = []
    for i, cam, stick in pairs:
        x = rows[i]
        fast = int(x["flags_a0"], 16) & 1
        cells.append((times[i], math.degrees(cam), math.degrees(stick), wrap_degrees(math.degrees(stick - cam)), float(x["speed3d"]), float(x["ground_y"]), fast))

    def stats(values):
        values = [abs(v) for v in values]
        return f"n {len(values)} mean {sum(values) / len(values):.2f} max {max(values):.2f}" if values else "n 0"

    if not ai:
        print("t cam_phi stick_phi diff s3 ground_y fast (degrees, original axes)")
        for t, cam, stick, d, s3, gy, fast in cells:
            print(f"{t:.3f} {cam:8.2f} {stick:8.2f} {d:6.2f} {s3:7.1f} {gy:.3f} {'fast' if fast else ''}")
    worst = max(cells, key=lambda c: abs(c[3]))
    slow_flat = [c[3] for c in cells if not c[6] and c[5] >= 0.99]
    lagged = [wrap_degrees(math.degrees(stick - earlier(i))) for i, _, stick in pairs if earlier(i) is not None]
    print(f"camera check: all {stats([c[3] for c in cells])} (max at {worst[0]:.3f}) | slow flat (a0 bit0 clear, gy>=0.99) {stats(slow_flat)} | camera one frame earlier {stats(lagged)}")
    if ai:
        print("t cam stick diff s3 gy fast")
        quiet = []
        for c in cells:
            if abs(c[3]) <= 1:
                quiet.append(c[0])
                continue
            if quiet:
                print(f"{quiet[0]:.3f}-{quiet[-1]:.3f} ok ({len(quiet)})")
                quiet = []
            print(f"{c[0]:.3f} {c[1]:.2f} {c[2]:.2f} {c[3]:.2f} {c[4]:.1f} {c[5]:.3f} {'fast' if c[6] else ''}")
        if quiet:
            print(f"{quiet[0]:.3f}-{quiet[-1]:.3f} ok ({len(quiet)})")


def main():
    args = sys.argv[1:]
    check = "--check-camera" in args
    ai = "--ai" in args
    args = [a for a in args if a not in ("--check-camera", "--ai")]
    if len(args) != (1 if check else 2):
        sys.exit(__doc__)
    rows = list(csv.DictReader(open(args[0])))
    if "pad_sx" not in rows[0]:
        sys.exit("no pad columns: record with --pad")
    times = frame_times(rows)
    if check:
        check_camera(rows, times, ai)
        return
    start = (float(rows[0]["pos_x"]), float(rows[0]["pos_y"]), float(rows[0]["pos_z"]))
    data = []
    for i, x in enumerate(rows):
        vx, vz = float(x["vel_x"]), float(x["vel_z"])
        data.append((times[i], -float(x["pad_fy"]), float(x["pad_fx"]), int(x["pad_buttons"], 16) & 1, math.atan2(vz, vx), math.hypot(vx, vz)))
    # Observed (REFERENCE, replay notes): when the logger misses the frame where a jump starts, the first pressed row already shows vy 97.52 instead of 100
    # and the press would reach the port one frame late. The press edge moves to the row before it, when that row is at most 1.5 frames earlier.
    for i in range(1, len(data)):
        t, f, r, a, h, s = data[i]
        vy = float(rows[i]["vel_y"])
        if a and not data[i - 1][3] and 97.0 < vy < 98.0 and t - data[i - 1][0] <= 1.5 / 60:
            p = data[i - 1]
            data[i - 1] = (p[0], p[1], p[2], 1, p[4], p[5])
    heads = [0.0] * len(rows)
    pitch = [None] * len(rows)
    heads = [0.0] * len(rows)
    pitch = [None] * len(rows)
    cams = [camera_phi(x) for x in rows]
    known = [c for c in cams if c is not None]
    if known:
        last = known[0]
        for k, c in enumerate(cams):
            last = c if c is not None else last
            heads[k] = last
        pitches = [camera_pitch(x) for x in rows]
        last = next(c for c in pitches if c is not None)
        for k, c in enumerate(pitches):
            last = c if c is not None else last
            pitch[k] = last
        print(f"phi from the camera columns ({len(known)} of {len(rows)} rows hold the camera)")
    else:
        points, held = stick_points(rows, times)
        if held and not points:
            sys.exit("the stick is held but no row holds the world stick vector: the camera heading is unknown")
        if points:
            unwrapped = [(times[points[0][0]], points[0][1])]
            for i, a in points[1:]:
                unwrapped.append((times[i], unwrapped[-1][1] + math.atan2(math.sin(a - unwrapped[-1][1]), math.cos(a - unwrapped[-1][1]))))
            at = 0
            for k, t in enumerate(times):
                while at + 1 < len(unwrapped) and unwrapped[at + 1][0] <= t:
                    at += 1
                (ta, aa), (tb, ab) = unwrapped[at], unwrapped[min(at + 1, len(unwrapped) - 1)]
                heads[k] = aa if t <= ta or tb == ta else aa + (ab - aa) * min(1.0, (t - ta) / (tb - ta))
        print(f"no camera columns: phi from the world stick vector, {len(points)} of {len(rows)} rows hold it with the stick held ({len(held)} rows hold the stick)")
    heading0 = next((v for _, _, _, _, v, speed in data if speed > START_SPEED), 0.0)
    name = args[1].rsplit("/", 1)[-1].removesuffix(".lua")
    with open(args[1], "w") as out:
        out.write(f"-- Generated by tools/makeReplay.py from {args[0]}; do not edit. See replay.lua.\n")
        first = rows[0]
        velocity = ""
        if math.hypot(float(first["vel_x"]), float(first["vel_z"])) > START_SPEED:
            velocity = f"velocity = {{{float(first['vel_x']):.4f}, {float(first['vel_y']):.4f}, {float(first['vel_z']):.4f}}}, "
        out.write(f"ShadowReplays.{name} = {{start = {{{start[0]:.4f}, {start[1]:.4f}, {start[2]:.4f}}}, heading0 = {heading0:.6f}, {velocity}phi = 0.000000, rows = {{\n")
        for i, (t, f, r, a, _, _) in enumerate(data):
            out.write(f"\t{{{t:.4f}, {f:.4f}, {r:.4f}, {a}, {heads[i]:.5f}" + (f", {pitch[i]:.5f}" if pitch[i] is not None else "") + "},\n")
        out.write("}}\n")


main()
