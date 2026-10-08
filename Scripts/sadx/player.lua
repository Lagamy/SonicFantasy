-- Sonic. Outside the debug mode (sf.debugMode, editor mode; Play button) W A S D steer him relative to where the camera looks, Space jumps (and homes in the air), CTRL spin dashes; in the debug mode those keys fly the camera and
-- he ignores them. Only the active player (state.active, main.lua) reads the keys. The engine's one physics system does the moving and the colliding (and the gravity): this only sets his velocity from what it reports of the last move (sf.grounded, sf.groundNormal).
-- state.lock = ticks the control is locked for (a launch), state.invulnerable = ticks left (objects.lua), state.heading = where he faces (radians, forward = (cos, sin) over x and z, like the game's BAMS heading).
-- External (_Main.rel, the per-character record of Sonic at 0x2C61E8 and the shared movement code, see the notes in the plan of 2026-10-05; the game's values are per frame, converted with 1 frame = 1 tick = 1/60 s,
-- a hypothesis): the ground and air rules below. Marked "hypothesis" or "placeholder" where they are not decoded. Animation (external, FUN_805a99f0 of the decompile): on the ground the motion is picked by forward speed (the game also adds 5 times an acceleration term of its motion struct, zero here, and scales the
-- playback rate by the speed, not ported). Placeholders / hypotheses: Brake for a hard turn on the ground, Landing for a few ticks, Damage while state.damaged, Spring Fall after a spring; the jump uses the curled model and the spin dash and
-- homing the jumpball model (external, the game's animation table pairs each motion with a model); the jump and the spin dash charge blink between the two (see "Ball blink"; the roll after a release is not ported). Not ported: slope friction (+0x60 to +0x68) and the slope slide, the surface multiplier,
-- the ball drag of the roll, the turn-around of a standing start on the ground beyond snapping the heading.

local FRAMES_PER_SECOND = 60
local JUMP_SPEED = 1.66 * FRAMES_PER_SECOND             -- external
local GROUND_ACCEL = 0.05                               -- external, units per frame squared (+0x3c)
local GROUND_ACCEL_FAST_FACTOR = 0.4                    -- external: from FUN_804e4e00, the acceleration at a forward speed of FAST_SPEED or more (with the ground normal's y >= 0)
local FAST_SPEED = 3.0                                  -- external (+0x10): above it the forward drag acts on the excess only
local AIR_ACCEL = 0.031                                 -- external (+0x40), doubled while falling (FUN_804e38a8)
local AIR_BRAKE = -0.17                                 -- external (+0x4c): the air acceleration when the stick is more than 135 degrees from his heading and forward speed > RUN_SPEED
local RUN_SPEED = 1.39                                  -- external (+0x28)
local GROUND_DECEL = 0.06                               -- external (+0x44): towards zero with no stick, when the ground normal's y >= 0.71
local DRAG_FORWARD_GROUND = -0.008                      -- external (+0x54)
local DRAG_FORWARD_AIR = -0.028                         -- external (+0x50)
local DRAG_UP = -0.01                                   -- external (+0x58)
local DRAG_SIDE = -0.4                                  -- external (+0x5c)
local SPEED_CAP = 16 * FRAMES_PER_SECOND                -- external, units per second on each world axis
local JUMP_HOLD_TICKS = 60                              -- external (+0x00): the most frames a held button extends the jump
local JUMP_HOLD_ACCEL = 0.8 * 0.076                     -- external (0.8 times +0x38), units per frame squared, added to the vertical speed in the jump
local GROUND_GRIP = 0.99                                -- external (FUN_804dd3f8): the share of his old local speed kept when he turns on the ground with the stick pushed
local AIR_GRIP = 0.1                                    -- external: the same in the air
local TURN_SLOW_ANGLE = 0x1000 / 0x10000 * 2 * math.pi  -- external: below it the heading moves by a eighth of the difference a frame, up to TURN_MID_ANGLE by a quarter, else by TURN_MAX_STEP
local TURN_MID_ANGLE = 0x2000 / 0x10000 * 2 * math.pi
local TURN_MAX_STEP = 0x800 / 0x10000 * 2 * math.pi
local BRAKE_ANGLE = 0x6000 / 0x10000 * 2 * math.pi
local SPIN_DASH_MIN_CHARGE = 2.0                        -- external, units per frame
local SPIN_DASH_CHARGE_STEP = 0.4                       -- external, units per frame, per tick
local SPIN_DASH_MAX_CHARGE = 10.0                       -- external, units per frame
local HOMING_MIN_SPEED = 2.0                            -- external, units per frame
local HOMING_MAX_SPEED = 8.0                            -- external, units per frame
local HOMING_CONE = 0x3000 / 0x10000 * 2 * math.pi      -- external, radians around his heading
local HOMING_TURN = 0x1800 / 0x10000 * 2 * math.pi      -- external: the most his heading turns towards the target a frame
local HOMING_RANGE = 100                                -- external (FUN_8041e060): a target is listed when the squared distance is under 10000; the game also needs its collision sensor to overlap the target's, which is not ported
local HOMING_COAST_TICKS = 10                           -- hypothesis: the game sets a counter (work+0x80) to 10 when it starts the attack; how it is counted down is not decoded. The stick does not steer during the coast.
local AIR_ANIMATION_TICKS = 8                           -- placeholder: ticks without ground contact before the air motions play (the engine's ground contact flickers on uneven terrain)
local CONTACT_WINDOW = 8                                -- placeholder: see LAND_CONTACTS
local LAND_CONTACTS = 4                                 -- placeholder: ticks with ground contact among the last CONTACT_WINDOW before the air motions give way to the ground ones
local HOMING_MAX_TICKS = 30                             -- placeholder
local WALK_SPEED = 0.46                                 -- external (+0x24): the speeds (per frame) up to which the game plays Walk, Speedwalk (up to +0x28), Jog (+0x2c) and Run (+0x34); Sprint above
local SPEEDWALK_SPEED = 1.39
local JOG_SPEED = 2.3
local RUN_MAX_SPEED = 5.09
-- Playback rate. External (FUN_804cf4f4 and the animation table at _Main.rel 0x3544E0, confirmed on a RAM dump of the running game): each game frame the walking motions advance by the forward speed (per frame)
-- times a per-motion value; Run and Jog use work[0], the forward speed plus 20 times an acceleration term (zero here), Sprint the forward speed.
local RATES = {walk = 3.4, speedwalk = 1.4, jog = 0.8, run = 0.5, sprint = 0.5}
local BALL_BLINK_TICKS = 4                              -- hypothesis, tuned from your report that 1 was far too fast: ticks per step of the game's blink counter
local LANDING_TICKS = 12                                -- placeholder: how long the landing motion plays

-- value moved toward target by at most step.
local function approach(value, target, step)
	if value < target then return math.min(value + step, target) end
	return math.max(value - step, target)
end

-- An angle in (-pi, pi].
local function wrapAngle(angle)
	return angle - 2 * math.pi * math.floor((angle + math.pi) / (2 * math.pi))
end

local function clamp(value, limit)
	return math.max(-limit, math.min(limit, value))
end

local clampSigned = clamp

-- The nearest registered target (objects.lua) within HOMING_RANGE and the cone around the horizontal direction (dx, dz); returns its handle, or nil.
local function findHomingTarget(entity, dx, dz)
	local px, py, pz = sf.position(entity)
	local best, bestDistance
	for target in pairs(homingTargets) do
		if not sf.exists(target) then
			homingTargets[target] = nil
		else
			local tx, ty, tz = sf.position(target)
			local ux, uy, uz = tx - px, ty - py, tz - pz
			local distance = math.sqrt(ux * ux + uy * uy + uz * uz)
			if distance > 1e-4 and distance <= HOMING_RANGE and (bestDistance == nil or distance < bestDistance)
				and math.acos(math.max(-1, math.min(1, (ux * dx + uz * dz) / distance))) <= HOMING_CONE then
				best, bestDistance = target, distance
			end
		end
	end
	return best
end

-- Turns his model to the heading. External (FUN_805a9f90, his draw): the model is drawn turned about Y by 0x8000 - heading, the heading in the game's convention (forward = (cos, sin) over x and z); the matrix is the
-- SDK's right-handed one (hypothesis, as glm's), and the model's own front is taken to be the game's (hypothesis: if he faces backwards or sideways, the offset is here).
local MODEL_YAW_OFFSET = math.pi
local function face(entity, heading)
	sf.turnY(entity, MODEL_YAW_OFFSET - heading)
end

sf.behavior("Player", function(entity)
	local state = sf.state(entity)
	state.lock = state.lock or 0
	state.invulnerable = state.invulnerable or 0
	if state.invulnerable > 0 then state.invulnerable = state.invulnerable - 1 end
	if state.lock > 0 then
		state.lock = state.lock - 1
		local _, lockedVy = sf.velocity(entity)
		sf.blink(entity, false)
		if sf.grounded(entity) then sf.animate(entity, "sprint")
		else sf.animate(entity, lockedVy > 0 and "springFall" or "fall") end
		return
	end

	-- The camera's heading over the ground, and the way to its right.
	local fx, _, fz = sf.cameraForward()
	local length = math.sqrt(fx * fx + fz * fz)
	if length > 1e-4 then fx, fz = fx / length, fz / length else fx, fz = 0, -1 end
	local rx, rz = -fz, fx
	if state.heading == nil then state.heading = math.atan(fz, fx) end

	local controlled = state.active and not sf.debugMode()
	local wx, wz = 0, 0
	if controlled and sf.key("W") then wx, wz = wx + fx, wz + fz end
	if controlled and sf.key("S") then wx, wz = wx - fx, wz - fz end
	if controlled and sf.key("D") then wx, wz = wx + rx, wz + rz end
	if controlled and sf.key("A") then wx, wz = wx - rx, wz - rz end
	local wishLength = math.sqrt(wx * wx + wz * wz)
	local hasWish = wishLength > 1e-4
	local stickAngle = hasWish and math.atan(wz, wx) or nil

	local frames = sf.tickTime() * FRAMES_PER_SECOND
	local vx, vy, vz = sf.velocity(entity)
	local grounded = sf.grounded(entity)

	-- Homing attack (SPACE pressed in the air, once until he lands). External (state 6 of the decompile): the nearest target in a 67.5 degree cone around his heading, speed = clamp(distance, 2, 8) per frame
	-- straight at it, his heading turning towards it by at most 33.75 degrees a frame; without a target he lunges forward at 8 for a few ticks. It ends when the target is gone (the enemy's own rule destroys
	-- it and bounces him), on landing, or after HOMING_MAX_TICKS. The stick steers the heading first (below), so the direction of the lunge is the game's: the heading the stick has turned.
	local pressed = controlled and sf.key("SPACE")
	local justPressed = pressed and not state.spaceHeld
	state.spaceHeld = pressed
	if grounded then state.homed, state.homing = false, nil end

	-- Steering. External (FUN_804dd3f8, FUN_804e4e00, FUN_804e38a8): the heading moves towards the stick by an eighth of the difference a frame below 22.5 degrees, a quarter below 45, else by 11.25 degrees; on the
	-- ground standing still it snaps. The velocity is then re-expressed along the new heading, keeping GROUND_GRIP (on the ground with the stick pushed) or AIR_GRIP of its old local part and the rest in the world.
	local heading = state.heading
	local turnAway = false
	if hasWish and state.homing == nil then
		local difference = wrapAngle(stickAngle - heading)
		local forwardSpeed = (vx * math.cos(heading) + vz * math.sin(heading)) / FRAMES_PER_SECOND
		turnAway = math.abs(difference) > BRAKE_ANGLE and forwardSpeed > RUN_SPEED
		local newHeading = heading
		if grounded and math.abs(forwardSpeed) < 1e-3 then
			newHeading = stickAngle
		elseif not turnAway then
			local size = math.abs(difference)
			local step = size < TURN_SLOW_ANGLE and size / 8 or size < TURN_MID_ANGLE and size / 4 or TURN_MAX_STEP
			newHeading = heading + (difference >= 0 and 1 or -1) * math.min(step * frames, size)
		end
		if newHeading ~= heading then
			local oldForward = vx * math.cos(heading) + vz * math.sin(heading)
			local oldSide = -vx * math.sin(heading) + vz * math.cos(heading)
			local newForward = vx * math.cos(newHeading) + vz * math.sin(newHeading)
			local newSide = -vx * math.sin(newHeading) + vz * math.cos(newHeading)
			local grip = grounded and GROUND_GRIP or AIR_GRIP
			local forward = grip * oldForward + (1 - grip) * newForward
			local side = grip * oldSide + (1 - grip) * newSide
			vx = forward * math.cos(newHeading) - side * math.sin(newHeading)
			vz = forward * math.sin(newHeading) + side * math.cos(newHeading)
			heading = newHeading
		end
	end
	state.heading = heading
	local hx, hz = math.cos(heading), math.sin(heading)

	if justPressed and not grounded and not state.homed then
		state.homed = true
		local target = findHomingTarget(entity, hx, hz)
		state.homing = {target = target, ticks = 0}
	end
	local homing = state.homing
	if homing ~= nil then
		homing.ticks = homing.ticks + 1
		if homing.target ~= nil and (not sf.exists(homing.target) or homing.ticks > HOMING_MAX_TICKS) then
			-- The target is gone (the enemy's own rule destroyed it) or took too long: he keeps the speed he has, like the game when its target list empties.
			homing.target = nil
			homing.coastX, homing.coastZ = vx, vz
		end
		if homing.target ~= nil then
			local px, py, pz = sf.position(entity)
			local tx, ty, tz = sf.position(homing.target)
			local ux, uy, uz = tx - px, ty - py, tz - pz
			local distance = math.sqrt(ux * ux + uy * uy + uz * uz)
			if distance > 1e-4 then
				local speed = math.max(HOMING_MIN_SPEED, math.min(HOMING_MAX_SPEED, distance)) * FRAMES_PER_SECOND
				if ux * ux + uz * uz > 1e-8 then
					local toTarget = wrapAngle(math.atan(uz, ux) - heading)
					state.heading = heading + clampSigned(toTarget, HOMING_TURN * frames)
				end
				sf.blink(entity, false)
				sf.animate(entity, "ball")
				face(entity, state.heading)
				sf.setVelocity(entity, ux / distance * speed, uy / distance * speed, uz / distance * speed)
				return
			end
		else
			-- No target. External (state 6, FUN_805a76c0 with an empty list): he keeps rolling at the 8 per frame he started with under the ball physics, gravity and air drag included, until he touches the ground
			-- or a wall. The horizontal speed is carried here; the vertical speed stays the physics' own.
			if homing.coastX == nil then
				homing.coastX, homing.coastZ = hx * HOMING_MAX_SPEED * FRAMES_PER_SECOND, hz * HOMING_MAX_SPEED * FRAMES_PER_SECOND
			end
			if homing.ticks <= HOMING_COAST_TICKS and not sf.touchingWall(entity) then
				homing.coastX = homing.coastX * (1 + DRAG_FORWARD_GROUND) ^ frames
				homing.coastZ = homing.coastZ * (1 + DRAG_FORWARD_GROUND) ^ frames
				sf.blink(entity, false)
				sf.animate(entity, "ball")
				face(entity, state.heading)
				sf.setVelocity(entity, homing.coastX, vy, homing.coastZ)
				return
			end
		end
		state.homing = nil
	end

	-- Spin dash (CTRL, on the ground). Held: the charge starts at max(2.0, forward speed) per frame and grows by 0.4 a frame up to 10.0 (external, states 4 and 5 of the decompile), the stick only steers the
	-- heading and the deceleration below slows him. Released: his forward speed becomes the charge along the heading. Leaving the ground cancels it. The roll's reduced drag afterwards is not ported.
	local forward = vx * hx + vz * hz
	local side = -vx * hz + vz * hx
	forward, side = forward / FRAMES_PER_SECOND, side / FRAMES_PER_SECOND
	local charging = controlled and grounded and sf.key("CTRL")
	if charging and state.charge == nil then state.ballTicks = 0 end
	if charging then
		state.charge = math.min((state.charge or math.max(SPIN_DASH_MIN_CHARGE, forward)) + SPIN_DASH_CHARGE_STEP * frames, SPIN_DASH_MAX_CHARGE)
	elseif state.charge ~= nil then
		if controlled and grounded then forward, side = state.charge, 0 end
		state.charge = nil
	end

	-- Forward speed, per frame. External (FUN_804e4e00, FUN_804e38a8, FUN_804dc9ec). Stick pushed: on the ground ground acceleration (0.4 of it from FAST_SPEED up), in the air air acceleration (doubled while
	-- falling, AIR_BRAKE when the stick points the other way while running); the digital stick is always at full strength. No stick: on the ground GROUND_DECEL towards zero (if the ground is flat enough) and the full
	-- forward drag above RUN_SPEED. Forward drag: on the ground with the stick only the excess over FAST_SPEED (or the whole of a backwards speed); in the air always. Side speed dies at DRAG_SIDE, vertical speed
	-- loses DRAG_UP of itself in the air. Hypothesis: on the ground a stick pointing the other way while running counts as no stick (he brakes, then turns), since the game's ground turn-around is not decoded.
	local normalY = 1
	if grounded then
		local _, ny = sf.groundNormal(entity)
		normalY = ny
	end
	if hasWish and not charging and not (grounded and turnAway) then
		if grounded then
			forward = forward + GROUND_ACCEL * (forward >= FAST_SPEED and normalY >= 0 and GROUND_ACCEL_FAST_FACTOR or 1) * frames
		else
			forward = forward + (turnAway and AIR_BRAKE or AIR_ACCEL * (vy < 0 and 2 or 1)) * frames
		end
		if not grounded then
			forward = forward + DRAG_FORWARD_AIR * forward * frames
		elseif forward > FAST_SPEED then
			forward = forward + DRAG_FORWARD_GROUND * (forward - FAST_SPEED) * frames
		elseif forward < 0 then
			forward = forward + DRAG_FORWARD_GROUND * forward * frames
		end
	else
		if grounded and normalY >= 0.71 then
			forward = approach(forward, 0, GROUND_DECEL * frames)
		end
		if math.abs(forward) > RUN_SPEED or not grounded then
			forward = forward + (grounded and DRAG_FORWARD_GROUND or DRAG_FORWARD_AIR) * forward * frames
		end
	end
	side = side + DRAG_SIDE * side * frames
	vx = (forward * hx - side * hz) * FRAMES_PER_SECOND
	vz = (forward * hz + side * hx) * FRAMES_PER_SECOND
	if grounded then
		-- On the ground his velocity is kept in the plane of the surface under him, so a slope is climbed instead of ground into.
		local nx, ny, nz = sf.groundNormal(entity)
		if ny > 1e-3 then
			-- Only when he is not already moving off the surface (a spring's launch arrives while the last move still reports the ground). The engine zeroes the vertical speed on contact, so going downhill the
			-- speed he has (0) is above the plane's, and the test is against 0 as well: without it he left the slope every second tick (observed in a log on the wooden slope: grounded alternated each tick with vy = -4.8).
			local planeVy = -(nx * vx + nz * vz) / ny
			if vy <= math.max(planeVy, 0) + 1 then vy = planeVy end
		end
	else
		vy = vy + DRAG_UP * vy * frames
		-- Jump hold. External (FUN_804e6064, called in the jump state): while the jump button is held and fewer than JUMP_HOLD_TICKS frames have passed since the jump, the vertical speed gets
		-- JUMP_HOLD_ACCEL a frame on top of gravity.
		if state.jumped and pressed and (state.holdTicks or 0) < JUMP_HOLD_TICKS then
			vy = vy + JUMP_HOLD_ACCEL * FRAMES_PER_SECOND * frames
			state.holdTicks = (state.holdTicks or 0) + 1
		end
	end
	if controlled and grounded and sf.key("SPACE") then
		vy = JUMP_SPEED
		state.jumped, state.jumpGrace, state.holdTicks = true, 2, 0
	end

	-- Motion. External (FUN_805a99f0): on the ground by forward speed, Walk to WALK_SPEED, Speedwalk to SPEEDWALK_SPEED, Jog to JOG_SPEED, Run to RUN_MAX_SPEED, Sprint above; standing still is Stand. The rest is
	-- placeholder or hypothesis (see the top): Brake for a hard turn, Landing for LANDING_TICKS after touching the ground, Damage while state.damaged, Spring Fall while rising after a spring, Jump (curled model) after a jump, Ball (jumpball model) charging a spin dash or homing, else Fall.
	-- Ground contact flickers on uneven terrain (observed in a log of a run: on a slope or stairs the body touches the ground every second or third tick). Without any air motion playing, a gap of fewer than AIR_ANIMATION_TICKS ticks
	-- keeps the ground motion; once they play, he counts as landed when at least LAND_CONTACTS of the last CONTACT_WINDOW ticks touched the ground, not when one did (placeholder rule: no flash of the landing motion in the middle of a fall).
	local contacts = state.contacts or {}
	table.insert(contacts, 1, grounded)
	contacts[CONTACT_WINDOW + 1] = nil
	state.contacts = contacts
	local contactCount = 0
	for _, touched in ipairs(contacts) do
		if touched then contactCount = contactCount + 1 end
	end
	local landed = state.wasGrounded == false and contactCount >= LAND_CONTACTS or state.wasGrounded ~= false and grounded
	local airTicks = landed and 0 or (state.airTicks or 0) + 1
	state.airTicks = airTicks
	local animGrounded = landed or (airTicks < AIR_ANIMATION_TICKS and not state.jumped and not state.springed)
	-- The window counts the contacts since the air motions began (observed: left as it was, the ticks of the ground he just left counted as a landing the moment after a jump, and Fall and Landing restarted the jump).
	if state.wasGrounded ~= false and not animGrounded then state.contacts = {} end
	local motion
	local speedForRate = math.abs(forward)
	if (state.damaged or 0) > 0 then
		state.damaged = state.damaged - 1
		motion = "damage"
	elseif animGrounded and (state.jumpGrace or 0) > 0 then
		-- The tick after the jump still reports the ground.
		state.jumpGrace = state.jumpGrace - 1
		motion = "jump"
	elseif animGrounded then
		state.springed, state.jumped = false, false
		if state.wasGrounded == false then state.landing = LANDING_TICKS end
		local speed = math.abs(forward)
		if (state.landing or 0) > 0 then
			state.landing = state.landing - 1
			motion = "landing"
		elseif charging then motion = "jump"
		elseif turnAway then motion = "brake"
		elseif not hasWish and speed < 0.01 then motion = "stand"
		elseif speed <= WALK_SPEED then motion = "walk"
		elseif speed <= SPEEDWALK_SPEED then motion = "speedwalk"
		elseif speed <= JOG_SPEED then motion = "jog"
		elseif speed <= RUN_MAX_SPEED then motion = "run"
		else motion = "sprint" end
	else
		if state.springed and vy <= 0 then state.springed = false end
		if state.springed then state.jumped = false end
		motion = state.springed and "springFall" or state.jumped and "jump" or "fall"
		state.landing = 0
	end
	state.wasGrounded = animGrounded

	-- Ball blink. External (FUN_805a9f90, FUN_805af2b0): while his ball flag is set (the jump and the spin dash charge, whose motion is the curled Jump) a counter (work+0x7e) rises each frame, and on the frames where
	-- counter & 0x11 is not zero his draw swaps the motion for the Ball one (jumpball model) while the Jump one goes on in the background (sf.blink). So in each 32 frames the odd ones of the first 16 and all of the last 16 are the ball. The game zeroes the counter when the spin dash
	-- starts; a jump does not (it carries on from where it was, hypothesis: a stale value after a roll).
	local blink = false
	if motion == "jump" then
		state.ballTicks = state.ballTicks or 0
		state.ballSubTicks = (state.ballSubTicks or 0) + 1
		if state.ballSubTicks >= BALL_BLINK_TICKS then
			state.ballSubTicks = 0
			state.ballTicks = state.ballTicks + 1
		end
		blink = state.ballTicks & 0x11 ~= 0
	end
	sf.blink(entity, blink and "ball" or false)
	sf.animate(entity, motion, RATES[motion] and RATES[motion] * speedForRate or 0)
	face(entity, state.heading)
	sf.setVelocity(entity, clamp(vx, SPEED_CAP), clamp(vy, SPEED_CAP), clamp(vz, SPEED_CAP))
end)

-- Called by main.lua's attachPlayer for the Sonic entity.
-- The player's state table as it starts (also after the engine's reset: resetPlayers, main.lua).
function initSonicState(state)
	state.lock = 0
	state.invulnerable = 0
end

function attachSonicPlayer(entity)
	initSonicState(sf.state(entity))
	sf.give(entity, "Player")
	sf.setUncullable(entity, true)
end
