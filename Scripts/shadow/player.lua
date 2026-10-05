-- Shadow as the player (Shadow the Hedgehog 2005). While he is the active player (state.active, set by main.lua; J switches) and outside the debug mode (sf.debugMode, Tab) W A S D steer him relative to where the camera looks and Space jumps; in the debug mode those keys fly the camera and he ignores them.
-- The engine's one physics system does the moving, the colliding and the gravity: this sets his velocity from what the game's movement code does with its two vectors, a velocity and an acceleration, which the game integrates with a linear
-- drag (decoded, see docs/SHADOW_PLAYER.md): v' = a - k v, so v = a/k + (v - a/k) e^(-k dt) over a step, with k = 20 on the ground and, in the air, 3 along the ground plane and 1 along the up axis.
-- External (the Ghidra decompile of main.dol, units per second as the game's update gets its time step in seconds: hypothesis): the values marked external. Hypothesis or placeholder where marked: which stick direction the acceleration points
-- along, the cosine, the walk / run split, the gravity (the engine's, -288, is Sonic's and nothing here is Shadow's: the game's gravity is not found), the motions by name. Not ported: the homing attack, the spin dash, the skate, Chaos Control, weapons,
-- sliding, the turn-around of a standing start, the speed curve the game uses with no stick (taken as linear), the slope forces.
-- state.heading = where he faces (radians, forward = (cos, sin) over x and z, like Sonic's script), state.speed = the game's "current speed" (the length of the acceleration along the stick), state.holdTime = what is left of the jump's held boost.

local MIN_SPEED = 1000                     -- external (FUN_80082cc8): the acceleration's length at rest
local MAX_SPEED = 15000                    -- external (FUN_80082cc8): 15000 times a factor of a table indexed by player+0x64; the live player of a RAM dump has index 14, whose factor is 1.0
local GROUND_DRAG = 20                     -- external (FUN_80093728): the drag on the whole vectors while he touches the ground
local AIR_DRAG_SIDE = 3                    -- external: the drag on the part of the vectors in the ground plane in the air (hypothesis: the second output of the splitting function FUN_8040f990 is the planar one, as FUN_80093af8 uses it)
local AIR_DRAG_UP = 1                      -- external: the drag on the part along the up axis in the air
local AIR_GRAVITY = -320                  -- external (FUN_800a7700, DAT_805f2bd8): the acceleration along the up axis every frame in the air, with the engine's gravity off for him (weight 0)
local GROUND_GRAVITY = -800               -- external (same, DAT_805f2bd4): the same on the ground (player flag 2); it presses him into the ground: with the drag of 20 the vertical speed settles at -40. Along the ground's normal instead of the up axis when he runs faster than 250 (a player bit the Ground update sets by that speed; hypothesis: that is its use, it keeps him on loops and walls)
local AIR_ACCEL = 220                      -- external (Fall, FUN_80080828): the acceleration in the air with the stick pushed, 110 with player flag 0x1e (not ported)
local JUMP_SPEED = 100                     -- external (Jump, table at 0x804b0558): the speed along the jump direction the jump starts with
local JUMP_HOLD_ACCEL = 270                -- external (same table): the acceleration along the jump direction while the button is held
local JUMP_HOLD_TIME = 0.75                -- external (DAT_805f2794): seconds the held boost lasts
local DASH_SPEED = 180                      -- external (JumpDash Update, FUN_800882a4, DAT_805f27a8), observed (live log): added to the velocity along the facing, once, when the dash starts
local DASH_TIME = 0.4                       -- external (DAT_805f27a0), observed: seconds the dash lasts; then the Fall state
local DASH_PUSH_TIME = 2.0                  -- external (DAT_805f27a4): a second countdown the push below runs while it is not below 0 (longer than the dash, so the push lasts the whole dash)
local DASH_PUSH_ACCEL = 430                 -- external (DAT_805f27b0), observed: acceleration along the facing every tick of the dash; with the stick's 220 and the drag of 3 the planar speed settles towards 650 / 3 = 216.7 (observed in two dashes)
local FAST_SPEED = 30                     -- external (DAT_805f2668): below this speed the top speed is not allowed to grow
local COSINE_START = 0.766                 -- external: the cosine between the stick and where he faces below which the top speed target is the minimum, from there it grows to the maximum at 1
local ACCEL_SHARE = 0.25                   -- external (DAT_805f26ac): the share of (max - min) a second the speed grows by at full alignment, times the cosine to the 8th
local NO_STICK_FULL_SPEED = 537.5          -- observed (live log of the original, tools/shadowLiveLog.py): with no stick the stored speed is 1000 + 14000 x min(1, speed / 537.5) (a straight line, slope 26.05 per unit of speed), only from a speed of 30
local STICK_FULL = 0.9                  -- observed (tools/live_half.csv), external: the stick length below which the current speed decays instead of growing
local STICK_DECAY = 0.9                    -- external: the speed is multiplied by this each update when the stick is pushed less than 0.9 (not reachable with the keyboard)
local TURN_SLOW = 15                       -- external (FUN_800831d8): the turn rate, per second, as a share of the way to the stick's direction, up to a speed of 150
local TURN_FAST = 2                        -- external: the turn rate above a speed of 300; it falls linearly from 15 at 150 to 2 at 300
local AIR_TURN = 15                        -- external (Fall): the same rate in the air while falling
local JUMP_TURN = 6.5                      -- external (Jump, DAT_805f2788): the same rate while jumping
local IDLE_SPEED = 1                     -- placeholder, see the animation block
local BRAKE_SPEED = 150                    -- placeholder: the Brake motion plays when the stick points the other way at this speed or more
local GROUND_SNAP = 1                      -- only switches the ground probe on (the engine knows its length, see attachShadowPlayer); it acts only when he was on the ground the tick before (the game's player flag 2), and is off after a jump
local SNAP_MEMORY_TICKS = 2              -- placeholder: ticks after the last ground contact the probe still acts (the engine's contact flickers on uneven ground)
-- Keyboard and mouse hack, NOT in the original (docs/CHANGED_FROM_ORIGINAL.md): the stick is camera-relative and the camera turns with the mouse, so a flick of the mouse at speed looks like a sharp stick turn to the speed code (the target of the acceleration
-- falls with the angle between the stick and where he faces, and his heading turns slowly at speed), which slowed him for no reason the player can see (observed: reports of 8 to 12% lost in a tick with W only held, at 29 to 35 degrees). Above
-- COSINE_DEAD_MIN_SPEED the angle between the stick and his heading counts COSINE_DEAD_ANGLE smaller in that cosine (and only there: the acceleration still points along the real stick, so he turns as the game does). An earlier version of this hack kept the stick itself
-- within 20 degrees of his heading, which made turning at speed far too hard (observed).
-- Facing hack, NOT in the original either: the original turns him towards the stick, and at speed only slowly (the rate falls to 2 a second), but its stick is analog and its camera follows him, so he rarely goes where he does not face; here
-- the keys and the mouse camera change the stick by a lot at once and the velocity follows the stick within a few frames while the heading does not (observed: skating, he does not face the way he goes). Above FACE_FOLLOW_SPEED the model faces
-- the direction of his planar velocity instead (turning to it at the original's visual smoothing rate, 30 a second: FUN_800a750c), below it his heading as before.
local FACE_FOLLOW_SPEED = 30
local FACE_SMOOTHING = 30
-- Standing on the ground (decoded, FUN_8009339c, FUN_800934fc, FUN_80079628; the game's orientation of the model is the basis of its smoothed ground normal times the yaw): the smoothed normal (the game's +0x17c) goes towards the ground's normal
-- while he is on it and towards straight up in the air; each frame it turns by at most GROUND_ALIGN_ANGLE and by at most GROUND_ALIGN_MAX_T of the way (min(GROUND_ALIGN_MAX_T, angle limit / angle), the game's curve with the 0.2 at 0x805e459c).
-- Approximation: a normalized mix instead of the game's quaternion slerp.
local GROUND_ALIGN_ANGLE = math.rad(15)
local GROUND_ALIGN_MAX_T = 0.2
local COSINE_DEAD_ANGLE = math.rad(30)
local COSINE_DEAD_MIN_SPEED = 150
local JUMP_GRACE_TICKS = 2                 -- placeholder: the ticks after a jump in which the last move may still report the ground
local FACE_YAW_OFFSET = math.pi             -- the model's front is -Z in the file (observed on the disc: running leans the root by -0.37 about x, towards -z), the instance turns it 90 degrees about y (-Z to -X), the engine's world is the original's turned 90 degrees; so the entity's rotation that puts the front along the heading (forward = (cos h, sin h) over x and z) is pi - h. Offsets tried: 0 faced backwards, -pi/2 (the original's yaw) faced 90 degrees off, with and without the instance's yaw (observed: taking the yaw out of the instance did not fix it). turnY is right-handed.
local AIR_ANIMATION_TICKS = 8             -- placeholder: ticks without ground contact before the air motions play (the engine's ground contact flickers on uneven terrain, as in Sonic's script)
local LAND_ANIMATION_TICKS = 3            -- placeholder: ticks of ground contact in a row before the ground motions play again after the air ones

local function wrapAngle(angle)
	return angle - 2 * math.pi * math.floor((angle + math.pi) / (2 * math.pi))
end

-- The game's rate from the speed (FUN_800831d8): 15 up to 150, falling linearly to 2 at 300, then 2.
local function turnRate(speed)
	if speed <= 150 then return TURN_SLOW end
	if speed <= 300 then return (speed - 300) / -150 * (TURN_SLOW - TURN_FAST) + TURN_FAST end
	return TURN_FAST
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
	local grounded = sf.grounded(entity)
	if (state.jumpGrace or 0) > 0 then
		state.jumpGrace = state.jumpGrace - 1
		grounded = false
	end
	-- Rising after a jump he is in the air whatever the last move reports (observed: the ground contact lasted a few ticks after the jump started, which reset the jump each tick).
	local speed = math.sqrt(vx * vx + vy * vy + vz * vz)
	local planarBefore = math.sqrt(vx * vx + vz * vz)
	-- The ground probe (see GROUND_SNAP): on while he was on the ground a moment ago and has not jumped; its length is the engine's: the move of the frame is added by the body's snap mode (attachPlayer).
	state.sinceGround = grounded and 0 or (state.sinceGround or SNAP_MEMORY_TICKS + 1) + 1
	local snapping = state.sinceGround <= SNAP_MEMORY_TICKS and not state.jumped
	sf.setGroundSnap(entity, snapping and GROUND_SNAP or 0)

	local pressed = controlled and sf.key("SPACE")
	if replay ~= nil then pressed = replay.a end
	local justPressed = pressed and not state.spaceHeld
	state.spaceHeld = pressed

	-- Heading. External (FUN_80093af8 and FUN_80093ba0): with the stick pushed it moves towards the stick's direction by min(rate x dt, 1) of the way; the rate is TURN_SLOW up to a speed of 150, falling to TURN_FAST at 300. In the air the rate is
	-- AIR_TURN, or JUMP_TURN while jumping. Hypothesis: the way is the short one (FUN_8040f314 is read as an angle interpolation).
	local heading = state.heading
	local headingBefore = heading
	local stickAngle = hasStick and math.atan(sz, sx) or nil
	local cosine = 0
	if hasStick then
		cosine = sx * math.cos(heading) + sz * math.sin(heading)
		if planarBefore >= COSINE_DEAD_MIN_SPEED then
			cosine = math.cos(math.max(0, math.acos(math.max(-1, math.min(1, cosine))) - COSINE_DEAD_ANGLE))
		end
		local rate
		if grounded then rate = turnRate(speed)
		elseif state.jumped then rate = JUMP_TURN
		else rate = AIR_TURN end
		heading = heading + wrapAngle(stickAngle - heading) * math.min(rate * dt, 1)
	end
	state.heading = heading

	local acceleration = 0
	local animSpeed
	local k = GROUND_DRAG
	local startJump = grounded and justPressed
	if startJump then
		-- The jump's start. External (Jump Enter, FUN_800877c0) and observed (live log, two jumps): the jump direction is the ground's normal (the up axis if the vector is shorter than 0.01); the part of the velocity along it is replaced by JUMP_SPEED
		-- (vy read exactly 100 on the start frame with the planar speed unchanged; on a slope of normal (0, 0.8813, -0.4725) the jump went 25 degrees forward), and nothing else happens that frame (no integration: the next row has the first step).
		local nx, ny, nz = sf.groundNormal(entity)
		if nx * nx + ny * ny + nz * nz < 0.0001 then nx, ny, nz = 0, 1, 0 end
		local along = vx * nx + vy * ny + vz * nz
		vx, vy, vz = vx + (JUMP_SPEED - along) * nx, vy + (JUMP_SPEED - along) * ny, vz + (JUMP_SPEED - along) * nz
		state.jumped, state.holdTime, state.jumpGrace, state.jumpPlanar = true, JUMP_HOLD_TIME, JUMP_GRACE_TICKS, planarBefore
		state.jumpDirX, state.jumpDirY, state.jumpDirZ = nx, ny, nz
		state.dashed, state.dashTime = false, nil
		state.speed = MIN_SPEED
	elseif grounded then
		-- The acceleration's length. External (FUN_800831d8, FUN_80082c48): with the stick pushed, the target is MIN_SPEED + k (MAX_SPEED - MIN_SPEED) with k = (cosine - COSINE_START) / (1 - COSINE_START) when he moves at FAST_SPEED or more
		-- and the cosine is at least COSINE_START, else 0; the speed grows by cosine^8 x dt x ACCEL_SHARE x (MAX_SPEED - MIN_SPEED) up to the target. With no stick it follows his speed along the line measured in the original (see NO_STICK_FULL_SPEED) and the acceleration is zero.
		state.speed = state.speed or MIN_SPEED
		local range = MAX_SPEED - MIN_SPEED
		-- Landing into motion. Observed (live log, three landings): the Ground behavior's current speed starts at 3972.6, 4614.8 and 1932.5 for planar speeds of about 160, 143 and 31 (the first landing came after a dash), not at MIN_SPEED, so with the stick held he
		-- runs on instead of dropping to the speed 1000 / 20 = 50 that MIN_SPEED allows. Hypothesis, not decoded: the value is the no-stick line of NO_STICK_FULL_SPEED taken at the planar speed of the landing (gives 4900 and 1840 for the last two; the first, 6800 at 222, is
		-- off). With no stick the speed falls by the drag as before.
		if state.wasAir then
			state.speed = planarBefore >= FAST_SPEED and math.min(planarBefore / NO_STICK_FULL_SPEED, 1) * range + MIN_SPEED or MIN_SPEED
			-- Landing with no stick. Observed (tools/live_run.csv, one landing after a jump dash at planar 146): on the frame he reaches the ground height the whole velocity becomes exactly (0, 0, 0) and stays so until Idle. Hypothesis: any landing
			-- with no stick does that (one recording); with the stick held the speed carries on as above.
			if not hasStick then vx, vz = 0, 0 end
		end
		if hasStick then
			local share = 0
			if speed >= FAST_SPEED and cosine >= COSINE_START then share = (cosine - COSINE_START) / (1 - COSINE_START) end
			local target = share * range + MIN_SPEED
			local grown = cosine ^ 8 * dt * range * ACCEL_SHARE + state.speed
			-- The stick's length. Observed (tools/live_half.csv, stick held at 0.17 to 0.95 of its range; a simulation of this rule on the recorded stick matched the original's planar speed within 1 to 3 at 1.2 to 4.4 s): the pad's float stick is
			-- (|raw| - 15) / 42 per axis, i.e. a dead zone of 15 and full at 57; the acceleration is the current speed times the stick's length (speeds settled at 50 x length: 33.33 at 0.667, 28.57 at 0.571), and while the length is below
			-- STICK_FULL (0.9, the external STICK_DECAY condition) the current speed is multiplied by STICK_DECAY each update down to MIN_SPEED instead of growing (the speed took off at 0.929 and not at 0.881).
			if mag < STICK_FULL then state.speed = math.max(state.speed * STICK_DECAY, MIN_SPEED)
			else state.speed = math.min(grown, target) end
			acceleration = math.min(state.speed, MAX_SPEED) * mag
		else
			if speed >= FAST_SPEED then state.speed = math.max(math.min(speed / NO_STICK_FULL_SPEED, 1), 0) * range + MIN_SPEED
			else state.speed = MIN_SPEED end
		end
		-- Compute planar acceleration components along the facing direction
		local ax = acceleration * math.cos(heading)
		local az = acceleration * math.sin(heading)

		local ex = math.exp(-k * dt)
		local nx, ny, nz = sf.groundNormal(entity)
		local planar2 = vx * vx + vz * vz

		vx = ax / k + (vx - ax / k) * ex
		vz = az / k + (vz - az / k) * ex
		-- On the ground his velocity is kept in the plane of the surface under him, so a slope is climbed instead of ground into (the same rule as Sonic's script).
		if ny > 1e-3 then
			local planeVy = -(nx * vx + nz * vz) / ny
			if vy <= math.max(planeVy, 0) + 1 then vy = planeVy end
		end
		-- The speed the animation reads: the length of his velocity in the plane of the surface, before the ground press below is added (the original's stored velocity has no part into the ground: its collision removes it; the press made a standing
		-- Shadow read 11.3 here and the ground blend start at 0.9 of the rate with no speed at all, observed in the animation log).
		animSpeed = math.sqrt(vx * vx + vy * vy + vz * vz)
		-- The ground gravity (see GROUND_GRAVITY): added to the velocity as the drag integrates it, along the surface's normal at high speed, else straight down.
		local press = GROUND_GRAVITY / k * (1 - ex)
		if planar2 > 250 * 250 and ny > 1e-3 then vx, vy, vz = vx + press * nx, vy + press * ny, vz + press * nz
		else vy = vy + press end
		state.jumped, state.holdTime = false, 0
		state.dashed, state.dashTime = false, nil
	else
		-- In the air. External (Fall, Jump, FUN_80093728): the acceleration is AIR_ACCEL along the stick in the ground plane, drag AIR_DRAG_SIDE on that plane and AIR_DRAG_UP on the up axis; while the jump button is held for at most
		-- JUMP_HOLD_TIME seconds after the jump, JUMP_HOLD_ACCEL is added along the jump direction (observed: the boost ran exactly while the button was held, vy followed a net -50 until the release and -320 after; the button's release ends it).
		-- The gravity is the engine's (placeholder).
		state.speed = MIN_SPEED
		local ax, az = sx * AIR_ACCEL, sz * AIR_ACCEL
		local up = AIR_GRAVITY
		-- The jump dash (JumpDash, vtable 0x80520850). Trigger: hypothesis, the jump button pressed again (a new press) in the air after a jump, once per time in the air; the log shows two dashes, each some frames after the hold ended, and the command
		-- that starts it is not read. Starting (external, FUN_800882a4, and observed): the velocity gets DASH_SPEED along the facing added once (the facing before this frame's turn); then every tick of the dash the acceleration gets
		-- DASH_PUSH_ACCEL along the facing (while the second countdown runs), the stick's AIR_ACCEL as in the air, the heading turns at JUMP_TURN, the vertical is the air's with no hold boost; after DASH_TIME it is the Fall.
		if justPressed and state.jumped and not state.dashed and state.dashTime == nil then
			state.dashTime, state.dashPush, state.dashed, state.holdTime = DASH_TIME, DASH_PUSH_TIME, true, 0
			vx, vz = vx + DASH_SPEED * math.cos(headingBefore), vz + DASH_SPEED * math.sin(headingBefore)
			state.dashFacing = headingBefore
		end
		if state.dashTime ~= nil then
			state.dashPush = state.dashPush - dt
			if state.dashPush >= 0 then
				ax, az = ax + DASH_PUSH_ACCEL * math.cos(state.dashFacing), az + DASH_PUSH_ACCEL * math.sin(state.dashFacing)
			end
			state.dashTime = state.dashTime - dt
			if state.dashTime <= 0 then state.dashTime, state.jumped = nil, false end
		elseif state.jumped and (state.holdTime or 0) > 0 and pressed then
			ax, az, up = ax + JUMP_HOLD_ACCEL * (state.jumpDirX or 0), az + JUMP_HOLD_ACCEL * (state.jumpDirZ or 0), up + JUMP_HOLD_ACCEL * (state.jumpDirY or 1)
			state.holdTime = state.holdTime - dt
		elseif state.jumped then
			state.holdTime = 0
		end
		local ex = math.exp(-AIR_DRAG_SIDE * dt)
		vx = ax / AIR_DRAG_SIDE + (vx - ax / AIR_DRAG_SIDE) * ex
		vz = az / AIR_DRAG_SIDE + (vz - az / AIR_DRAG_SIDE) * ex
		vy = up / AIR_DRAG_UP + (vy - up / AIR_DRAG_UP) * math.exp(-AIR_DRAG_UP * dt)
	end

	-- Animation. The kind of state the game's animation is in (see animation.lua): in the air, the jump's motions while he rises after a jump and the fall otherwise; on the ground, the landing after an air state (until its motion is over), standing still with
	-- no stick (below IDLE_SPEED, a placeholder: the original goes back to Idle at a speed of about 0.2 and was in Ground at 10), braking (a placeholder rule, not measured) or the ground blend. The contact is debounced so that a flicker of it does not change the state.
	if grounded then
		state.groundTicks, state.airTicks = (state.groundTicks or 0) + 1, 0
	else
		state.groundTicks, state.airTicks = 0, (state.airTicks or 0) + 1
	end
	if state.animGrounded == nil then state.animGrounded = true end
	if state.animGrounded and state.airTicks >= AIR_ANIMATION_TICKS then state.animGrounded = false
	elseif not state.animGrounded and state.groundTicks >= LAND_ANIMATION_TICKS then state.animGrounded = true end
	if state.jumped and not grounded then state.animGrounded = false end
	local planar = math.sqrt(vx * vx + vz * vz)
	if state.wasAnimGrounded == false and state.animGrounded then state.landing = true end
	-- Hypothesis, not measured: a stick pushed ends the landing, so he runs on at once (the original's landing was recorded with no stick; with it held the log of this port showed the whole landing motion playing while he ran at 100).
	if state.landing and hasStick then state.landing = false end
	state.wasAnimGrounded = state.animGrounded
	local motion
	if state.animGrounded then
		if state.landing then motion = "landing"
		elseif hasStick and cosine < -0.5 and planar >= BRAKE_SPEED then motion = "brake"
		elseif not hasStick and planar < IDLE_SPEED then motion = "idle"
		else motion = "ground" end
	else
		state.landing = false
		motion = state.dashTime ~= nil and "dash" or (state.jumped and vy > 0 and "jump" or "fall")
	end
	if ShadowAnimation.update(entity, state, motion, {speed = animSpeed or math.sqrt(vx * vx + vy * vy + vz * vz), jumpPlanar = state.jumpPlanar or 0}) then state.landing = false end
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
	sf.setVelocity(entity, vx, vy, vz)
	ShadowReplay.log(entity, motion)
end)

-- Called by main.lua's attachPlayer for the Shadow entity.
function attachShadowPlayer(entity)
	local state = sf.state(entity)
	state.speed = MIN_SPEED
	sf.give(entity, "ShadowPlayer")
	sf.setWeight(entity, 0)
	-- His ground snap is the game's probe (PhysicsBody::snapByRay): a ray along minus the last floor's normal from his center, 2.37 times his sphere radius (5.0 in the live record) plus the frame's move long, accepting a surface within 80 degrees of that normal.
	sf.setGroundSnapMode(entity, {ray = true, radius = 5.0, minNormalY = math.cos(math.rad(80))})
	sf.setUncullable(entity, true)
end
