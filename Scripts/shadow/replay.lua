-- Replay of a run recorded from the original game, to compare Shadow's port against it (docs/STH2005_PLAYER_REFERENCE.md, section 20). NOT part of the original: a test aid. The recording is a CSV of tools/shadowLiveLog.py made with --pad;
-- tools/makeReplay.py turns it into Scripts/shadow/replays/<name>.lua, which fills ShadowReplays[<name>] with {start = {x, y, z}, heading0, velocity (only when the recording began moving), phi, rows = {{t, forward, right, a, phi}, ...}} (phi, the camera's heading per row: from the logged camera on recordings since 2026-10-06, from the world stick vector on older ones, see makeReplay.py; t in seconds from the first row, the stick as the
-- camera-relative forward and right parts, a = the jump button, start = where the recording began, all in the original's own axes). While ShadowReplay.enabled, R (main.lua's respawn) also activates Shadow, puts him at the next recording's start position
-- and runs it: the first R the first recording of ORDER, the second R the second, and so on, then round again. Until the last row he reads those inputs in place of the keyboard, with the camera heading phi (on older recordings fitted by the
-- tool, a hypothesis) and his heading starting at heading0, and every tick prints one tab separated line starting with REPLAY (stdout) for tools/compareReplay.py: REPLAY, name, tick, t, x, y, z, vx, vy, vz, motion, heading (the control heading the turn acts on), faceHeading (the smoothed model facing), turnSpeed (the speed turnRate read, 0 with no stick), turnRate (per second), grounded, the ground normal (3), touchingWall, the wall normal (3), the speed word, the growth cosine and the growth target (the last two 0 when no growth ran), the speed entering the ground step (after the contact projection), the speed leaving it (before the press) and the ground normal the step used (3; 0 off the ground step, stale values are possible on air ticks), then the velocity at the start of the tick, as the engine step left it (3). Lua has no file output, so
-- redirect the app's stdout to a file. Set ShadowReplay.enabled to false to get R back as a plain respawn.
-- Hypothesis: the port's world is the original's turned by RenderWareYawDegrees (90) about +Y, which takes (x, z) to (z, -x) and an angle h (forward = (cos h, sin h) over x and z) to h - pi/2; the position and the angles are turned so, and the height is taken as is.

ShadowReplays = ShadowReplays or {}
ShadowReplay = {enabled = false}

local ORDER = {
	"live_uphill_stop",
	"live_uphill_jump",
	"live_uphill_walk",
	"live_fence_headon",
	"live_fence_glance",
	"live_fence_slow",
	"live_slopes"}
local END_HOLD = 0.3
-- A row counts as reached from half a tick before its stamp, i.e. it applies to the tick nearest its stamp (the recording has about one row per game frame, and a row's state is that frame's result). Hypothesis, fitted to live_skid (rows 6.5670 and 9.1389, 0.3 and 5.6 ms after a tick, both belong to that tick); the 1 ms tried first missed the second.
local ROW_TOLERANCE_TICKS = 0.5
local WORLD_TURN = -math.pi / 2
-- Observed (live_run, live_accel comparisons): the port's ground under the start point lies 5.02 below the recorded height (hypothesis: ours is the capsule's center, the original's the feet, or the level's collision differs), whatever the spawn height is; a lift of +5.0 only made him fall 10 and start moving
-- 0.1 s late. So he is spawned 5.0 below the recorded height, which puts him on the ground.
local START_LIFT = -5.0
local current = nil
local nextIndex = 1

-- Puts the player at the start of the next recording and begins it. Called by main.lua after the respawn.
function ShadowReplay.start(entity)
	local name = ORDER[nextIndex]
	nextIndex = nextIndex % #ORDER + 1
	local data = ShadowReplays[name]
	if data == nil then error("no replay '" .. name .. "'") end
	local state = sf.state(entity)
	sf.setPosition(entity, data.start[3], data.start[2] + START_LIFT, -data.start[1])
	-- A recording that began already moving carries its first row's velocity (original axes, turned like the position); else he starts at rest.
	local v = data.velocity
	if v then sf.setVelocity(entity, v[3], v[2], -v[1]) else sf.setVelocity(entity, 0, 0, 0) end
	state.heading = data.heading0 + WORLD_TURN
	state.speed, state.jumped, state.dashed, state.dashTime, state.holdTime, state.spaceHeld = nil, false, false, nil, 0, false
	state.lastVx, state.lastVy, state.lastVz = 0, 0, 0
	state.airLanding = false
	current = {name = name, data = data, tick = 0, index = 1}
	print(string.format("REPLAY\tbegin\t%s\ttickTime %.6f", name, sf.tickTime()))
end

