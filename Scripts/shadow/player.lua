-- Shadow as the player (Shadow the Hedgehog 2005). While he is the active player (state.active, set by main.lua; J switches) and outside the debug mode (Tab) W A S D steer him relative to the camera and Space jumps.
-- This sets his velocity the way the game's movement code does: acceleration a and velocity v integrated with a linear drag, v = a/k + (v - a/k) e^(-k dt), k = 20 on the ground,
-- 3 along the ground plane and 1 along the up axis in the air. Evidence and history for every rule: docs/STH2005_PLAYER_REFERENCE.md (REF below), compact twin docs/ai/STH2005_PLAYER.md.
-- Labels: external = Ghidra decompile of main.dol (units per second: hypothesis), observed = live recordings, hypothesis / placeholder where marked. Hypotheses include the cosine, the walk / run split, the motions by name.
-- Not ported: homing attack, spin dash, skate, Chaos Control, weapons, sliding, the standing turn-around.
-- state.heading = where he faces (radians, forward = (cos, sin) over x and z), state.speed = the game's speed word (the acceleration's length), state.holdTime = what is left of the jump's held boost.

local MIN_SPEED = 1000                     -- external (FUN_80082cc8): the acceleration's length at rest
local MAX_SPEED = 15000                    -- external (FUN_80082cc8): x the factor of table index player+0x64 (1.0 for the live player)
local GROUND_DRAG = 20                     -- external (FUN_80093728)
local AIR_DRAG_SIDE = 3                    -- external: drag in the ground plane in the air (hypothesis: FUN_8040f990's second output is the planar part)
local AIR_DRAG_UP = 1                      -- external: drag along the up axis in the air
local AIR_GRAVITY = -320                  -- external (FUN_800a7700, DAT_805f2bd8); the engine's gravity is off for him (weight 0)
local GROUND_GRAVITY = -800               -- external (DAT_805f2bd4): straight down, along the normal from GROUND_GRAVITY_FAST on; REF "Gravity"
local AIR_ACCEL = 220                      -- external (Fall, FUN_80080828); 110 with player flag 0x1e (not ported)
local JUMP_SPEED = 100                     -- external (Jump, table 0x804b0558)
local JUMP_HOLD_ACCEL = 270                -- external (same table): along the jump direction while held
local JUMP_HOLD_TIME = 0.75                -- external (DAT_805f2794)
local DASH_SPEED = 180                      -- external (FUN_800882a4, DAT_805f27a8), observed: added once along the facing
-- observed (live_jumpdash_all 7.5603 s, live_uphill_jump; 2 jumps): the Jump behavior, and its animation, ends 2 ticks after the vertical speed first drops to 0 or below, not on that tick. Counted in ticks.
local JUMP_FALL_DELAY = 2
-- observed (live Landing entries, docs/ai/REPLAY.md: Fall lasts 14 to 26 ticks after a Jump, 1 to 4 after a JumpDash) and by the user (a plain jump's landing started for 2 frames with its effect when it should not): the airborne Landing cannot start in a jump (no dash) until this many ticks after the vertical speed first dropped to 0 or below.
local JUMP_LANDING_MIN_TICKS = JUMP_FALL_DELAY + 14
local DASH_UPDATES = 25                     -- observed (live_jumpdash_holdstick2, 5 dashes): the push is in the acceleration of 25 updates, the entry one and 24 more (external DAT_805f27a0 = 0.4 s = 24 ticks); counted in ticks, a float 0.4 s ended it after 23 or 24. Then the Fall state
local DASH_PUSH_TIME = 2.0                  -- external (DAT_805f27a4): the push's countdown (outlasts the dash)
local DASH_PUSH_ACCEL = 430                 -- external (DAT_805f27b0), observed: every tick of the dash along the facing
local FAST_SPEED = 30                     -- external (DAT_805f2668): below this the target does not grow
local COSINE_START = 0.766                 -- external: stick / facing cosine from which the target grows to the maximum at 1
local ACCEL_SHARE = 0.25                   -- external (DAT_805f26ac): growth a second, share of (max - min), x cosine^8
local NO_STICK_FULL_SPEED = 537.5          -- observed: with no stick the word is 1000 + 14000 x min(1, speed / 537.5), from speed 30
local STICK_FULL = 0.9                  -- observed (live_half), external: below this stick length the word decays
local STICK_DECAY = 0.9                    -- external: the word's factor per update below STICK_FULL
local TURN_SLOW = 15                       -- external (FUN_800831d8): turn rate up to speed 150
local TURN_FAST = 2                        -- external: turn rate from 300
local TURN_FAST_FROM = 150                 -- external (FUN_800838f4, DAT_805f26b8), observed: above it (last tick's 3D speed) and stick >= STICK_FULL the acceleration follows the facing; REF "The ground acceleration +0x24"
local AIR_TURN = 15                        -- external (Fall)
local JUMP_TURN = 6.5                      -- external (Jump, DAT_805f2788)
local IDLE_SPEED = 1                     -- placeholder, see the animation block
local SLOPE_NORMAL_Y = 0.95               -- a ground whose normal's height is under this counts as a slope for the animation (the recorded slope is 0.866)
local SLOPE_WALK_SPEED = 12.5               -- the speed the Ground animation is held to on a slope with no stick until he stops: rate 1, the walk (no idle pair in between)
local ANIM_IDLE_SPEED = 0.2                -- observed: the original goes Ground to Idle with no stick when the planar speed falls under about 0.2 (27 transitions in the recordings: planar after 0.145 to 0.199, before 0.156 to 0.278)
local HOLD_PLANAR_SPEED = 110              -- hypothesis: round value in the observed (105.9, 119.8] (live_fence_headon 1.80 s held at 119.8; was 150 in (105.9, 153]); REF section 20
local SKID_MIN_PLANAR = 200                -- hypothesis: round value in the observed (151.8, 221.65]; REF "Skid at planar 221.65"
local SKID_COSINE = 0                      -- hypothesis: 90 degrees, observed range (44, 93]
local SKID_END_PLANAR = 0.2                -- observed (live_flick): threshold or first speed below it, unknown
local LANDING_FACTOR = 0.7166               -- observed (16 landings): probably the ground drag e^(-1/3); REF "Landing tick"
local DASH_END_LANDING_GAP_PER_SPEED = 0.25
local DASH_END_LANDING_TICKS = 8
local LANDING_GAP_PER_SPEED = 0.118          -- fitted (27 landings, 18 fit; fixed 9.7 fit 10): Landing starts when the gap below is at most this times the fall speed. REF "Landing trigger vs the recordings"
local LANDING_PROBE = 40                    -- placeholder: longest gap the Landing test looks down for
local GROUND_GRAVITY_FAST = 250             -- observed (live_slopes 1.52 to 1.58 s): 3D speed from which gravity stops pulling along the surface
local UPHILL_TARGET_SHIFT = math.rad(7.6)        -- observed fit (live_slopes 7.0 to 8.4 s): turn target offset per unit sin(tilt) on fast uphill rows
local SLIDE_MAX_NORMAL_Y = 0.87             -- hypothesis: with no stick the slope pull acts only this steep or steeper; observed: slides on at 0.866 with speed 17 (live_jumpdash), stands still at 0.872 from rest (live_jumpdash_all 16.35 s, 190 rows)
local JUMP_BALL_DELAY_TICKS = 15           -- ball A starts this many ticks into the jump motion (live measurement: 14 and 15)
local BRAKE_SPEED = 150                  -- placeholder: Brake motion from this speed with the stick pointing back
local GROUND_SNAP = 1                      -- only switches the ground probe on (the engine knows its length, see attachShadowPlayer); it acts only when he was on the ground the tick before (the game's player flag 2), and is off after a jump
local SNAP_MEMORY_TICKS = 2              -- placeholder: ticks after the last ground contact the probe still acts (the contact flickers)
-- Keyboard hack, NOT in the original (docs/CHANGED_FROM_ORIGINAL.md): a mouse flick at speed looks like a sharp stick turn and slowed him for no visible reason (observed, 8 to 12% lost in a tick).
-- Above COSINE_DEAD_MIN_SPEED the stick / heading angle counts COSINE_DEAD_ANGLE smaller in the speed cosine only; the acceleration still follows the real stick.
-- Facing hack, NOT in the original: keys and mouse change the stick at once, so the velocity leaves the slow heading (observed: skating sideways). Above FACE_FOLLOW_SPEED the model
-- faces the planar velocity, smoothed at the original's visual rate 30 a second (FUN_800a750c).
local FACE_FOLLOW_SPEED = 30
local FACE_SMOOTHING = 30
-- The model's smoothed ground normal (external, FUN_8009339c, FUN_800934fc; the game's +0x17c): towards the ground's normal on the ground, straight up in the air, at most GROUND_ALIGN_ANGLE
-- and GROUND_ALIGN_MAX_T of the way a frame. Approximation: a normalized mix instead of a slerp. REF section 16.
local GROUND_ALIGN_ANGLE = math.rad(15)
local GROUND_ALIGN_MAX_T = 0.2
local COSINE_DEAD_ANGLE = math.rad(30)
local COSINE_DEAD_MIN_SPEED = 150
local JUMP_GRACE_TICKS = 2                 -- placeholder: ticks after a jump in which the last move may still report the ground
local FACE_YAW_OFFSET = math.pi             -- observed: the model's front is -Z in the file and the instance and engine world each turn 90 degrees, so rotation pi - h faces heading h (0 and -pi/2 were tried and wrong)
local AIR_ANIMATION_TICKS = 8             -- placeholder: ticks without ground before the air motions (contact flickers)
local LAND_ANIMATION_TICKS = 3            -- placeholder: ticks of ground in a row before the ground motions again

local function wrapAngle(angle)
	return angle - 2 * math.pi * math.floor((angle + math.pi) / (2 * math.pi))
end

-- At an exact 180 degree reversal the original turned the negative way (observed, live_skid 2.58 s, one case); the tolerance is a guess. REF "Turn direction at an exact 180 degree reversal".
local TURN_TIE_TOLERANCE = 0.01

local function turnDifference(angle)
	local difference = wrapAngle(angle)
	if difference > math.pi - TURN_TIE_TOLERANCE then difference = difference - 2 * math.pi end
	return difference
end

-- Turn rate from the speed (external, FUN_800831d8; observed): 15 up to 150, linearly to 2 at 300, then 2.
local function turnRate(speed)
	if speed <= TURN_FAST_FROM then return TURN_SLOW end
	if speed >= 300 then return TURN_FAST end
	return TURN_SLOW + (TURN_FAST - TURN_SLOW) * (speed - 150) / 150
end

-- A horizontal vector laid into the ground plane by the raw normal's basis (the game's +0x1e4: shortest rotation of up onto the normal, REF section 16); a rotation, so its length stays.
local function layInGround(x, z, nx, ny, nz)
	local axisX, axisZ = nz, -nx
	local sinTilt = math.sqrt(axisX * axisX + axisZ * axisZ)
	if ny <= 1e-3 or sinTilt <= 1e-6 then return x, 0, z end
	local ux, uz = axisX / sinTilt, axisZ / sinTilt
	local along = ux * x + uz * z
	return x * ny + ux * along * (1 - ny), sinTilt * (uz * x - ux * z), z * ny + uz * along * (1 - ny)
end

-- A vector turned about the horizontal axis that takes up onto the normal, the way layInGround does for horizontal ones; sign -1 turns it back (the normal's basis inverse).
local function turnByGround(x, y, z, nx, ny, nz, sign)
	local sinTilt = math.sqrt(nx * nx + nz * nz)
	if sinTilt <= 1e-6 then return x, y, z end
	local ax, az = nz / sinTilt, -nx / sinTilt
	local s, c = sign * sinTilt, ny
	local dot = ax * x + az * z
	return x * c + (-az * y) * s + ax * dot * (1 - c), y * c + (az * x - ax * z) * s, z * c + (ax * y) * s + az * dot * (1 - c)
end

sf.behavior("ShadowPlayer", function(entity)
	local state = sf.state(entity)
	local dt = sf.tickTime()

	-- The camera's heading over the ground, and the way to its right.
	local fx, _, fz = sf.cameraForward()
	local length = math.sqrt(fx * fx + fz * fz)
	if length > 1e-4 then fx, fz = fx / length, fz / length else fx, fz = 0, -1 end
	if state.heading == nil then state.heading = math.atan(fz, fx) end

	-- A replay of a recorded run (replay.lua) takes the place of the keyboard and of the camera's heading while it runs.
	local replay = ShadowReplay.step()
	local controlled = state.active and not sf.debugMode()
	local wx, wz = 0, 0
	if replay ~= nil then
		wx, wz = replay.wx, replay.wz
	else
		local rx, rz = -fz, fx
		if controlled and sf.key("W") then wx, wz = wx + fx, wz + fz end
		if controlled and sf.key("S") then wx, wz = wx - fx, wz - fz end
		if controlled and sf.key("D") then wx, wz = wx + rx, wz + rz end
		if controlled and sf.key("A") then wx, wz = wx - rx, wz - rz end
	end
	local wishLength = math.sqrt(wx * wx + wz * wz)
	local hasStick = wishLength > 1e-4
	local mag = math.min(wishLength, 1)
	local sx, sz = 0, 0
	if hasStick then sx, sz = wx / wishLength, wz / wishLength end

	local vx, vy, vz = sf.velocity(entity)
	-- The velocity as the engine step left it, before this tick changes anything, for the replay log (ShadowReplay.log).
	state.logV0x, state.logV0y, state.logV0z = vx, vy, vz
	local grounded = sf.grounded(entity)
	if (state.jumpGrace or 0) > 0 then
		state.jumpGrace = state.jumpGrace - 1
		grounded = false
	end
	-- Rising after a jump he is in the air whatever the last move reports (observed: the contact lasted a few ticks after the jump and reset it).
	local speed = math.sqrt(vx * vx + vy * vy + vz * vz)
	-- On the ground the speed is the one along the surface (observed, live_slope 2.9 to 3.5 s: the original turned at the 3D speed's rate; hypothesis: the engine already removed the part into the floor). REF "Turn rate by speed".
	if grounded then
		local gnx, gny, gnz = sf.groundNormal(entity)
		if gny > 1e-3 and gny < 1 then
			-- Without the last press's horizontal part (observed, live_slopes 1.27 to 1.33 s: the press along a tilted normal cut the horizontal 6, the reading fell under GROUND_GRAVITY_FAST on every other tick and the slope pull
			-- switched on: in-plane speed alternated +5 / -1.5 against the original's smooth +1; hypothesis for the cause). Only on a tick that follows a ground tick.
			local pressed = 0
			if not state.wasAir then pressed = state.lastPressNormal or 0 end
			local hx, hz = vx - pressed * gnx, vz - pressed * gnz
			local planeVy = -(gnx * hx + gnz * hz) / gny
			speed = math.sqrt(hx * hx + planeVy * planeVy + hz * hz)
		end
	end
	local planarBefore = math.sqrt(vx * vx + vz * vz)
	-- The ground probe (see GROUND_SNAP): on while he was on the ground a moment ago and has not jumped; its length is the engine's: the move of the frame is added by the body's snap mode (attachPlayer).
	state.sinceGround = grounded and 0 or (state.sinceGround or SNAP_MEMORY_TICKS + 1) + 1
	local snapping = state.sinceGround <= SNAP_MEMORY_TICKS and not state.jumped
	sf.setGroundSnap(entity, snapping and GROUND_SNAP or 0)

	local pressed = controlled and sf.key("SPACE")
	if replay ~= nil then pressed = replay.a end
	local justPressed = pressed and not state.spaceHeld
	state.spaceHeld = pressed

	-- Heading (external, FUN_80093af8, FUN_80093ba0): towards the stick by min(rate x dt, 1) of the short way (hypothesis). The update on which a skid ends is drag only, turn and word held
	-- (observed, live_skid, three skids). REF "The tick after a skid ends is drag only".
	local skidEnding = false
	if state.skid and (not grounded or planarBefore < SKID_END_PLANAR) then
		state.skid = false
		state.speed = MIN_SPEED
		skidEnding = grounded
	end
	local heading = state.heading
	local headingBefore = heading
	local stickAngle = hasStick and math.atan(sz, sx) or nil
	local cosine = 0
	if hasStick then
		cosine = sx * math.cos(heading) + sz * math.sin(heading)
		-- The skid (observed, live_flick, live_flick2, live_skid; thresholds hypotheses): stick 90 degrees or more away at speed gives drag only, heading and word frozen, stick ignored until planar 0.2.
		if grounded and not state.skid and planarBefore >= SKID_MIN_PLANAR and cosine <= SKID_COSINE then state.skid = true end
		-- The keyboard hack is off in replays: their stick is the original's pad (observed: with it the port outran the original in turns by 50 to 80).
		if replay == nil and planarBefore >= COSINE_DEAD_MIN_SPEED then
			cosine = math.cos(math.max(0, math.acos(math.max(-1, math.min(1, cosine))) - COSINE_DEAD_ANGLE))
		end
		local rate
		if grounded then rate = turnRate(speed)
		elseif state.jumped then rate = JUMP_TURN
		else rate = AIR_TURN end
		state.turnRate, state.turnSpeed = rate, speed
		-- The turn target (external, FUN_80093ba0; observed arithmetic on live_slopes). Below GROUND_GRAVITY_FAST 3D speed it is the horizontal stick taken into the ground basis (slow slope windows: yaw model rms 0.99 deg, mean 0.03,
		-- against 1.74 and 0.86 with the horizontal target); the yaw then lives in the basis, so the same angle is the growth angle (slow downhill 0.65 deg mean error against the original's angle word, horizontal 1.60;
		-- slow uphill keeps the 3D rule below, 1.30). From GROUND_GRAVITY_FAST the target stays the horizontal stick, shifted on uphill rows (UPHILL_TARGET_SHIFT below); the original's rule there is the stick in the basis
		-- turned by the camera direction taken into the basis (0.0 deg on 13 rows, 7.0 to 8.4 s), not ported: the shift fixed the heading at 7.5 to 8.4 s without the camera's pitch. REF "The camera and the stick's world direction".
		local targetAngle = stickAngle
		local gnx, gny, gnz = sf.groundNormal(entity)
		if grounded and gny > 1e-3 and gny < 0.999 then
			local lx, lz
			if speed >= GROUND_GRAVITY_FAST then
				-- Fast rows climbing: the original's turn target sits tilt x 7.6 deg off the horizontal stick (observed, live_slopes 7.0 to 8.4 s: fit rms 1.08 deg against 1.62; downhill rows 0; cause unknown). REF "Turn-target offset splits by climb direction" and "Uphill offset fit".
				if vy > 0 then targetAngle = stickAngle - UPHILL_TARGET_SHIFT * math.sqrt(gnx * gnx + gnz * gnz) end
			else
				lx, _, lz = turnByGround(sx, 0, sz, gnx, gny, gnz, -1)
			end
			if lx ~= nil then
				targetAngle = math.atan(lz, lx)
				local hx, hz = math.cos(heading), math.sin(heading)
				if speed >= GROUND_GRAVITY_FAST or gnx * hx + gnz * hz >= 0 then cosine = math.cos(turnDifference(targetAngle - heading)) end
			end
		end
		-- The airborne Landing state does not turn the facing (observed, live_jumpdash_holdstick2, 5 landings: it turns on the entry row, then 0.0 per tick through the Landing rows and the first Ground row). REF "After the 25-update dash".
		-- The first Ground row after any landing keeps the facing too (observed after the airborne Landing state; the same for a touchdown without it is a hypothesis, live_jumpdash_holdstick2 landing at 2.5 s, where the port's turn there made the growth cosine 0.73 against the original's 0.84 and dropped the word to 1000).
		if not state.skid and not skidEnding and not state.airLanding and not (state.wasAir and grounded) then heading = heading + turnDifference(targetAngle - heading) * math.min(rate * dt, 1) end
	end
	state.heading = heading
	if not hasStick then state.turnRate, state.turnSpeed = nil, nil end

	local acceleration = 0
	local animSpeed, animVx, animVz
	local k = GROUND_DRAG
	local startJump = grounded and justPressed
	if startJump then
		-- The jump's start (external, FUN_800877c0; observed): the velocity's part along the ground normal becomes JUMP_SPEED; no integration that frame. REF section 20.
		local nx, ny, nz = sf.groundNormal(entity)
		if nx * nx + ny * ny + nz * nz < 0.0001 then nx, ny, nz = 0, 1, 0 end
		local along = vx * nx + vy * ny + vz * nz
		vx, vy, vz = vx + (JUMP_SPEED - along) * nx, vy + (JUMP_SPEED - along) * ny, vz + (JUMP_SPEED - along) * nz
		state.jumped, state.holdTime, state.jumpGrace, state.jumpPlanar, state.jumpCutShort = true, JUMP_HOLD_TIME, JUMP_GRACE_TICKS, planarBefore, false
		state.jumpDirX, state.jumpDirY, state.jumpDirZ = nx, ny, nz
		state.dashed, state.dashTime = false, nil
		-- The airborne Landing state ends with the jump (observed in the port, live_jumpdash_holdstick 2.4 s).
		state.airLanding = false
		state.firstGrowthAfterLanding = false
		state.speed = MIN_SPEED
	elseif grounded then
		-- The speed word (external, FUN_800831d8, FUN_80082c48): target MIN_SPEED + share x range, share from COSINE_START (only from FAST_SPEED); grows by cosine^8 x dt x ACCEL_SHARE x range.
		-- With no stick it follows the NO_STICK_FULL_SPEED line and the acceleration is zero. REF section 8.
		state.speed = state.speed or MIN_SPEED
		local range = MAX_SPEED - MIN_SPEED
		-- The landing tick (observed, 16 landings, 2026-10-06): the first Ground row carries the landing frame's whole 3D speed S along the ground, no drag or acceleration that frame, and the word is
		-- MIN_SPEED + range x min(1, LANDING_FACTOR x S / NO_STICK_FULL_SPEED). The port sees the ground a tick late, so S is the velocity set the tick before (hypothesis). REF "Landing tick".
		local landed = state.wasAir
		local landingCarry = false
		if landed then
			local lx, ly, lz = state.lastVx or vx, state.lastVy or vy, state.lastVz or vz
			local landingSpeed = math.sqrt(lx * lx + ly * ly + lz * lz)
			state.speed = math.min(LANDING_FACTOR * landingSpeed / NO_STICK_FULL_SPEED, 1) * range + MIN_SPEED
			-- Landing with no stick stops him dead (observed, live_run, one landing; hypothesis for all).
			if not hasStick then vx, vz = 0, 0
			else
				local gnx, gny, gnz = sf.groundNormal(entity)
				local dy = 0
				if gny > 1e-3 then dy = -(gnx * lx + gnz * lz) / gny end
				local dl = math.sqrt(lx * lx + dy * dy + lz * lz)
				if dl > 1e-6 then
					vx, vy, vz = lx / dl * landingSpeed, dy / dl * landingSpeed, lz / dl * landingSpeed
					landingCarry = true
				end
			end
		end
		-- The growth cosine and target of this tick for the replay log (ShadowReplay.log); nil when no growth ran.
		state.logCosine, state.logTarget = nil, nil
		if state.skid or skidEnding then
			-- Skidding: no acceleration, word held; only the ground drag acts.
			acceleration = 0
		elseif landed and hasStick then
			acceleration = 0
			state.firstGrowthAfterLanding = true
		elseif hasStick and not state.hadStick and planarBefore < IDLE_SPEED then
			-- The tick the stick starts him from rest accelerates nothing (observed, 9 Idle to Ground transitions in 8 recordings: the first Ground row has planar speed 0 to 0.1, the next one the full step with that tick's pad;
			-- hypothesis: Idle takes the stick and switches to Ground). The word stays. REF "Start from rest".
			acceleration = 0
		elseif hasStick then
			local share = 0
			if speed >= FAST_SPEED and cosine >= COSINE_START then share = (cosine - COSINE_START) / (1 - COSINE_START) end
			local target = share * range + MIN_SPEED
			-- Uphill below GROUND_GRAVITY_FAST the growth angle is the 3D angle between the horizontal stick and the heading laid into the ground plane (observed arithmetic on live_slopes, 49 rows, mean error 1.3 deg; hypothesis for the cause); only the growth changes, not the target.
			-- Replaces the earlier linear sum of stick angle and inclination, which overshot by 5-15 deg off the heading. REF "Uphill growth".
			local gnx, gny, gnz = sf.groundNormal(entity)
			local growCosine = cosine
			local hx, hz = math.cos(heading), math.sin(heading)
			if speed < GROUND_GRAVITY_FAST and gny > 1e-3 and gny < 0.999 and gnx * hx + gnz * hz < 0 then
				local hy = -(gnx * hx + gnz * hz) / gny
				growCosine = (sx * hx + sz * hz) / math.sqrt(hx * hx + hy * hy + hz * hz)
			end
			state.logCosine, state.logTarget = growCosine, target
			local grown = math.max(growCosine, 0) ^ 8 * dt * range * ACCEL_SHARE + state.speed
			-- Stick length (observed, live_half): the acceleration is word x length; below STICK_FULL the word decays by STICK_DECAY down to MIN_SPEED instead of growing. REF section 20.
			if mag < STICK_FULL then state.speed = math.max(state.speed * STICK_DECAY, MIN_SPEED)
			else
				-- A target below the word leaves it unchanged from HOLD_PLANAR_SPEED on, else drops to it (observed, live_turn, live_flick, live_flick2, live_snap).
				-- Not on the first growth row after a landing: the original dropped the word to 1000 there at planar 155 and 188 (observed, live_jumpdash_stick 2.41 s and 7.18 s, the only drops at 150 or more
				-- in all recordings; hypothesis: the hold does not apply there, two samples). REF "Landing tick".
				local newSpeed = math.min(grown, target)
				if newSpeed < state.speed and math.sqrt(vx * vx + vz * vz) >= HOLD_PLANAR_SPEED and not state.firstGrowthAfterLanding then newSpeed = state.speed end
				state.speed = newSpeed
			end
			state.firstGrowthAfterLanding = false
			acceleration = math.min(state.speed, MAX_SPEED) * mag
		else
			-- No stick: the word is the line at the speed after this update's drag (observed, live_skid release 2.53 s).
			local dragged = speed * math.exp(-k * dt)
			if dragged >= FAST_SPEED then state.speed = math.max(math.min(dragged / NO_STICK_FULL_SPEED, 1), 0) * range + MIN_SPEED
			else state.speed = MIN_SPEED end
		end
		local ex = math.exp(-k * dt)
		local nx, ny, nz = sf.groundNormal(entity)
		-- The acceleration's direction (external, FUN_800831d8; observed, nine 2026-10-06 recordings): above TURN_FAST_FROM with stick >= STICK_FULL the pre-turn facing laid into the raw ground plane, x stick length;
		-- else the world stick, horizontal below GROUND_GRAVITY_FAST and in the raw ground plane from it on (hypothesis: the game's tilt also depends on camera pitch). REF "The ground acceleration +0x24".
		local ax, ay, az
		if speed > TURN_FAST_FROM and mag >= STICK_FULL then
			ax, ay, az = layInGround(acceleration * math.cos(headingBefore), acceleration * math.sin(headingBefore), nx, ny, nz)
		elseif speed >= GROUND_GRAVITY_FAST then
			ax, ay, az = layInGround(acceleration * sx, acceleration * sz, nx, ny, nz)
		else
			ax, ay, az = acceleration * sx, 0, acceleration * sz
		end
		-- Length clamp (external, FUN_800831d8, FUN_800a7700): stick part + ground gravity clamped to MAX_SPEED. The port keeps gravity apart (slope pull, press), so the factor scales all of them.
		local gx, gy, gz = 0, GROUND_GRAVITY, 0
		if speed >= GROUND_GRAVITY_FAST and ny > 1e-3 then gx, gy, gz = GROUND_GRAVITY * nx, GROUND_GRAVITY * ny, GROUND_GRAVITY * nz end
		local sumLength = math.sqrt((ax + gx) ^ 2 + (ay + gy) ^ 2 + (az + gz) ^ 2)
		local clamp = 1
		if sumLength > MAX_SPEED then clamp = MAX_SPEED / sumLength end
		ax, ay, az = ax * clamp, ay * clamp, az * clamp
		-- The engine's floor push cancels the vertical velocity on the ticks it pushes (CollisionSystem, walkable floor), which on a downhill slope cut the in-plane speed by about 15% every other tick (live_slopes 1.28 to 1.5 s: the logged speed entering the step
		-- 219.7 = sqrt(218^2 + 27^2), the velocity without its vy). The original's velocity lies in the ground plane (observed), so a vy above the plane's is put back on it. It is put back where the last press left it (the velocity's part along the normal is the press,
		-- observed in the rerun: start velocity vy 0 on the pushed ticks, the previous end's vy on the others, the press's horizontal part stays either way), else the press's horizontal part is left uncancelled and costs 5.6 of in-plane speed on those ticks.
		local restored = false
		if ny > 1e-3 and not landingCarry then
			local planeVy = -(nx * vx + nz * vz) / ny + (state.lastPressNormal or 0) / ny
			if speed < GROUND_GRAVITY_FAST then
				-- Below GROUND_GRAVITY_FAST only the cancelled vy comes back, as the last tick left it, not the new plane's (observed, live_slopes ticks 31 to 35 and 42 to 45: the original's velocity stays above a steeper new plane and
				-- falls by gravity). REF "Velocity above a new plane".
				if planeVy < 0 and not landed and state.lastVy and vy > state.lastVy then vy = state.lastVy; restored = true end
			elseif planeVy < 0 and vy > planeVy then vy = planeVy; restored = true end
		end
		-- Ground contact projection (external, FUN_800986f0): the velocity loses its part into the ground plane. The engine only cancels a floor's vertical push, so it is done here with the last move's normal
		-- (the original does it inside the move). REF "Velocity at a ground contact".
		if ny > 1e-3 then
			local into = vx * nx + vy * ny + vz * nz
			if into < 0 then vx, vy, vz = vx - into * nx, vy - into * ny, vz - into * nz end
		end
		-- Where the plane got steeper the restore above kept the horizontal speed and lengthened the velocity (live_slopes 1.017, 1.100, 1.250 s: port in-plane speed +13, +18, +4.5 over the previous tick's
		-- out, the original +6, +6, -2). The original turns the velocity into the new plane keeping its length (observed, live_slopes 1.080 to 1.098 s: 3D speed 217.1 to 220.9, no jump; hypothesis for the
		-- cause), so the length is capped at the previous tick's out. Only after a restore, on a tick that follows a ground tick.
		if restored and speed >= GROUND_GRAVITY_FAST and not landed and state.lastOut then
			local length = math.sqrt(vx * vx + vy * vy + vz * vz)
			if length > state.lastOut * 1.001 then vx, vy, vz = vx * state.lastOut / length, vy * state.lastOut / length, vz * state.lastOut / length end
		end
		-- Above the plane (observed, live_slopes: a steeper new plane leaves the velocity 18 units out of it, falling by the straight-down gravity under the ground drag, within 0.9 over 4 ticks; hypothesis for the cause): the whole
		-- gravity acts, no slope pull and no press, and only a fall through the plane is projected. REF "Velocity above a new plane".
		local aboveGround = speed < GROUND_GRAVITY_FAST and ny > 1e-3 and not landingCarry and vx * nx + vy * ny + vz * nz > 0.1
		-- For the replay log (ShadowReplay.log): the speed entering the step and the normal this tick used.
		state.logIn = math.sqrt(vx * vx + vy * vy + vz * vz)
		state.logNx, state.logNy, state.logNz = nx, ny, nz
		-- Gravity along the surface below GROUND_GRAVITY_FAST (observed, live_slopes 0.86 to 1.6 s; follows slope changes 1 to 4 frames late, cause unknown). With no stick only on steep ground (SLIDE_MAX_NORMAL_Y).
		-- REF "Ground gravity acts along the surface".
		local slopePull = hasStick or (ny > 1e-3 and ny <= SLIDE_MAX_NORMAL_Y)
		if aboveGround then
			ay = ay + GROUND_GRAVITY * clamp
		elseif speed < GROUND_GRAVITY_FAST and ny > 1e-3 and slopePull then
			local pull = GROUND_GRAVITY * clamp
			ax, ay, az = ax - pull * ny * nx, ay + pull * (1 - ny * ny), az - pull * ny * nz
		end
		if not landingCarry then
			vx = ax / k + (vx - ax / k) * ex
			vy = ay / k + (vy - ay / k) * ex
			vz = az / k + (vz - az / k) * ex
		end
		if aboveGround then
			local into = vx * nx + vy * ny + vz * nz
			if into < 0 then vx, vy, vz = vx - into * nx, vy - into * ny, vz - into * nz end
		end
		-- Uphill the part of the step's result into the ground plane is only dropped (plain projection; observed, live_slopes 5.579 to 5.596 s: at the plane change the original's 3D speed falls by 0.9781 = the cosine between the two planes 0.9720
		-- x the tick's growth 1.0063, and 5.862 s: 1.0048 = 0.9993 x 1.0055; observed, live_uphill_walk: plain projection gives the original's plateau, the length-kept turn gave planar 27 against 19.4). REF "Uphill: the step is turned into the ground plane".
		if ny > 1e-3 and not landingCarry and nx * vx + nz * vz < 0 then
			local into = vx * nx + vy * ny + vz * nz
			if into < 0 then vx, vy, vz = vx - into * nx, vy - into * ny, vz - into * nz end
		end
		-- Downhill the velocity follows the surface (hypothesis, not decoded).
		if ny > 1e-3 and not landingCarry then
			local planeVy = -(nx * vx + nz * vz) / ny
			if planeVy < 0 and vy <= 1 and not aboveGround then vy = planeVy end
		end
		-- The animation's speed: the velocity before the press (the original's stored velocity has no part into the ground; observed in the animation log).
		animSpeed = math.sqrt(vx * vx + vy * vy + vz * vz)
		animVx, animVz = vx, vz
		state.logOut = animSpeed
		state.lastOut = animSpeed
		-- The ground press (GROUND_GRAVITY): keeps him on the ground, the engine cancels it on a walkable floor. Along the normal from GROUND_GRAVITY_FAST on (observed, live_slopes), else straight down.
		-- With no stick on ground gentler than SLIDE_MAX_NORMAL_Y the press also goes along the normal (observed, live_jumpdash_all 16.4 s: the original stands still on ny 0.872; the straight-down press made the port creep about 5 units a second; hypothesis for the cause).
		-- When the slope pull applied, only the press's part along the normal is added: its in-plane part is the pull, already counted
		-- (hypothesis, port arithmetic; the original has one gravity and projects onto the plane, external FUN_800a7700, FUN_800986f0). REF "Ground gravity acts along the surface".
		local press = GROUND_GRAVITY * clamp / k * (1 - ex)
		-- Standing still (no stick, no slope pull, no horizontal velocity) he gets no press: it only left a stored velocity of (5.6, -9.9) when a wall blocked the engine's cancel (observed, live_jumpdash_all 16.4 s; hypothesis, untested: the ground probe holds him on the floor without it).
		state.lastPressNormal = 0
		if aboveGround or (not slopePull and vx * vx + vz * vz < 0.01) then state.lastPressNormal = 0
		elseif (speed >= GROUND_GRAVITY_FAST or not slopePull) and ny > 1e-3 then vx, vy, vz = vx + press * nx, vy + press * ny, vz + press * nz; state.lastPressNormal = press
		elseif speed < GROUND_GRAVITY_FAST and ny > 1e-3 and slopePull then vx, vy, vz = vx + press * ny * nx, vy + press * ny * ny, vz + press * ny * nz; state.lastPressNormal = press * ny
		else vy = vy + press end
		state.jumped, state.holdTime = false, 0
		state.dashed, state.dashTime = false, nil
		sf.setDashTrail(entity, false)
		state.airLanding = false
	else
		-- In the air (external, Fall, Jump, FUN_80093728): AIR_ACCEL along the stick, drags AIR_DRAG_SIDE / AIR_DRAG_UP; JUMP_HOLD_ACCEL along the jump direction while the button is held, at most
		-- JUMP_HOLD_TIME (observed: release ends it). REF section 20.
		state.speed = MIN_SPEED
		local ax, az = sx * AIR_ACCEL, sz * AIR_ACCEL
		local up = AIR_GRAVITY
		-- The jump dash (external, FUN_800882a4; observed): DASH_SPEED once along the pre-turn facing, then DASH_PUSH_ACCEL each tick along that tick's pre-turn facing (not the facing at the dash's start: live_jumpdash_holdstick, acceleration fit error 0.1 to 3 against 5 to 19) plus the air's stick; Fall after DASH_TIME. REF "JumpDash push follows the facing every tick".
		-- Hypothesis: the trigger is a new press in the air after a jump, once per air time (the command is not read). REF section 20.
		if justPressed and state.jumped and not state.dashed and state.dashTime == nil then
			state.dashTime, state.dashPush, state.dashed, state.holdTime = DASH_UPDATES, DASH_PUSH_TIME, true, 0
			vx, vz = vx + DASH_SPEED * math.cos(headingBefore), vz + DASH_SPEED * math.sin(headingBefore)
			sf.setDashTrail(entity, true)
		end
		if state.dashTime ~= nil then
			state.dashPush = state.dashPush - dt
			if state.dashPush >= 0 then
				ax, az = ax + DASH_PUSH_ACCEL * math.cos(headingBefore), az + DASH_PUSH_ACCEL * math.sin(headingBefore)
			end
			state.dashTime = state.dashTime - 1
			if state.dashTime <= 0 then state.dashTime, state.jumped = nil, false; sf.setDashTrail(entity, false) end
		elseif state.jumped and (state.holdTime or 0) > 0 and pressed then
			ax, az, up = ax + JUMP_HOLD_ACCEL * (state.jumpDirX or 0), az + JUMP_HOLD_ACCEL * (state.jumpDirZ or 0), up + JUMP_HOLD_ACCEL * (state.jumpDirY or 1)
			state.holdTime = state.holdTime - dt
		elseif state.jumped then
			if (state.holdTime or 0) > 0 then state.jumpCutShort = true end
			state.holdTime = 0
		end
		-- A jumpdash that ends close to the ground goes straight to the Landing animation, with no Fall pose or Fall ball (observed by the user, original; in the port the Fall showed for 1 to 5 ticks first). Within DASH_END_LANDING_TICKS of the dash's end the Landing starts from a wider gap (DASH_END_LANDING_GAP_PER_SPEED is a guess to be tuned by what the user sees; the old recordings had a 1 to 4 tick Fall after a dash, which conflicts).
		-- The airborne Landing state (observed, 15 landings): falling, 5 to 11 frames before touchdown, then only AIR_GRAVITY acts. Tested on the last tick's state; trigger: the gap at most LANDING_GAP_PER_SPEED x fall speed (fitted;
		-- gap from sf.groundGap). REF "The airborne Landing state".
		-- The row that enters the state still has the stick acceleration (observed, live_jumpdash_holdstick2: 217 to 219 on its first Landing rows, zero from the next), so only a state set on an earlier tick zeroes it.
		state.postDashTicks = state.dashed and state.dashTime == nil and (state.postDashTicks or 0) + 1 or 0
		local wasAirLanding = state.airLanding
		if not state.airLanding and state.dashTime == nil and vy < 0 and not (state.jumped and not state.dashed and (state.jumpFallTicks or 0) < JUMP_LANDING_MIN_TICKS) then
			local gap = sf.groundGap(entity, LANDING_PROBE)
			local gapPerSpeed = LANDING_GAP_PER_SPEED
			if state.dashed and (state.postDashTicks or 0) < DASH_END_LANDING_TICKS then gapPerSpeed = DASH_END_LANDING_GAP_PER_SPEED end
			if gap ~= nil and gap <= gapPerSpeed * -vy then state.airLanding = true end
		end
		if wasAirLanding then ax, az, up = 0, 0, AIR_GRAVITY end
		local ex = math.exp(-AIR_DRAG_SIDE * dt)
		vx = ax / AIR_DRAG_SIDE + (vx - ax / AIR_DRAG_SIDE) * ex
		vz = az / AIR_DRAG_SIDE + (vz - az / AIR_DRAG_SIDE) * ex
		vy = up / AIR_DRAG_UP + (vy - up / AIR_DRAG_UP) * math.exp(-AIR_DRAG_UP * dt)
	end

	-- Animation state (see animation.lua): air = jump while rising after a jump, else fall; ground = landing (until its motion ends), idle (no stick and below ANIM_IDLE_SPEED), brake (placeholder rule)
	-- or the ground blend. The contact is debounced so a flicker does not change the state.
	if grounded then
		if (state.airTicks or 0) > 0 then state.lastAirTicks = state.airTicks end
		state.groundTicks, state.airTicks = (state.groundTicks or 0) + 1, 0
	else
		state.groundTicks, state.airTicks = 0, (state.airTicks or 0) + 1
	end
	if state.animGrounded == nil then state.animGrounded = true end
	if state.animGrounded and state.airTicks >= AIR_ANIMATION_TICKS then state.animGrounded = false
	-- After an air spell as long as the air motions' own debounce the touch is no flicker, so the ground motions start at once; the debounce stays for short contact losses. Without it the fall motion played for LAND_ANIMATION_TICKS ticks after the touch (observed by the user: a 2 frame fall with its effect after a jump, JUMP_FALL_DELAY changes did nothing).
	elseif not state.animGrounded and state.groundTicks >= ((state.lastAirTicks or 0) >= AIR_ANIMATION_TICKS and 1 or LAND_ANIMATION_TICKS) then state.animGrounded = true end
	if state.jumped and not grounded then state.animGrounded = false end
	local planar = math.sqrt(vx * vx + vz * vz)
	-- The idle test and the reverse test use the planar velocity before the ground press: on a slope the press leaves a planar part (5.4 at rest on the 0.872 slope, live_uphill_stop port run 3.9 to 4.1 s, position still) that kept the walk playing at a standstill and flipped it with idle.
	local animPlanar = animVx ~= nil and math.sqrt(animVx * animVx + animVz * animVz) or planar
	-- A cut-short jump ends with no Landing animation at all (observed by the user: the port played it for 2 frames with its effect and the original does not; JUMP_FALL_DELAY 20 did not change it, so it is this touch and not the airborne Landing).
	if state.wasAnimGrounded == false and state.animGrounded then state.landing = not state.jumpCutShort; state.jumpCutShort = false end
	-- Hypothesis: a pushed stick ends the landing motion, so he runs on at once.
	if state.landing and hasStick then state.landing = false end
	state.wasAnimGrounded = state.animGrounded
	local motion
	if state.animGrounded then
		state.jumpFallTicks = 0
		if state.landing then motion = "landing"
		elseif state.skid then motion = "brake"
		elseif hasStick and cosine < -0.5 and planar >= BRAKE_SPEED then motion = "brake"
		elseif not hasStick and animPlanar < ANIM_IDLE_SPEED then motion = "idle"
		else motion = "ground" end
	else
		state.landing = false
		state.jumpFallTicks = vy > 0 and 0 or (state.jumpFallTicks or 0) + 1
		-- The Landing animation starts with the airborne Landing state, not at the touch (observed in live_jumpdash_all: the original's anim index changes to Landing 5 to 11 frames before touchdown, the port showed the Fall pose until the contact).
		-- A cut-short jump has no airborne Landing animation (observed by the user: it started briefly in the port and should not start at all; the jump motion is kept to the touch).
		-- A jump whose button was released before the hold time ran out never goes to the Fall animation: it stays the jump until the landing (observed by the user, original; decompile FUN_800871d8, hypothesis: Jump's state 2 goes to state 3 only when the hold timer +0x2c runs out, and only state 3 and 4 lead to Fall, so a release leaves it in state 2 until the ground).
		motion = state.airLanding and not (state.jumped and state.jumpCutShort) and "landing" or state.dashTime ~= nil and "dash" or (state.jumped and (state.jumpCutShort or state.jumpFallTicks <= JUMP_FALL_DELAY) and "jump" or "fall")
	end
	-- The effect balls (REF "Jump_ballA / Jump_ballB decoded"): ball A while the jump motion plays (decompile: from Jump's update to its leave; the condition FUN_800a9e1c it also needs is not decoded), the dash's ball B while the dash lasts (hypothesis: it ends with the dash ribbon), the Fall's ball B in the fall after a dash (the user's observation: the fall effect only appears after a jumpdash; the decompile says a Fall entered from Jump also spawns it, unresolved).
	-- Live measurement (tools/shadowEffectLog.py, two recordings): ball A is created 14 to 15 ticks after the Jump behavior begins, not at takeoff; both balls fade out over 8 ticks after the stop (EffectBalls).
	state.jumpMotionTicks = motion == "jump" and (state.jumpMotionTicks or 0) + 1 or 0
	sf.setEffectBall(entity, "jumpA", motion == "jump" and state.jumpMotionTicks > JUMP_BALL_DELAY_TICKS - 1)
	sf.setEffectBall(entity, "dashB", motion == "dash")
	sf.setEffectBall(entity, "fallB", motion == "fall" and state.dashed)
	-- The walking motions play in reverse while he moves against the model's facing (the user saw the original do it with a negative speed; which signed speed it is is a hypothesis: the planar velocity along the facing of the last tick, with a deadband of IDLE_SPEED).
	local reverse = false
	if state.faceHeading ~= nil then reverse = (animVx or vx) * math.cos(state.faceHeading) + (animVz or vz) * math.sin(state.faceHeading) < -IDLE_SPEED end
	state.animReverse = reverse
	-- MADE UP BY THE USER, kept on purpose (decided 2026-10-07): not the original's behavior (the recording swaps to the idle pair in about 2 frames there): on a slope with no stick the Ground animation never drops under the walk until he stops, so the release before the slide back shows no swap to idle.
	local ratedSpeed = animSpeed or math.sqrt(vx * vx + vy * vy + vz * vz)
	local _, slopeNy = sf.groundNormal(entity)
	if motion == "ground" and not hasStick and grounded and slopeNy < SLOPE_NORMAL_Y then ratedSpeed = math.max(ratedSpeed, SLOPE_WALK_SPEED) end
	if ShadowAnimation.update(entity, state, motion, {speed = ratedSpeed, jumpPlanar = state.jumpPlanar or 0, reverse = reverse}) then state.landing = false end
	local faceHeading = heading
	if planar >= FACE_FOLLOW_SPEED then faceHeading = math.atan(vz, vx) end
	state.faceHeading = state.faceHeading or faceHeading
	state.faceHeading = state.faceHeading + wrapAngle(faceHeading - state.faceHeading) * math.min(FACE_SMOOTHING * dt, 1)
	local tx, ty, tz = 0, 1, 0
	if grounded then tx, ty, tz = sf.groundNormal(entity) end
	local sx0, sy0, sz0 = state.normalX or 0, state.normalY or 1, state.normalZ or 0
	local cosAngle = math.max(-1, math.min(1, sx0 * tx + sy0 * ty + sz0 * tz))
	local angle = math.acos(cosAngle)
	if angle > 1e-4 then
		local t = math.min(GROUND_ALIGN_MAX_T, GROUND_ALIGN_ANGLE / angle)
		sx0, sy0, sz0 = sx0 + (tx - sx0) * t, sy0 + (ty - sy0) * t, sz0 + (tz - sz0) * t
		local length = math.sqrt(sx0 * sx0 + sy0 * sy0 + sz0 * sz0)
		sx0, sy0, sz0 = sx0 / length, sy0 / length, sz0 / length
	end
	state.normalX, state.normalY, state.normalZ = sx0, sy0, sz0
	sf.orient(entity, FACE_YAW_OFFSET - state.faceHeading, sx0, sy0, sz0)
	state.wasAir = not grounded
	state.hadStick = hasStick
	state.lastVx, state.lastVy, state.lastVz = vx, vy, vz
	sf.setVelocity(entity, vx, vy, vz)
	ShadowReplay.log(entity, motion, wx, wz, pressed, fx, fz)
end)

-- Called by main.lua's attachPlayer for the Shadow entity.
-- The player's state table as it starts (also after the engine's reset: resetPlayers, main.lua).
function initShadowState(state)
	state.speed = MIN_SPEED
end

function attachShadowPlayer(entity)
	initShadowState(sf.state(entity))
	sf.give(entity, "ShadowPlayer")
	sf.setWeight(entity, 0)
	-- His ground snap is the game's probe (PhysicsBody::snapByRay): a ray along minus the last floor's normal from his center, 2.37 times his sphere radius (5.0 in the live record) plus the frame's move long, accepting a surface within 80 degrees of that normal. blockedByWall (hypothesis, the fence kick of live_slopes): a wall stopping that snap move before a floor leaves him not grounded, so the air branch (Fall) runs.
	sf.setGroundSnapMode(entity, {ray = true, radius = 5.0, minNormalY = math.cos(math.rad(80)), blockedByWall = true})
	-- His move is the mean of the old and new velocity (observed, live_jumpdash_stick: every height step = (old vy + new vy) / 120 within 0.0005).
	sf.setMeanVelocityMove(entity, true)
	sf.setUncullable(entity, true)
end
