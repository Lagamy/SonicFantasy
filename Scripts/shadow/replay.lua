-- Replay of a run recorded from the original game, to compare Shadow's port against it (docs/STH2005_PLAYER_REFERENCE.md, section 20). NOT part of the original: a test aid. The recording is a CSV of tools/shadowLiveLog.py made with --pad;
-- tools/makeReplay.py turns it into Scripts/shadow/replays/<name>.lua, which fills ShadowReplays[<name>] with {start = {x, y, z}, heading0, phi, rows = {{t, forward, right, a}, ...}} (t in seconds from the first row, the stick as the
-- camera-relative forward and right parts, a = the jump button, start = where the recording began, all in the original's own axes). While ShadowReplay.enabled, R (main.lua's respawn) also activates Shadow, puts him at the next recording's start position
-- and runs it: the first R the first recording of ORDER, the second R the second, and so on, then round again. Until the last row he reads those inputs in place of the keyboard, with the camera heading phi (the camera is not recorded: phi is fitted by the
-- tool, a hypothesis) and his heading starting at heading0, and every tick prints one tab separated line starting with REPLAY (stdout) for tools/compareReplay.py: REPLAY, name, tick, t, x, y, z, vx, vy, vz, motion. Lua has no file output, so
-- redirect the app's stdout to a file. Set ShadowReplay.enabled to false to get R back as a plain respawn.
-- Hypothesis: the port's world is the original's turned by RenderWareYawDegrees (90) about +Y, which takes (x, z) to (z, -x) and an angle h (forward = (cos h, sin h) over x and z) to h - pi/2; the position and the angles are turned so, and the height is taken as is.

ShadowReplays = ShadowReplays or {}
ShadowReplay = {enabled = true}

local ORDER = {"live_run", "live_accel", "live_half"}
local END_HOLD = 0.3
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
	sf.setVelocity(entity, 0, 0, 0)
	state.heading = data.heading0 + WORLD_TURN
	state.speed, state.jumped, state.dashed, state.dashTime, state.holdTime, state.spaceHeld = nil, false, false, nil, 0, false
	current = {name = name, data = data, tick = 0, index = 1}
	print(string.format("REPLAY\tbegin\t%s\ttickTime %.6f", name, sf.tickTime()))
end

-- Called once a tick by the player before it reads its input. Returns nil when no replay runs, else the input of this tick: wx, wz (the stick in world terms) and a (the jump button).
function ShadowReplay.step()
	if current == nil then return nil end
	local rows = current.data.rows
	local time = current.tick * sf.tickTime()
	if time > rows[#rows][1] + END_HOLD then
		print("REPLAY\tend\t" .. current.name)
		current = nil
		return nil
	end
	while rows[current.index + 1] ~= nil and rows[current.index + 1][1] <= time do current.index = current.index + 1 end
	local row = rows[current.index]
	current.tick = current.tick + 1
	current.time = time
	local phi = current.data.phi + WORLD_TURN
	local fx, fz = math.cos(phi), math.sin(phi)
	local rx, rz = -fz, fx
	return {wx = fx * row[2] + rx * row[3], wz = fz * row[2] + rz * row[3], a = row[4] == 1}
end

function ShadowReplay.log(entity, motion)
	if current == nil then return end
	local x, y, z = sf.position(entity)
	local vx, vy, vz = sf.velocity(entity)
	print(string.format("REPLAY\t%s\t%d\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%s", current.name, current.tick - 1, current.time, x, y, z, vx, vy, vz, motion))
end