-- Called once a tick by the player before it reads its input. Returns nil when no replay runs, else the input of this tick: wx, wz (the stick in world terms), a (the jump button), phi (the camera's heading) and pitch (the camera direction's vertical part per unit of horizontal length, nil on recordings made before the camera columns).
function ShadowReplay.step()
	if current == nil then return nil end
	local rows = current.data.rows
	local time = current.tick * sf.tickTime()
	if time > rows[#rows][1] + END_HOLD then
		print("REPLAY\tend\t" .. current.name)
		current = nil
		return nil
	end
	-- Observed (live_skid): a row stamped 0.3 ms (6.5670) or 5.6 ms (9.1389) after a tick time missed that tick and was read one tick late, so the port gained one more tick of acceleration before the skid and every later event sat one tick (-2.9 planar) behind. The stamps are the logger's sample times, not tick times: see ROW_TOLERANCE_TICKS.
	while rows[current.index + 1] ~= nil and rows[current.index + 1][1] <= time + ROW_TOLERANCE_TICKS * sf.tickTime() do current.index = current.index + 1 end
	local row = rows[current.index]
	current.tick = current.tick + 1
	current.time = time
	local phi = (row[5] or current.data.phi) + WORLD_TURN
	local fx, fz = math.cos(phi), math.sin(phi)
	local rx, rz = -fz, fx
	return {wx = fx * row[2] + rx * row[3], wz = fz * row[2] + rz * row[3], a = row[4] == 1, phi = phi, pitch = row[6]}
end

-- Live logging: while no recording runs, every tick of the player's own play is kept as a LIVE line in a ring of the last LIVE_TICKS ticks (10 s), and holding L once prints them (stdout, between LIVE begin and LIVE end lines; one dump per press). A line: LIVE, tick, time, x, y, z, vx, vy, vz (as the tick left them), motion, heading, grounded, ground normal (3), touchingWall, speed word, the input of the tick (world stick wx, wz, jump held 1/0, camera forward fx, fz) and the velocity at the start of the tick (3), the model's facing, the reverse-walk flag 1/0 and the animation description (stage, fade, layers with time and weight). Ticks count from the first live tick since the last replay.
local LIVE_TICKS = 600
local liveRing = {}
local liveTick = 0
local liveLHeld = false

local function liveLog(entity, motion, wx, wz, jump, fx, fz)
	local x, y, z = sf.position(entity)
	local vx, vy, vz = sf.velocity(entity)
	local state = sf.state(entity)
	local gnx, gny, gnz = sf.groundNormal(entity)
	liveRing[liveTick % LIVE_TICKS + 1] = string.format("LIVE\t%d\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%s\t%.5f\t%d\t%.4f\t%.4f\t%.4f\t%d\t%.3f\t%.4f\t%.4f\t%d\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%d\t%s", liveTick, liveTick * sf.tickTime(), x, y, z, vx, vy, vz, motion, state.heading or 0, sf.grounded(entity) and 1 or 0, gnx, gny, gnz, sf.touchingWall(entity) and 1 or 0, state.speed or 0, wx, wz, jump and 1 or 0, fx, fz, state.logV0x or 0, state.logV0y or 0, state.logV0z or 0, state.faceHeading or 0, state.animReverse and 1 or 0, state.anim and state.anim.description or "-")
	liveTick = liveTick + 1
	local held = not sf.debugMode() and sf.key("L")
	if held and not liveLHeld then
		print("LIVE\tbegin\ttickTime " .. string.format("%.6f", sf.tickTime()))
		for i = math.max(liveTick - LIVE_TICKS, 0), liveTick - 1 do print(liveRing[i % LIVE_TICKS + 1]) end
		print("LIVE\tend")
	end
	liveLHeld = held
end

function ShadowReplay.log(entity, motion, wx, wz, jump, fx, fz)
	if current == nil then liveLog(entity, motion, wx, wz, jump, fx, fz) return end
	local x, y, z = sf.position(entity)
	local vx, vy, vz = sf.velocity(entity)
	local state = sf.state(entity)
	local gnx, gny, gnz = sf.groundNormal(entity)
	local wnx, wny, wnz = sf.wallNormal(entity)
	print(string.format("REPLAY\t%s\t%d\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%s\t%.5f\t%.5f\t%.3f\t%.3f\t%d\t%.4f\t%.4f\t%.4f\t%d\t%.4f\t%.4f\t%.4f\t%.3f\t%.5f\t%.3f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f", current.name, current.tick - 1, current.time, x, y, z, vx, vy, vz, motion, state.heading or 0, state.faceHeading or 0, state.turnSpeed or 0, state.turnRate or 0, sf.grounded(entity) and 1 or 0, gnx, gny, gnz, sf.touchingWall(entity) and 1 or 0, wnx, wny, wnz, state.speed or 0, state.logCosine or 0, state.logTarget or 0, state.logIn or 0, state.logOut or 0, state.logNx or 0, state.logNy or 0, state.logNz or 0, state.logV0x or 0, state.logV0y or 0, state.logV0z or 0))
end
