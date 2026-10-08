#!/bin/sh
# Regenerates every replay in Scripts/shadow/replays/ from its recording tools/live_<name>.csv (the names of ORDER in Scripts/shadow/replay.lua).
# Usage: sh tools/regenReplays.sh [name ...]   (no names = all)
cd "$(dirname "$0")/.." || exit 1
names="${*:-accel fall_stickhold jumpdash_holdstick jumpdash_holdstick2 jumpdash_all jumpdash_stick rotation run slopes slow_walk stand_jumpdash}"
for n in $names; do
	echo "== $n"
	python3 tools/makeReplay.py "tools/live_$n.csv" "Scripts/shadow/replays/live_$n.lua" || echo "FAILED: $n"
done
