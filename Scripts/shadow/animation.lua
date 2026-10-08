-- Shadow's animation, as the original does it (measured on the running game with tools/shadowLiveLog.py; docs/SHADOW_PLAYER.md, "Animation, measured and decoded"). The engine only samples motions at the frames given and mixes them
-- by weight (sf.pose); the timing, the pairs, the fades are here. What is observed and what is not is marked.
--
-- The original's animation library has two channels: a main motion A and a second motion B mixed over it by a weight w; a motion plays in the frame range its data gives (sf.motionRange: start, end), its time advances by a time scale
-- every game frame (60 a second: the same unit as here, one tick) and loops back to start at end (a state that does not loop holds the end). Observed time scales (exact on every row of the recording): with lengths la = end - start of A
-- and lb of B, B's scale is 1 / ((la / lb - 1) x (1 - w) + 1) and A's is that times la / lb, so that the two cycles stay in step.
-- Changing the state (what the game calls the animation index: 0 Idle, 1 Ground, 3 Fall, 4 Landing, 5 and 12 the jump) fades the new motions in: their weight rises linearly from 0 to 1 over a number of frames given when the state starts (observed:
-- 15 entering Ground, 5 entering Idle and Landing, 10 entering Fall, 2 entering the jump, 0 between the jump's own stages) while the old ones go on playing underneath (hypothesis: the way the old ones fade out; their weights are the rest).
-- The Ground state mixes two motions picked by the rate (observed): the whole part of the rate is the pair, its fraction the weight of the second: idle-walk, walk-run, run-slow skate, slow skate-fast skate; the rate is the speed over 12.5
-- below 12.5, 1 + (speed - 12.5) / 17.5 to 30, 2 + (speed - 30) / 70 to 100, 3 + (speed - 100) / 620 to 720, then 4 (decoded and confirmed).
-- Moving backwards (observed by the user, original, not measured: the log shows no negative time scale on the one slide recorded): the walking motions play in reverse when the speed is negative. Here 'negative' is a hypothesis:
-- the planar velocity against the model's facing (player.lua passes `reverse`); the rate still comes from the speed's length.
-- The jump (observed): jump_a and jump_fa (the one for a jump at speed) together at weights 1 - m and m, m = the planar speed at the jump over 700 (clamped to 1), until jump_a's time reaches its end; then jump_b and jump_fb the same way, from their
-- first frames, until jump_b's time reaches its end; then jump_c alone, looping in its range (20 to 30), while he rises; the Fall state plays jump_d looping (53 to 68); the Landing state plays jump_e (68 to 102) once, then the state ends.

ShadowAnimation = {}

local BLEND_FRAMES = {idle = 5, ground = 15, jump = 2, fall = 10, landing = 5, brake = 5, dash = 5}   -- observed (dash: the JumpDash state's PlayMotion(5, 6), decoded and seen in the log), except brake (placeholder)
local BANDS = {"idle", "walk", "run", "skate", "highSkate"}
local REVERSE_TICKS = 8                                                                    -- placeholder: ticks the walk takes to swing from forward to reverse (a one-tick flip popped the legs at the slide start)
local WEIGHT_STEP = 0.25                                                                        -- placeholder: most a Ground layer's weight moves in a tick (a pair swap popped the whole pose in one tick)
local JUMP_SPEED_FULL = 700                                                                    -- observed: the weight of the speed jump's motions is the planar speed over this

local ranges = {}

local function rangeOf(entity, name)
	local found = ranges[name]
	if found == nil then
		local first, last = sf.motionRange(entity, name)
		found = {first, last}
		ranges[name] = found
	end
	return found[1], found[2]
end

-- The rate that picks the pair and the weight in the Ground state, from the speed (decoded, confirmed by the log).
local function groundRate(speed)
	if speed < 12.5 then return speed / 12.5 end
	if speed < 30 then return 1 + (speed - 12.5) / 17.5 end
	if speed < 100 then return 2 + (speed - 30) / 70 end
	if speed < 720 then return 3 + (speed - 100) / 620 end
	return 4
end

-- Time scales of a pair (observed formula, see the top): returns A's and B's.
local function pairScales(entity, a, b, w)
	local aStart, aEnd = rangeOf(entity, a)
	local bStart, bEnd = rangeOf(entity, b)
	local ratio = (aEnd - aStart) / (bEnd - bStart)
	local scaleB = 1 / ((ratio - 1) * (1 - w) + 1)
	return scaleB * ratio, scaleB
end

-- The motions a state wants: a list of {name, weight, scale, loop}, the weights adding up to 1.
local function wanted(entity, anim, kind, context)
	if kind == "idle" then
		return {{"idle", 1, 1, true}}
	elseif kind == "ground" then
		local rate = groundRate(context.speed)
		local band = math.min(math.floor(rate), 3)
		local w = rate >= 4 and 1 or rate - band
		local a, b = BANDS[band + 1], BANDS[band + 2]
		local scaleA, scaleB = pairScales(entity, a, b, w)
		local direction = anim.direction
		return {{a, 1 - w, scaleA * direction, true}, {b, w, scaleB * direction, true}}
	elseif kind == "jump" then
		local m = math.min(1, anim.jumpPlanar / JUMP_SPEED_FULL)
		if anim.stage == 1 then
			local scaleA, scaleB = pairScales(entity, "jump", "jumpFA", m)
			return {{"jump", 1 - m, scaleA, false}, {"jumpFA", m, scaleB, false}}
		elseif anim.stage == 2 then
			local scaleA, scaleB = pairScales(entity, "jumpB", "jumpFB", m)
			return {{"jumpB", 1 - m, scaleA, false}, {"jumpFB", m, scaleB, false}}
		end
		return {{"jumpC", 1, 1, true}}
	elseif kind == "dash" then
		return {{"dash", 1, 1, true}}
	elseif kind == "fall" then
		return {{"jumpD", 1, 1, true}}
	elseif kind == "landing" then
		return {{"jumpE", 1, 1, false}}
	elseif kind == "brake" then
		return {{"brake", 1, 1, false}}
	end
	return {{"idle", 1, 1, true}}
end

-- Advances the times of a list of entries by one tick (times kept by name in `times`); returns true when the first entry, not looping, has reached its end.
local function advance(entity, entries, times, frames)
	local finished = false
	-- A motion that enters a pair starts at the phase of the one already playing (observed: when the Ground blend moved from run and slow skate to slow skate and fast skate, the fast skate came in at 20.136 of 45 while the slow skate
	-- was at 26.848 of 60, both 0.4475 of their cycles; the same at every earlier switch), so the two cycles stay in step; with none playing it starts at its first frame.
	local phase
	for _, entry in ipairs(entries) do
		local known = times[entry[1]]
		if known ~= nil then
			local first, last = rangeOf(entity, entry[1])
			phase = (known - first) / (last - first)
			break
		end
	end
	for index, entry in ipairs(entries) do
		local name = entry[1]
		local first, last = rangeOf(entity, name)
		local t = times[name]
		if t == nil then t = phase ~= nil and first + phase * (last - first) or first end
		t = t + entry[3] * frames
		if t < first then
			if entry[4] then t = last - (first - t) % (last - first) else t = first end
		elseif t >= last then
			if entry[4] then t = first + (t - first) % (last - first)
			else
				t = last
				if index == 1 then finished = true end
			end
		end
		times[name] = t
	end
	-- A motion that left the pair forgets its time, so it enters the next pair at the phase of the one playing instead of where it was left (a stale time put highSkate half a cycle out of step with skate in a recorded leg log).
	local inPair = {}
	for _, entry in ipairs(entries) do inPair[entry[1]] = true end
	for name in pairs(times) do
		if not inPair[name] then times[name] = nil end
	end
	return finished
end

-- Ground layers' shown weights: each moves toward its wanted weight by at most WEIGHT_STEP a frame, so a pair swap (walk and run to idle and walk) crosses over a few ticks instead of one (hypothesis for the look: the user saw a one-frame pop in the port; the original's own blend weights are not decoded).
-- A layer that left the pair keeps playing (its own time, scale and loop) until its weight reaches 0. Shown weights are renormalized to add up to 1. anim.mix is kept by name: {w, t, entry}; the first Ground tick takes the wanted weights as they are.
local function mixGround(entity, anim, entries, frames)
	local fresh = anim.mix == nil
	local mix = anim.mix or {}
	anim.mix = mix
	local wantedBy = {}
	for _, entry in ipairs(entries) do
		wantedBy[entry[1]] = entry
		local m = mix[entry[1]]
		if m == nil then m = {w = fresh and entry[2] or 0}; mix[entry[1]] = m end
		m.entry = entry
		m.t = anim.times[entry[1]]
	end
	local layers, sum = {}, 0
	for name, m in pairs(mix) do
		local goal = wantedBy[name] and wantedBy[name][2] or 0
		m.w = m.w + math.max(-WEIGHT_STEP * frames, math.min(WEIGHT_STEP * frames, goal - m.w))
		if not wantedBy[name] then
			local first, last = rangeOf(entity, name)
			local t = m.t + m.entry[3] * frames
			if t < first then t = last - (first - t) % (last - first) elseif t >= last then t = first + (t - first) % (last - first) end
			m.t = t
		end
		if m.w <= 0 and not wantedBy[name] then mix[name] = nil else sum = sum + m.w end
	end
	for name, m in pairs(mix) do layers[#layers + 1] = {name, m.t, m.w / sum} end
	table.sort(layers, function(a, b) return a[3] > b[3] end)
	return layers
end

-- state.anim keeps: mix (the Ground layers' shown weights), direction (-1 to 1, the Ground pair's time direction), kind, stage, times (by motion name), fade {frames, elapsed, old = entries, oldTimes}, jumpPlanar, landingDone.
-- context: speed (the length of his velocity), reverse (the Ground pair's time runs backward), jumpPlanar (the planar speed when he jumped), kind: "idle" | "ground" | "jump" | "fall" | "landing" | "brake".
function ShadowAnimation.update(entity, state, kind, context)
	local anim = state.anim
	if anim == nil then
		anim = {kind = kind, stage = 1, times = {}, jumpPlanar = 0}
		state.anim = anim
	end
	local frames = sf.tickTime() * 60
	if kind ~= anim.kind then
		-- A new state: the motions it wants fade in over the frames it was started with, the old ones go on under them.
		anim.fade = {frames = BLEND_FRAMES[kind] or 5, elapsed = 0, old = anim.entries, oldTimes = anim.times}
		anim.times = {}
		anim.mix = nil
		anim.kind = kind
		anim.stage = 1
		anim.landingDone = false
		anim.jumpPlanar = context.jumpPlanar or 0
	end

	-- The Ground pair's time direction moves toward +1 (forward) or -1 (reverse) by 2 / REVERSE_TICKS a tick, so it slows through standstill instead of flipping (hypothesis for the look; the original's log has no reverse).
	anim.direction = anim.direction or 1
	local target = context.reverse and -1 or 1
	anim.direction = anim.direction + math.max(-2 / REVERSE_TICKS, math.min(2 / REVERSE_TICKS, target - anim.direction)) * frames

	local entries = wanted(entity, anim, kind, context)
	local finished = advance(entity, entries, anim.times, frames)
	if kind == "jump" and finished then
		if anim.stage == 1 then
			anim.stage = 2
			anim.times = {}
		elseif anim.stage == 2 then
			anim.stage = 3
			anim.times = {}
		end
		entries = wanted(entity, anim, kind, context)
		advance(entity, entries, anim.times, 0)
	elseif kind == "landing" and finished then
		anim.landingDone = true
	end
	anim.entries = entries

	local layers = {}
	local fade = anim.fade
	local f = 1
	if fade ~= nil then
		fade.elapsed = fade.elapsed + frames
		f = math.min(1, fade.elapsed / fade.frames)
		if f >= 1 then anim.fade, fade = nil, nil end
	end
	if fade ~= nil and fade.old ~= nil then
		advance(entity, fade.old, fade.oldTimes, frames)
		for _, entry in ipairs(fade.old) do
			layers[#layers + 1] = {entry[1], fade.oldTimes[entry[1]], entry[2] * (1 - f)}
		end
	end
	-- The Ground weight limit (mixGround) is switched off at the user's request (2026-10-07); remove the -- marks to turn it back on.
	-- if kind == "ground" then
	-- 	for _, layer in ipairs(mixGround(entity, anim, entries, frames)) do
	-- 		layers[#layers + 1] = {layer[1], layer[2], layer[3] * f}
	-- 	end
	-- else
		for _, entry in ipairs(entries) do
			layers[#layers + 1] = {entry[1], anim.times[entry[1]], entry[2] * f}
		end
	-- end
	sf.pose(entity, layers)
	local parts = {}
	for _, layer in ipairs(layers) do parts[#parts + 1] = string.format("%s@%.2f w=%.3f", layer[1], layer[2], layer[3]) end
	anim.description = string.format("stage=%d fade=%s [%s]", anim.stage, fade and string.format("%.2f", f) or "-", table.concat(parts, "  "))
	return anim.landingDone
end
