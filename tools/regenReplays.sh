#!/bin/sh
# Regenerates every replay in Scripts/shadow/replays/ from its recording tools/live_<name>.csv (the names of ORDER in Scripts/shadow/replay.lua).
# Usage: sh tools/regenReplays.sh [name ...]   (no names = all)
# With --game sadx first: the SADX Sonic recordings tools/<name>.csv -> Scripts/sadx/replays/<name>.lua (the names of ORDER in Scripts/sadx/replay.lua; default sadx_1).
cd "$(dirname "$0")/.." || exit 1
if [ "$1" = "--game" ] && [ "$2" = "sadx" ]; then
	shift 2
	names="${*:-sadx_1}"
	for n in $names; do
		echo "== $n"
		python3 tools/makeReplay.py --game sadx "tools/$n.csv" "Scripts/sadx/replays/$n.lua" || echo "FAILED: $n"
	done
	exit 0
fi
names="${*:-accel fall_stickhold jumpdash_holdstick jumpdash_holdstick2 jumpdash_all jumpdash_stick rotation run slopes slow_walk stand_jumpdash}"
for n in $names; do
	echo "== $n"
	python3 tools/makeReplay.py "tools/live_$n.csv" "Scripts/shadow/replays/live_$n.lua" || echo "FAILED: $n"
done
