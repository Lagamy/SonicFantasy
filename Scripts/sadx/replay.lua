-- Replay of a run recorded from the original SADX Sonic, to compare the port's Sonic against it (docs/SADX_SONIC_REFERENCE.md "Replay"). NOT part of the original: a test aid. The recording is a CSV of tools/sadxLiveLog.py;
-- tools/makeReplay.py --game sadx turns it into Scripts/sadx/replays/<name>.lua, which fills SadxReplays[<name>] with {start = {x, y, z}, angle0 (Sonic's rot_y, 16 bit), velocity = {vx, vy, vz}
-- (units per frame, only when the recording began moving), rows = {{t, angle, mag, a, b}, ...}}: one row per game step (the last logger row of each vi_count tick, because that row is after the step), t in seconds
-- from the first tick (vi_count / 60; the start row's own input is not in rows, it is already in the start state, so row t = (tick - 1) / 60: the port's step k uses the input of the original's tick k + 1), angle = the game's world stick (the camera already applied, 16 bit, forward = (cos, sin) over x and z as his rot_y: observed on sadx_1, velocity heading minus rot_y mean 0.008 rad),
-- mag = the stick's magnitude 0..1 (the cubic response), a = the jump button held (pad_buttons & 0x4), b = the action button held (pad_buttons & 0x402); everything in the original's own axes.
-- While SadxReplay.enabled, R (Scripts/main.lua's respawn) also activates Sonic, puts him at the next recording's start and runs it: the first R the first recording of ORDER, the second the second, round again. Until the last row
-- (and END_HOLD after it) he reads those inputs in place of the keyboard, and every tick prints one tab separated line starting with REPLAY (stdout) for the comparison tools: REPLAY, name, tick, t, x, y, z (before this tick's
-- move), vx, vy, vz (units per second, as this tick's script left them), motion, heading (radians, port axes), grounded, ground normal (3), touchingWall, wall normal (3), work0 (the forward speed the motion rate reads, per
-- frame), velocity at the start of the tick (3, as the engine step left it), the tick's input (stick x, stick z, magnitude, a, b). Pairing: the original's row N holds the state AFTER step N, which used the input of row N
-- (observed, sadx_1 task 2); the port's step N prints its input and the position before the move, so the port's result of step N is the position of line N + 1 and its velocity the velocity of line N; with the start row left out of
-- rows, that result pairs with the original's tick N + 1 (tools/compareReplay.py --game sadx).
-- Lua has no file output, so redirect the app's stdout to a file. Set SadxReplay.enabled to false to get R back as a plain respawn.
-- Hypothesis: the port's world is the original's (same axes and angles); no turn like Shadow's RenderWareYawDegrees is applied. Unverified until a replay is run.

SadxReplays = SadxReplays or {}
SadxReplay = {enabled = true}

-- The recordings R cycles through, in order (names of SadxReplays entries; add one with its file's sf.include in Scripts/sadx/main.lua).
local ORDER = {"sadx_4", "sadx_1", "sadx_2", "sadx_3"}
local END_HOLD = 0.3
-- A row counts as reached from half a tick before its stamp (float noise of vi_count / 60 against tick * tickTime).
local ROW_TOLERANCE_TICKS = 0.5
local ANGLE_UNIT = 2 * math.pi / 65536
-- The offset from the recorded position (the game's task position) to the engine's body position: observed on the 2026-10-10 run, the body stood at y 9.30 from the first line for a recorded y 4.00 (compareReplay: 5.300 at
-- tick 15). With 0.0 the engine pushed the body out of the floor on the first tick; with this it starts where it settles.
local START_LIFT = 5.3
local current = nil
local nextIndex = 1

-- Puts Sonic at the start of the next recording and begins it. Called by main.lua after the respawn.
function SadxReplay.start(entity)
	if #ORDER == 0 then error("SadxReplay: ORDER is empty") end
	local name = ORDER[nextIndex]
	nextIndex = nextIndex % #ORDER + 1
	local data = SadxReplays[name]
	if data == nil then error("no replay '" .. name .. "'") end
	local state = sf.state(entity)
	local active = state.active
	for key in pairs(state) do state[key] = nil end
	initSonicState(state)
	state.active = active
	sf.setPosition(entity, data.start[1], data.start[2] + START_LIFT, data.start[3])
	-- The recording's velocity is per frame, the engine's per second.
	local v = data.velocity
	if v then sf.setVelocity(entity, v[1] * 60, v[2] * 60, v[3] * 60) else sf.setVelocity(entity, 0, 0, 0) end
	state.heading = data.angle0 * ANGLE_UNIT
	current = {name = name, data = data, tick = 0, index = 1}
	print(string.format("REPLAY\tbegin\t%s\ttickTime %.6f", name, sf.tickTime()))
end

-- Called once a tick by the player before it reads its input. Returns nil when no replay runs, else the input of this tick: wx, wz (the stick in world terms, length = the magnitude), a, b (the buttons held).
function SadxReplay.step()
	if current == nil then return nil end
	local rows = current.data.rows
	local time = current.tick * sf.tickTime()
	if time > rows[#rows][1] + END_HOLD then
		print("REPLAY\tend\t" .. current.name)
		current = nil
		return nil
	end
	while rows[current.index + 1] ~= nil and rows[current.index + 1][1] <= time + ROW_TOLERANCE_TICKS * sf.tickTime() do current.index = current.index + 1 end
	local row = rows[current.index]
	current.tick = current.tick + 1
	current.time = time
	local angle = row[2] * ANGLE_UNIT
	current.input = {wx = math.cos(angle) * row[3], wz = math.sin(angle) * row[3], mag = row[3], a = row[4] == 1, b = row[5] == 1}
	return current.input
end

-- Live logging: while no recording runs, every tick of the player's own play is kept as a LIVE line in a ring of the last LIVE_TICKS ticks (10 s), and holding K once prints them (stdout, between LIVE begin and
-- LIVE end lines; one dump per press). A line has the REPLAY line's fields after the name: tick, time, position, velocity, motion, heading, grounded, ground normal, wall, wall normal, work0, tick-start
-- velocity, input (stick x, stick z, magnitude, a, b). Ticks count from the first live tick since the last replay.
local LIVE_TICKS = 600
local liveRing = {}
local liveTick = 0
local liveKHeld = false

local function fields(entity, motion, wx, wz, mag, a, b)
	local x, y, z = sf.position(entity)
	local vx, vy, vz = sf.velocity(entity)
	local state = sf.state(entity)
	local gnx, gny, gnz = sf.groundNormal(entity)
	local wnx, wny, wnz = sf.wallNormal(entity)
	return string.format("%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%s\t%.5f\t%d\t%.4f\t%.4f\t%.4f\t%d\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%d\t%d",
		x, y, z, vx, vy, vz, motion, state.heading or 0, sf.grounded(entity) and 1 or 0, gnx, gny, gnz, sf.touchingWall(entity) and 1 or 0, wnx, wny, wnz,
		state.work0 or 0, state.logV0x or 0, state.logV0y or 0, state.logV0z or 0, wx, wz, mag, a and 1 or 0, b and 1 or 0)
end

local function liveLog(entity, motion, wx, wz, mag, a, b)
	liveRing[liveTick % LIVE_TICKS + 1] = string.format("LIVE\t%d\t%.4f\t%s", liveTick, liveTick * sf.tickTime(), fields(entity, motion, wx, wz, mag, a, b))
	liveTick = liveTick + 1
	local held = not sf.debugMode() and sf.key("K")
	if held and not liveKHeld then
		print("LIVE\tbegin\ttickTime " .. string.format("%.6f", sf.tickTime()))
		for i = math.max(liveTick - LIVE_TICKS, 0), liveTick - 1 do print(liveRing[i % LIVE_TICKS + 1]) end
		print("LIVE\tend")
	end
	liveKHeld = held
end

-- Called by the player at the end of every tick it ran (motion = the name of the motion it chose), with the input it used.
function SadxReplay.log(entity, motion, wx, wz, mag, a, b)
	if current == nil then liveLog(entity, motion, wx, wz, mag, a, b) return end
	print(string.format("REPLAY\t%s\t%d\t%.4f\t%s", current.name, current.tick - 1, current.time, fields(entity, motion, wx, wz, mag, a, b)))
end
