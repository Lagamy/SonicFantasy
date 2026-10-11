-- Sonic. Outside the debug mode (sf.debugMode, editor mode; Play button) W A S D steer him relative to where the camera looks, Space jumps (and homes in the air), CTRL spin dashes; in the debug mode those keys fly the camera and
-- he ignores them. Only the active player (state.active, main.lua) reads the keys. The engine's one physics system does the moving and the colliding (and the gravity): this only sets his velocity from what it reports of the last move (sf.grounded, sf.groundNormal).
-- state.lock = ticks the control is locked for (a launch), state.invulnerable = ticks left (objects.lua), state.heading = where he faces (radians, forward = (cos, sin) over x and z, like the game's BAMS heading).
-- External (_Main.rel, the per-character record of Sonic at 0x2C61E8 and the shared movement code, see the notes in the plan of 2026-10-05; the game's values are per frame, converted with 1 frame = 1 tick = 1/60 s,
-- a hypothesis): the ground and air rules below. Marked "hypothesis" or "placeholder" where they are not decoded. Animation (FUN_805a99f0, FUN_804cf4f4 of the decompile, decoded 2026-10-10; see "Animation" in docs/SADX_SONIC_REFERENCE.md): on the ground the motion is picked by the forward speed plus 5 times the part of the
-- acceleration the grip limit cut (groundStep's excess) and plays at a per-motion value times that speed plus 20 times the excess. Placeholders / hypotheses: Damage while state.damaged, Spring Fall after a spring; the jump uses the curled model and the spin dash and
-- homing the jumpball model (external, the game's animation table pairs each motion with a model); every state with the ball flag (jump, charge, roll, homing flight) blinks between the two (see "Ball blink"; the roll after a release has no motion of its own yet, the running ones by speed, blinking to the ball). The ground step (groundStep: turns, acceleration with the grip limit, slope pull, drag, stopping) follows FUN_804e4e00 / FUN_804dc9ec / FUN_804dd3f8 / FUN_804dcfa8 /
-- FUN_804de5a0 as decoded 2026-10-10 (docs/SADX_SONIC_REFERENCE.md); not run against the game yet. Not ported on the ground: the surface multiplier, the surface flags, wall running, the rail. The brake state 0xD is the behaviour's (curledStep with skid). The air step (airStep:
-- FUN_804e38a8), the jump (FUN_805a70ac, FUN_804e6064), the spin dash (FUN_805a4340, FUN_805a3eb8, FUN_804df428), the roll (FUN_804decb0) and the homing attack (state 0xE: FUN_805a8958, FUN_805a8310) are decoded the same
-- day, also not run. Not ported: the Light Speed Attack (state 6, FUN_805a76c0: the old homing here was its rules), the tilt of his frame in the air, the object he stands on pushing him.

local FRAMES_PER_SECOND = 60
local JUMP_SPEED = 1.66 * FRAMES_PER_SECOND             -- external
local GROUND_ACCEL = 0.05                               -- external, units per frame squared (+0x3c)
local GROUND_ACCEL_FAST_FACTOR = 0.4                    -- external: from FUN_804e4e00, the acceleration at a forward speed of FAST_SPEED or more (with the ground normal's y >= 0)
local ANALOG_MID_FACTOR = 0.3                           -- external (FUN_804e4e00, DAT_8065cd18): the acceleration's factor from RUN_SPEED up to JOG_SPEED with the stick's magnitude at ANALOG_MID_LIMIT or less
local ANALOG_MID_LIMIT = 0.9                            -- external (FUN_804e4e00, DAT_8065cd70)
local ANALOG_STOP_LIMIT = 0.5                           -- external (FUN_804e4e00, DAT_8065cccc): with the stick's magnitude at this or less the acceleration stops at ANALOG_STOP_FRACTION times WALK_SPEED
local ANALOG_STOP_FRACTION = 0.4                        -- external (FUN_804e4e00, DAT_8065cd40)
local STORED_SPEED_MIN = 0.01                           -- hypothesis (the game's constant DAT_8065cd60 is 0.02, perhaps compared against twice the frame's step), bracket 0.0093 to 0.0111 per frame (recording sadx_2, 2.94 to 2.98 s): under it, with the stick pushed, the stored forward speed stays 0
local FAST_SPEED = 3.0                                  -- external (+0x10): above it the forward drag acts on the excess only
local AIR_ACCEL = 0.031                                 -- external (+0x40), doubled while falling (FUN_804e38a8)
local AIR_BRAKE = -0.17                                 -- external (+0x4c): the air acceleration when the stick is more than 135 degrees from his heading and forward speed > RUN_SPEED
local RUN_SPEED = 1.39                                  -- external (+0x28)
local WALL_STOP_SPEED = 15.0                            -- external (FUN_804dac14, DAT_8065ce40): per frame; at this forward speed or more a wall in front stops him dead
local WALL_HEAD_ON_SPEED = 3.7                          -- external (FUN_804dac14, record +0x30): per frame; at this speed or more a head-on wall zeroes his horizontal velocity
local WALL_HEAD_ON_COS = -0.98                          -- external (FUN_804dac14, DAT_8065ce6c): the velocity direction's cosine to the wall's normal that counts as head-on
local WALL_TURN_AWAY = 0.75 * math.pi                   -- external (FUN_804db674: 0x6000 of 0x10000): a stored turn target further than this from the heading flips it by half a turn when it slides
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
local SPIN_DASH_MIN_CHARGE = 2.0                        -- external (FUN_805a4340), units per frame: the charge starts at the larger of this and his forward speed
local SPIN_DASH_CHARGE_STEP = 0.4                       -- external (FUN_805a3eb8), units per frame, per tick held
local SPIN_DASH_MAX_CHARGE = 10.0                       -- external (FUN_805a3eb8): the step is added only while the charge is under this, so the last step ends above it (10.4 from 2.0 in the game's floats)
local SPIN_DASH_MAX_TICKS = 300                         -- external (FUN_805a3eb8): a charge held this long is dropped without a launch
local ROLL_SLOPE_DRAG = -0.0002                         -- external (FUN_804decb0, constant DAT_8065ceb8): the forward drag of the rolling ball while the ground's normal y is up to ROLL_FLAT_NORMAL
local ROLL_FLAT_NORMAL = 0.98                           -- external (FUN_804decb0, DAT_8065ceb0 as a double): above it the rolling ball's drag is DRAG_FORWARD_GROUND
local BRAKE_FORCE = -0.18                               -- external (+0xe4 of the record, FUN_804df428): the deceleration against his speed while the spin dash charges
local GRIP_ROLL = -0.1                                  -- external (+0xfc, FUN_804df428): times the pull into the surface, the share the charge's forward deceleration loses
local SKID_FRAMES = 36                                  -- external (state 0xD, FUN_805aa66c, FUN_804cf4f4, table entry 0x14): the frames it lasts when nothing stops it, derived: the motion's 4 blend frames and 32 more (33 frames, 1 a frame)
local LANDING_SKID_FRAMES = 1                           -- external (FUN_805aa66c, state 8 and others): a landing into state 0xD writes another motion than 0x14, so the next frame leaves it
local TURN_AROUND_FRAMES = 18                           -- external (FUN_805aa66c state 0xD, FUN_804cf4f4, table entry 0x1E): the frames after the skid's stop until the Turn Around motion (16 frames, 0.9 a frame) has ended, derived
local TURN_AROUND_RATE = 0.9                            -- external (table entry 0x1E): the Turn Around motion's frames a game frame
-- Standing poses (table entries 0x1 Land after a fall, 0x2 Alternate Stand after a jump, 0x4 and 0x5 the idle waiting, 0x1D the roll's end; FUN_805a3e58, FUN_805a9c28, FUN_804cf4f4). A motion that plays once ends at its
-- last frame (frame >= count - 1, at the table's rate) after its blend; its value is the game frames from the change to the end, derived: blend frames (1 / step) + (frames - 1) / rate. Land 4 + 19 / 0.25, Wait A the same,
-- Wait B 4 + 38 / 0.25, Roll End 16 + 15 / 1. The entry after each is the table's next id (Land to Alternate Stand, Wait A to Wait B, Roll End to Land).
local POSE_END = {rollEnd = 31, landFall = 80, waitA = 80, waitB = 156, waitBack = 81}
local POSE_NEXT = {rollEnd = "landFall", landFall = "standAlt", waitA = "waitB", waitB = "waitBack"}
local POSE_RATE = {standAlt = 0.25, landFall = 0.25, waitA = 0.25, waitB = 0.25, waitBack = 0.25, rollEnd = 1.0} -- external (table +0xC); waitBack (id 6) plays backwards: its rate is sent negative
local IDLE_WAIT_FRAMES = 0x708                          -- external (FUN_805a9c28): the state 1 steps (work+0xE) after which the idle waiting (Wait A) starts, 1800 = 30 s at 60 a second
local HOMING_SPEED = 5.0                                -- external (FUN_805a8958, FUN_805a8310): units per frame
local HOMING_CONE = 0x5000 / 0x10000 * 2 * math.pi      -- external (FUN_805a8310): a target must be within this of his heading, radians
local HOMING_TURN = 0x800 / 0x10000 * 2 * math.pi       -- external (FUN_805a8310): the most his heading turns towards the target a frame
local HOMING_RANGE = 100                                -- external (FUN_8041e060): a target is listed when the squared distance is under 10000 and its collision record has the flag 0x40; the game measures from the entity positions
local HOMING_DECAY = 0.9                                -- external (FUN_805a8310): the forward speed's factor a frame once he no longer tracks a target
local HOMING_END_COUNT = 6                              -- external (FUN_805aa66c, state 0xE): the counter (work+0x80) from which the attack ends in the fall
local HOMING_MAX_FRAMES = 359                           -- external (FUN_805aa66c): the frame counter (work+0x82) above which the attack ends in the jump
local HOMING_LONG_FRAMES = 180                          -- external (FUN_805a8310): past this many frames the speed is scaled by 0.7 + 0.1 x a random number in [0, 1)
local STEEP_TURN_STEP_BALL = 0xC00 / 0x10000 * 2 * math.pi -- external (FUN_804de5a0): the same for the ball (his curled states)
local AIR_LEVEL_SPEED = 5.09                             -- observed (sadx_1) + decompile (FUN_804dd7a0, w3+0xd0): his 3D speed a frame at or under which the tilt of his frame levels out
local KEPT_VELOCITY_TOLERANCE = 0.01                    -- units per second: how far the engine's horizontal velocity may be from the one he handed it for his own ground speeds to stand
local PLANE_CHANGED_EPSILON = 1e-6                       -- dot product of two ground normals under 1 minus this: another plane (the same triangle gives the same normal)
local ROLL_GAP_TICKS = 8                               -- placeholder: ticks without ground contact the charge and the roll survive (the engine's ground contact flickers on uneven terrain)
local AIR_ANIMATION_TICKS = 8                           -- placeholder: ticks without ground contact before the air motions play (the engine's ground contact flickers on uneven terrain)
local CONTACT_WINDOW = 8                                -- placeholder: see LAND_CONTACTS
local LAND_CONTACTS = 4                                 -- placeholder: ticks with ground contact among the last CONTACT_WINDOW before the air motions give way to the ground ones
local WALK_SPEED = 0.46                                 -- external (+0x24): the speeds (per frame) up to which the game plays Walk, Speedwalk (up to +0x28), Jog (+0x2c) and Run (+0x34); Sprint above
local SPEEDWALK_SPEED = 1.39
local JOG_SPEED = 2.3
local RUN_MAX_SPEED = 5.09
local PROBE_REACH_SLOW = 0.1                           -- decompile (FUN_804d83c0): the second ground sphere (centre 4.0 above his position, radius 4.1) reaches this far below his feet at WALK_SPEED or less
-- The decompile's value is 1.1 (radius 5.1). Raised to 1.5 at the user's request (2026-10-11, not run): a slow roll up the 14.7 degree ramp of sadx_4 (4.5 per frame, rising 1.2 a tick) left the floor at the crest with an
-- overshoot of 0.99 against a reach of 0.98, where the original is put down on the plateau. A ramp of that slope needs about 0.26 x speed, so 1.5 covers it up to RUN_MAX_SPEED. Hypothesis: the original's real reason is unknown.
local PROBE_REACH_FAST = 1.5                           -- the same at RUN_MAX_SPEED or more; linear between
local GRAVITY = 0.08                                    -- external (+0x114), units per frame squared: the engine's gravity (-288 a second squared) is the same
local FRICTION_LIMIT = -0.2825                          -- external (+0x104): the ground's grip: the forward acceleration on the ground can't exceed -this times the gravity's pull into the surface (0.0226 on flat ground)
local FRICTION_SIDE = -0.6                              -- external (+0x100): the sideways slide's friction, times the pull into the surface
local WIDE_TURN_STEP = 0x100 / 0x10000 * 2 * math.pi    -- external (FUN_804dcfa8): the most the heading turns a frame in the gentle turn
local MID_SPEED = (WALK_SPEED + RUN_SPEED) / 2                 -- external (0.5 times +0xc0 + +0xc4): from here a turn of more than 22.5 degrees brakes
local STAND_TURN_STEP = 0x2000 / 0x10000 * 2 * math.pi  -- external (FUN_804e4e00): the most the heading turns a frame when he stands still
local STEEP_TURN_STEP = 0x600 / 0x10000 * 2 * math.pi   -- external (FUN_804de5a0): per unit of the sideways pull of gravity, no stick on a slope steeper than 45 degrees, when not curled
local FORCE_DEAD = 0.051                                -- external (FUN_804e4e00): the acceleration below which the resting rules zero it
local SNAP_SPEED = 0.87 * WALK_SPEED                      -- external (FUN_804dc9ec): without the stick a speed under this stops
-- Playback rate. External (FUN_804cf4f4 and the animation table at _Main.rel 0x3544E0, confirmed on a RAM dump of the running game): each game frame the motions of table modes 9 and 10 advance by a per-motion value times
-- a speed. Walk, Speedwalk, Jog, Run and the curled Jump use work[0] (mode 10): the forward speed plus 20 times the excess (what FUN_805a99f0 writes there, the spin dash's charge while it charges); Sprint and the roll use the
-- forward speed (mode 9).
local RATES = {walk = 3.4, speedwalk = 1.4, jog = 0.8, run = 0.5, sprint = 0.5}
-- Blend steps. External (the animation table at _Main.rel 0x3544E0, confirmed on a RAM dump: entry +8; FUN_804cf4f4 mode 2): the share of the way from the old pose to the new motion's first frame added each game frame on a change
-- to that motion; 1.0 is a cut. The jump entry and the roll entry (the same motion data) have 0.25, the ball (the blink, sf.blink) 0.5 but it is not a change of the wanted motion, so it does not blend.
local BLEND_STEPS = {stand = 1 / 16, walk = 0.25, speedwalk = 0.25, jog = 0.25, run = 0.25, sprint = 0.5, jump = 0.25, roll = 0.25, springFall = 0.125, fall = 0.25, landing = 1.0, brake = 0.25, damage = 1.0, turnAround = 1.0,
	standAlt = 0.25, landFall = 0.25, waitA = 0.25, waitB = 0.25, waitBack = 0.25, rollEnd = 0.0625}
local JUMP_RATE = 0.2                                      -- external (table entry 0xE: the jump and the charge): times work[0]
local ROLL_RATE = 0.7                                      -- external (table entry 0xF: the roll): times the forward speed
local PICK_EXCESS_FACTOR = 5.0                             -- external (FUN_805a99f0, DAT_8065efbc): times the excess, added to the forward speed that picks the ground motion
local RATE_EXCESS_FACTOR = 20.0                            -- external (FUN_805a99f0, DAT_8065f024): times the excess, added to the forward speed in work[0]
local HARD_LANDING_VY = -5.0                               -- external (FUN_805aa66c, state 8): per frame; at his vertical speed this low or lower a landing without the stick and without speed plays Landing, else Stand
local STAND_EPSILON = 1e-3                                 -- the game stands at a forward speed of exactly 0 with no stick (FUN_805a444c); groundStep zeroes the speeds it means to, this only absorbs float noise
local BALL_BLINK_TICKS = 4                              -- observed (your report that 1 was far too fast), against the decompile (1 per 60 Hz step, see "Ball blink"): ticks per step of the game's blink counter
local LANDING_TICKS = 16                                -- external (table entry 0x13: 16 frames, plays once at 1 a frame, blend 1): derived from the table, not observed

-- An angle in (-pi, pi].
local function wrapAngle(angle)
	return angle - 2 * math.pi * math.floor((angle + math.pi) / (2 * math.pi))
end

local function clamp(value, limit)
	return math.max(-limit, math.min(limit, value))
end

-- A number rounded to the game's single precision (the spin dash's charge adds 0.4 to it until it is over 10.0: in doubles the 20th step already is, in floats the 21st).
local function float32(value)
	return (string.unpack("f", string.pack("f", value)))
end

-- The game's list of homing targets (FUN_8041e060, built by the collision manager each frame) is every registered target (objects.lua) whose position is within HOMING_RANGE of his. Returns whether the list is
-- not empty, and the nearest entry within HOMING_CONE of the heading (as FUN_805a8310 picks it: strictly nearer wins, so the first of equals) with the vector to it.
local function homingCandidates(entity, heading)
	local px, py, pz = sf.position(entity)
	local rangeSquared = HOMING_RANGE * HOMING_RANGE
	local listed, best, bestSquared, bx, by, bz = false, nil, rangeSquared, 0, 0, 0
	for target in pairs(homingTargets) do
		if not sf.exists(target) then
			homingTargets[target] = nil
		else
			local tx, ty, tz = sf.position(target)
			local dx, dy, dz = tx - px, ty - py, tz - pz
			local squared = dx * dx + dy * dy + dz * dz
			if squared < rangeSquared then
				listed = true
				if squared < bestSquared and math.abs(wrapAngle(math.atan(dz, dx) - heading)) <= HOMING_CONE then
					best, bestSquared, bx, by, bz = target, squared, dx, dy, dz
				end
			end
		end
	end
	return listed, best, bx, by, bz
end

-- How far below his feet the game still finds the floor (FUN_804d83c0 of the decompile): its second sphere's lowest point is PROBE_REACH_SLOW below the feet at WALK_SPEED forward speed or less, rising linearly to
-- PROBE_REACH_FAST at RUN_MAX_SPEED. The engine's ground snap does the same job (a stepped move straight down by this much, kept only if it ends on a floor, not while rising). Units: forward speed per frame.
local function probeReach(forwardSpeed)
	local t = math.min(math.max((forwardSpeed - WALK_SPEED) / (RUN_MAX_SPEED - WALK_SPEED), 0), 1)
	return PROBE_REACH_SLOW + (PROBE_REACH_FAST - PROBE_REACH_SLOW) * t
end

-- Turns his model to the heading. External (FUN_805a9f90, his draw): the model is drawn turned about Y by 0x8000 - heading, the heading in the game's convention (forward = (cos, sin) over x and z); the matrix is the
-- SDK's right-handed one (hypothesis, as glm's), and the model's own front is taken to be the game's (hypothesis: if he faces backwards or sideways, the offset is here).
local MODEL_YAW_OFFSET = math.pi
local function face(entity, heading)
	sf.turnY(entity, MODEL_YAW_OFFSET - heading)
end

-- The axes of his frame on a surface: forward = the heading laid into the plane of the normal, side = forward x normal (flat: (-sin, 0, cos) of the heading, as everywhere here). The game rotates his local speeds by the
-- surface's tilt and the heading (FUN_804dc9ec), so these are its local x and z.
local function groundAxes(angle, nx, ny, nz)
	local hx, hz = math.cos(angle), math.sin(angle)
	local along = hx * nx + hz * nz
	local fx, fy, fz = hx - along * nx, -along * ny, hz - along * nz
	local length = math.sqrt(fx * fx + fy * fy + fz * fz)
	if length < 1e-6 then fx, fy, fz, length = hx, 0, hz, 1 end
	fx, fy, fz = fx / length, fy / length, fz / length
	return fx, fy, fz, fy * nz - fz * ny, fz * nx - fx * nz, fx * ny - fy * nx
end

-- The heading moved towards the target the way FUN_804dd3f8 does: by an eighth of the difference below 22.5 degrees, a quarter below 45, else by 11.25 degrees.
local function tightTurn(heading, target, frames)
	local difference = wrapAngle(target - heading)
	local size = math.abs(difference)
	local step = size < TURN_SLOW_ANGLE and size / 8 or size < TURN_MID_ANGLE and size / 4 or TURN_MAX_STEP
	return heading + (difference >= 0 and 1 or -1) * math.min(step * frames, size)
end

-- His forward and side speeds in the surface's plane (units per frame) from the world velocity (units per second), and the plane's axes. The velocity he had is expected to be in the surface plane; the engine's own
-- gravity of the last tick (weight 1, the same 0.08 a frame squared) is taken out of it, the game's rules add theirs (with their multipliers and their limit).
local function planeSpeeds(heading, nx, ny, nz, vx, vy, vz, frames)
	local fx, fy, fz, sx, sy, sz = groundAxes(heading, nx, ny, nz)
	local forward = (vx * fx + vy * fy + vz * fz) / FRAMES_PER_SECOND + GRAVITY * fy * frames
	local side = (vx * sx + vy * sy + vz * sz) / FRAMES_PER_SECOND + GRAVITY * sy * frames
	return forward, side, fx, fy, fz, sx, sy, sz
end

-- The wall in front on the ground (FUN_804dac14, FUN_804db674; docs/SADX_SONIC_REFERENCE.md "FUN_804dac14 wall branch"). The engine's contact has already cut the velocity into the wall, so the velocity he wrote last tick
-- (kept) is the one the game's rules see; dot is its direction against the wall's normal. External: forward speed WALL_STOP_SPEED or more stops him dead; WALL_HEAD_ON_SPEED or more and head-on zeroes the horizontal velocity;
-- under RUN_SPEED and moving into the wall he slides: the whole speed along the wall's tangent in the ground plane (the cross of the two normals, on the side of the velocity), the heading turned by the change of the
-- horizontal direction and half a turn more when the stored turn target is far from it. Not ported: the bounce of the entry flag 0x400 and the push of the hit flag 0x2000 (both under RUN_SPEED), the turn around a round
-- obstacle at RUN_SPEED or more (needs the entries' type and size bytes). Returns the new velocity (the engine's gravity of the tick lacks in it), the heading, and whether it stopped.
local function wallHit(state, heading, kept, wx, wy, wz, nx, ny, nz, frames)
	local speed = kept.forward
	local length = math.sqrt(kept.vx * kept.vx + kept.vy * kept.vy + kept.vz * kept.vz)
	if length < 1e-6 then return nil end
	local dot = (kept.vx * wx + kept.vy * wy + kept.vz * wz) / length
	local gravity = GRAVITY * FRAMES_PER_SECOND * frames
	if speed >= WALL_STOP_SPEED then
		return 0, -gravity, 0, heading
	elseif speed >= WALL_HEAD_ON_SPEED and dot < WALL_HEAD_ON_COS then
		return 0, kept.vy - gravity, 0, heading
	elseif speed < RUN_SPEED and dot < 0 then
		local tx, ty, tz = wy * nz - wz * ny, wz * nx - wx * nz, wx * ny - wy * nx
		local tangent = math.sqrt(tx * tx + ty * ty + tz * tz)
		if tangent < 1e-6 then return nil end
		local sign = (tx * kept.vx + ty * kept.vy + tz * kept.vz) >= 0 and 1 or -1
		local k = sign * length / tangent
		local vx, vy, vz = tx * k, ty * k, tz * k
		if vx * vx + vz * vz > 1e-6 and kept.vx * kept.vx + kept.vz * kept.vz > 1e-6 then
			heading = heading + wrapAngle(math.atan(vz, vx) - math.atan(kept.vz, kept.vx))
			if state.turnTarget ~= nil and math.abs(wrapAngle(state.turnTarget - heading)) > WALL_TURN_AWAY then heading = heading + math.pi end
			state.turnTarget = heading
		end
		return vx, vy - gravity, vz, heading
	end
	return nil
end

-- His local speeds after the heading turned from heading to newHeading (the end of FUN_804dd3f8): the numbers keep the share keep of themselves a frame, the rest is the old world velocity laid on the new axes.
local function retarget(heading, newHeading, keep, nx, ny, nz, forward, side, frames)
	local fx, fy, fz, sx, sy, sz = groundAxes(heading, nx, ny, nz)
	local wx, wy, wz = fx * forward + sx * side, fy * forward + sy * side, fz * forward + sz * side
	local gx, gy, gz, tx, ty, tz = groundAxes(newHeading, nx, ny, nz)
	local kept = keep ^ frames
	return kept * forward + (1 - kept) * (wx * gx + wy * gy + wz * gz), kept * side + (1 - kept) * (wx * tx + wy * ty + wz * tz)
end

-- The integration of the local speeds (FUN_804dc9ec): forward and side speed grow by the accelerations af and as (per frame); a speed that crosses zero stops, and without the stick a speed under SNAP_SPEED on ground
-- flatter than 45 degrees (ny over 0.7; 1 in the air) stops; a side speed under SNAP_SPEED that did not change, or that falls under it, stops.
local function integrateSpeeds(forward, side, af, as, hasWish, ny, frames)
	local before = forward
	forward = before + af * frames
	if before <= 0 then
		if before < 0 then
			if forward <= 0 then
				if not hasWish and forward > -SNAP_SPEED and ny > 0.7 then forward = 0 end
			else
				forward = 0
			end
		end
	elseif forward >= 0 then
		if not hasWish and forward < SNAP_SPEED and ny > 0.7 then forward = 0 end
	else
		forward = 0
	end
	local sideBefore = side
	side = sideBefore + as * frames
	if sideBefore > 0 then
		if sideBefore < SNAP_SPEED and side == sideBefore then side = 0 end
		if sideBefore >= SNAP_SPEED and side < SNAP_SPEED then side = 0 end
	elseif sideBefore < 0 then
		if sideBefore > -SNAP_SPEED and side == sideBefore then side = 0 end
		if sideBefore <= -SNAP_SPEED and side > -SNAP_SPEED then side = 0 end
	end
	return forward, side
end

-- The world velocity (units per second) of the local speeds on the surface (the end of FUN_804dc9ec); without the stick an axis under 0.02 a frame is zeroed.
local function planeVelocity(heading, nx, ny, nz, forward, side, hasWish)
	local gx, gy, gz, tx, ty, tz = groundAxes(heading, nx, ny, nz)
	local wx, wy, wz = gx * forward + tx * side, gy * forward + ty * side, gz * forward + tz * side
	if not hasWish then
		if math.abs(wx) < 0.02 then wx = 0 end
		if math.abs(wy) < 0.02 then wy = 0 end
		if math.abs(wz) < 0.02 then wz = 0 end
	end
	return wx * FRAMES_PER_SECOND, wy * FRAMES_PER_SECOND, wz * FRAMES_PER_SECOND
end

-- One tick of the ground (the running state, FUN_804e4e00 then FUN_804dc9ec of the decompile). External; the stick's magnitude (state.stickMag) scales the acceleration (see "The stick's acceleration"). Not ported: the surface multiplier (+0x3c of the movement block, 1.0), the surface flags 0x400000/0x2 (a deceleration of 0.18 above 0.37), wall running (normal y under 0.1), the rail, the second
-- turn of the standing state, the brake state 0xD (the behaviour runs it through curledStep, not this). Returns the new heading, the forward and side speeds (units per frame), the
-- world velocity (units per second), whether the game would be in the brake state (FUN_804e6144 with WALK_SPEED <= forward speed) and the excess: the part of the stick's acceleration the grip limit cut (w2+0x38 of the
-- game, units per frame squared, 0 when nothing was cut), which the motion pick and the playback rate add to the forward speed.
local function groundStep(state, hasWish, stickAngle, heading, vx, vy, vz, nx, ny, nz, frames)
	local forward, side, fx, fy, fz, sx, sy, sz = planeSpeeds(heading, nx, ny, nz, vx, vy, vz, frames)

	-- The forces in his frame: gravity along forward, sideways and into the surface; steeper ground pulls harder at low speed.
	local af, as, au = -GRAVITY * fy, -GRAVITY * sy, -GRAVITY * ny
	if ny < 0.5 and math.abs(forward) < RUN_SPEED then
		af, as = af * 4.225, as * 4.225
	elseif ny < 0.7 and math.abs(forward) < RUN_SPEED then
		as = as * 2
	elseif ny < 0.87 and forward < WALK_SPEED and forward > -RUN_SPEED then
		as = as * 1.4
	end
	-- Forward drag: with the stick only above FAST_SPEED (1.7 times as strong on nearly flat ground) or backwards, without it on the whole speed above RUN_SPEED. Side drag on the side speed.
	if hasWish or forward <= RUN_SPEED then
		if hasWish and forward > FAST_SPEED and ny > 0.96 then
			af = af + 1.7 * DRAG_FORWARD_GROUND * (forward - FAST_SPEED)
		elseif forward <= FAST_SPEED then
			if forward < 0 then af = af + DRAG_FORWARD_GROUND * forward end
		else
			af = af + DRAG_FORWARD_GROUND * (forward - FAST_SPEED)
		end
	else
		af = af + DRAG_FORWARD_GROUND * forward
	end
	as = as + DRAG_SIDE * side

	-- The stick's acceleration m (FUN_804e4e00): the stick's magnitude times GROUND_ACCEL times a factor of the forward speed. From FAST_SPEED up 0.4 with the surface not upside down. From RUN_SPEED to JOG_SPEED
	-- ANALOG_MID_FACTOR when the magnitude is ANALOG_MID_LIMIT or less. Under WALK_SPEED with a magnitude of ANALOG_STOP_LIMIT or less none from ANALOG_STOP_FRACTION times WALK_SPEED on: a gentle push holds him at a
	-- slow walk. No stick: GROUND_DECEL towards zero, only on ground flatter than 45 degrees (steeper, he slides).
	local m = 0
	if hasWish then
		local mag = state.stickMag or 1
		local factor = 1
		if forward >= FAST_SPEED then
			if ny >= 0 then factor = GROUND_ACCEL_FAST_FACTOR end
		elseif forward >= JOG_SPEED then
			factor = 1
		elseif forward >= RUN_SPEED then
			if mag <= ANALOG_MID_LIMIT then factor = ANALOG_MID_FACTOR end
		elseif forward < WALK_SPEED and mag <= ANALOG_STOP_LIMIT and forward >= ANALOG_STOP_FRACTION * WALK_SPEED then
			factor = 0
		end
		m = GROUND_ACCEL * mag * factor
	elseif ny >= 0.71 then
		if forward > 0 then m = -GROUND_DECEL elseif forward < 0 then m = GROUND_DECEL end
	end

	-- The turn (FUN_804e4e00, FUN_804de5a0). Stick: standing still and more than 22.5 degrees off he turns by up to 45 degrees a frame and does not accelerate; from MID_SPEED up and more than 22.5 off he brakes
	-- (m = -GROUND_DECEL) and turns the normal way; within 22.5 degrees at WALK_SPEED or more he turns the gentle way (WIDE_TURN_STEP, narrower above RUN_MAX_SPEED); otherwise the normal way, with 0.8 of the acceleration
	-- from WALK_SPEED to JOG_SPEED when more than 45 degrees off. The normal turn keeps GROUND_GRIP of his local velocity (0.5 on a steep ground); the gentle one the same. No stick on a slope steeper than 45
	-- degrees: the heading follows the pull of gravity (target = heading + atan2(sideways pull, |forward pull|)), moved by STEEP_TURN_STEP per unit of the sideways pull, and keeps 0.05 of the old local velocity.
	local newHeading, keep = heading, nil
	local turnAway = false
	if hasWish then
		local difference = wrapAngle(stickAngle - heading)
		local size = math.abs(difference)
		state.turnTarget = stickAngle
		turnAway = size > BRAKE_ANGLE and forward >= WALK_SPEED
		keep = ny <= 0.4 and 0.5 or GROUND_GRIP
		if math.abs(forward) < 1e-3 and size > TURN_SLOW_ANGLE then
			m = 0
			newHeading = heading + (difference >= 0 and 1 or -1) * math.min(STAND_TURN_STEP * frames, size)
		elseif forward >= MID_SPEED and size > TURN_SLOW_ANGLE then
			m = -GROUND_DECEL
			newHeading = tightTurn(heading, stickAngle, frames)
		elseif forward < WALK_SPEED or size >= TURN_SLOW_ANGLE then
			if forward >= WALK_SPEED and forward <= JOG_SPEED and size > TURN_MID_ANGLE then m = m * 0.8 end
			newHeading = tightTurn(heading, stickAngle, frames)
		else
			local step = WIDE_TURN_STEP
			if forward > RUN_MAX_SPEED then step = step * (1 - math.sqrt((forward - RUN_MAX_SPEED) * 0.0625)) end
			newHeading = heading + (difference >= 0 and 1 or -1) * math.min(step * frames, size)
		end
	elseif ny < 0.71 and ny > 0 then
		local previous = state.turnTarget or heading
		local pull = math.atan(-sy, math.abs(fy))
		local step = STEEP_TURN_STEP * math.abs(sy) * frames
		local goal = wrapAngle(heading + pull - previous)
		state.turnTarget = previous + math.max(-step, math.min(step, goal))
		keep = 0.05
		newHeading = tightTurn(heading, state.turnTarget, frames)
	end
	if newHeading ~= heading then
		forward, side = retarget(heading, newHeading, keep, nx, ny, nz, forward, side, frames)
	end

	-- The acceleration's limits (FUN_804e4e00, end). The grip limit F caps the forward acceleration the stick gives (the excess is lost); the resting rules zero small forces without a stick. Forward in the order of
	-- the game: standing still / accelerating / braking.
	local limit = au * FRICTION_LIMIT
	local excess = 0
	if math.abs(forward) < 1e-6 then
		af = af + m
		if not hasWish then
			if af < limit and af > -limit then af = 0 end
			if af < FORCE_DEAD and af > -FORCE_DEAD then af = 0 end
		end
	elseif m >= 0 then
		if forward >= 0 or af >= FORCE_DEAD or af <= -FORCE_DEAD then
			if limit <= 0 or m <= limit then
				if hasWish or forward > WALK_SPEED or af >= FORCE_DEAD or af <= -FORCE_DEAD then af = af + m else af = 0 end
			else
				-- FUN_804e4e00: the sum over the limit is stored in w2+0x38 (the excess) and the limit is what accelerates him.
				local sum = af + m
				if sum > limit then excess = sum - limit end
				af = math.min(sum, limit)
			end
		else
			af = af + m
			if af * m < 0 then af = 0 end
		end
	elseif forward > 0 then
		af = af + m
		if af * m < 0 then af = 0 end
	elseif hasWish or af >= FORCE_DEAD or af <= -FORCE_DEAD then
		af = limit + m
		if af >= 0 then af = 0 end
	else
		af = 0
	end
	-- Sideways: standing still sideways, forces under the grip limit vanish; sliding, the friction takes its share off the pull and can't turn it round.
	if math.abs(side) < 1e-6 then
		if as < limit and as > -limit then as = 0 end
	else
		local friction = au < 0 and au * FRICTION_SIDE or 0
		local change = as > 0 and -friction or as < 0 and friction or 0
		local pulled = as + change
		if as ~= 0 and change ~= 0 and pulled * as < 0 then pulled = 0 end
		as = pulled
	end

	forward, side = integrateSpeeds(forward, side, af, as, hasWish, ny, frames)
	local wx, wy, wz = planeVelocity(newHeading, nx, ny, nz, forward, side, hasWish)
	return newHeading, forward, side, wx, wy, wz, turnAway, excess
end

-- One tick of the curled ground (external): the spin dash's charge (FUN_804de8a0, FUN_804df428: the stick turns him, no acceleration, a hard deceleration) when charging, else the rolling ball after the release
-- (FUN_804de8a0, FUN_804decb0: only gravity and a small drag), then FUN_804dc9ec. Both turn him by the stick's normal turn first, with the same blend as the running state; the charge turns once more at the end of its
-- own function (twice a frame). Not ported: the push of the object he stands on (work+0x64), the roll upside down (gravity x -8 above RUN_SPEED when the normal's y is under 0), the surface multiplier. launched = the speed
-- a release gives (only his forward speed; the side speed stays). skid = the brake state 0xD (charging's friction, FUN_804df428 alone: its chain has no FUN_804de8a0 before it, so only the end turn towards the target
-- of the last frames happens, once). Returns what groundStep does, without the brake flag.
local function curledStep(state, charging, hasWish, stickAngle, heading, vx, vy, vz, nx, ny, nz, frames, launched, skid)
	local forward, side, fx, fy, fz, sx, sy, sz = planeSpeeds(heading, nx, ny, nz, vx, vy, vz, frames)
	if launched then forward = launched end

	-- The stick's turn (FUN_804de8a0): target = the stick; without it on a slope under 30 degrees (normal's y under 0.87) the heading follows the pull of gravity as in groundStep (3072 instead of 1536 for the
	-- ball), unless he moves up steeply (his direction's y over 0.86) at more than WALK_SPEED, when the target stays what it was. The blend: the stick 0.99 (0.5 on a normal's y of 0.4 or less), no stick 0.05.
	local keep = 0.05
	local newHeading = heading
	if skid then
		keep = hasWish and (ny <= 0.4 and 0.5 or GROUND_GRIP) or keep
	elseif hasWish then
		state.turnTarget = stickAngle
		keep = ny <= 0.4 and 0.5 or GROUND_GRIP
		newHeading = tightTurn(heading, stickAngle, frames)
	else
		local speed = math.sqrt(vx * vx + vy * vy + vz * vz) / FRAMES_PER_SECOND
		if not (speed > WALK_SPEED and vy / (speed * FRAMES_PER_SECOND) > 0.86) then
			if ny >= 0.87 or ny <= 0 then
				state.turnTarget = heading
			else
				local previous = state.turnTarget or heading
				local pull = math.atan(-sy, math.abs(fy))
				local step = STEEP_TURN_STEP_BALL * math.abs(sy) * frames
				state.turnTarget = previous + clamp(wrapAngle(heading + pull - previous), step)
			end
			newHeading = tightTurn(heading, state.turnTarget, frames)
		end
	end
	if newHeading ~= heading then
		forward, side = retarget(heading, newHeading, keep, nx, ny, nz, forward, side, frames)
		fx, fy, fz, sx, sy, sz = groundAxes(newHeading, nx, ny, nz)
	end

	-- The forces in the new frame: gravity along forward, sideways and into the surface, the drag on the speeds.
	local af, as, au = -GRAVITY * fy, -GRAVITY * sy, -GRAVITY * ny
	as = as + DRAG_SIDE * side
	if charging then
		-- FUN_804df428: forward drag DRAG_FORWARD_GROUND; with a speed, BRAKE_FORCE against it (less GRIP_ROLL times the pull into the surface, never past zero speed); standing still, forces under the grip limit vanish.
		-- Side: the same with BRAKE_FORCE against the side speed and FRICTION_SIDE's share, which depends on the sign of the side force.
		af = af + DRAG_FORWARD_GROUND * forward
		local limit = au * FRICTION_LIMIT
		if math.abs(forward) < 1e-6 then
			if af < limit and af > -limit then af = 0 end
		else
			local grip = au < 0 and au * GRIP_ROLL or 0
			af = af + (forward > 0 and BRAKE_FORCE or -BRAKE_FORCE)
			if af <= 0 then
				if af < 0 then
					af = af + grip
					if af > 0 then af = 0 end
				end
			else
				af = af - grip
				if af < 0 then af = 0 end
			end
			if forward > 0 and -af > forward then af = -forward elseif forward < 0 and -af < forward then af = -forward end
		end
		if math.abs(side) < 1e-6 then
			if as < limit and as > -limit then as = 0 end
		else
			local grip = au < 0 and au * FRICTION_SIDE or 0
			local brake = side > 0 and BRAKE_FORCE or -BRAKE_FORCE
			if as > 0 then brake = brake - grip elseif as < 0 then brake = brake + grip end
			as = as + brake
			if side > 0 and -as > side then as = -side elseif side < 0 and -as < side then as = -side end
		end
		-- The second turn, at the end of FUN_804df428: towards the same target, the same blend.
		local turned = tightTurn(newHeading, state.turnTarget or newHeading, frames)
		if turned ~= newHeading then
			forward, side = retarget(newHeading, turned, keep, nx, ny, nz, forward, side, frames)
			newHeading = turned
		end
	else
		-- FUN_804decb0: forward drag DRAG_FORWARD_GROUND, ROLL_SLOPE_DRAG on a slope (normal's y up to ROLL_FLAT_NORMAL).
		af = af + (ny <= ROLL_FLAT_NORMAL and ROLL_SLOPE_DRAG or DRAG_FORWARD_GROUND) * forward
	end

	forward, side = integrateSpeeds(forward, side, af, as, hasWish, ny, frames)
	local wx, wy, wz = planeVelocity(newHeading, nx, ny, nz, forward, side, hasWish)
	return newHeading, forward, side, wx, wy, wz
end

-- One tick in the air (the jump 8, the fall 0xC and the other free air states, FUN_804e38a8, FUN_804dd3f8 and FUN_804dc9ec of the decompile; external). The engine's gravity is the game's, so only the rest is here: the drag
-- on the three speeds (DRAG_FORWARD_AIR, DRAG_UP, DRAG_SIDE; from their values at the start of the tick) and the stick. Stick: more than 135 degrees off his heading at a forward speed over RUN_SPEED: AIR_BRAKE forward and
-- the turn goes to the target he had (the stick's of the last frames); else the target is the stick; within 22.5 degrees of the heading also AIR_ACCEL forward, doubled while he falls (the engine's y speed under 0).
-- The turn is the normal one with the blend AIR_GRIP; no stick: no turn, the target becomes the heading. Then the integration; without the stick an axis under 0.02 a frame (after the game's gravity) is zeroed.
-- His frame in the air is tilted to the last ground normal (state.tilt; hypothesis, one jump of sadx_1 off a ramp: rot_x matches the ramp's normal for 0.37 s) and the three speeds live on its axes: the drags act on them, so a
-- ramp's jump loses height faster than a level frame would. The tilt levels out towards (0, 1, 0) by TURN_MAX_STEP a frame while his 3D speed is AIR_LEVEL_SPEED a frame or less (FUN_804dd7a0, observed 7.683 and 7.700 s),
-- the world velocity kept. The engine's gravity stays in the world velocity (no part is put back); how the game applies its own in the tilted frame is unknown. Not ported: the aim assist of FUN_804c7e7c (needs a list the stages here don't make). Returns the new heading, the world velocity (units per second) and
-- whether he brakes (the stick far away from his run). grip replaces AIR_GRIP when given (the jump's own tick).
local function airStep(state, hasWish, stickAngle, heading, vx, vy, vz, frames, grip)
	local nx, ny, nz = 0, 1, 0
	local tilt = state.tilt
	if tilt ~= nil then
		nx, ny, nz = tilt[1], tilt[2], tilt[3]
		if vx * vx + vy * vy + vz * vz <= (AIR_LEVEL_SPEED * FRAMES_PER_SECOND) ^ 2 then
			local angle = math.acos(math.min(ny, 1))
			local step = TURN_MAX_STEP * frames
			if angle <= step then
				nx, ny, nz = 0, 1, 0
			else
				local keep, toward = math.sin(angle - step) / math.sin(angle), math.sin(step) / math.sin(angle)
				nx, ny, nz = keep * nx, keep * ny + toward, keep * nz
			end
		end
		state.tilt = ny < 1 and {nx, ny, nz} or nil
	end
	local fx, fy, fz, sx, sy, sz = groundAxes(heading, nx, ny, nz)
	local forward = (vx * fx + vy * fy + vz * fz) / FRAMES_PER_SECOND
	local side = (vx * sx + vy * sy + vz * sz) / FRAMES_PER_SECOND
	local up = (vx * nx + vy * ny + vz * nz) / FRAMES_PER_SECOND
	local af, as, au = DRAG_FORWARD_AIR * forward, DRAG_SIDE * side, DRAG_UP * up
	local newHeading, brake = heading, false
	if hasWish then
		local size = math.abs(wrapAngle(stickAngle - heading))
		local target = state.turnTarget or heading
		if forward > RUN_SPEED and size > BRAKE_ANGLE then
			af = af + AIR_BRAKE
			brake = true
		else
			target = stickAngle
			if size <= TURN_SLOW_ANGLE then af = af + AIR_ACCEL * (vy >= 0 and 1 or 2) end
		end
		state.turnTarget = target
		newHeading = tightTurn(heading, target, frames)
		if newHeading ~= heading then
			forward, side = retarget(heading, newHeading, grip or AIR_GRIP, nx, ny, nz, forward, side, frames)
		end
	else
		state.turnTarget = heading
	end
	forward, side = integrateSpeeds(forward, side, af, as, hasWish, 1, frames)
	local nextUp = up + au * frames
	if not hasWish and math.abs(nextUp - GRAVITY * frames) < 0.02 then nextUp = GRAVITY * frames end
	local gx, gy, gz, tx, ty, tz = groundAxes(newHeading, nx, ny, nz)
	local wx = (gx * forward + tx * side + nx * nextUp) * FRAMES_PER_SECOND
	local wy = (gy * forward + ty * side + ny * nextUp) * FRAMES_PER_SECOND
	local wz = (gz * forward + tz * side + nz * nextUp) * FRAMES_PER_SECOND
	if not hasWish then
		if math.abs(wx) < 0.02 * FRAMES_PER_SECOND then wx = 0 end
		if math.abs(wz) < 0.02 * FRAMES_PER_SECOND then wz = 0 end
	end
	return newHeading, wx, wy, wz, brake
end

-- One tick of the homing attack (state 0xE, FUN_805a8310 of the decompile; external; also run once when it starts, FUN_805a8958). state.homing.count is the game's work+0x80, state.homing.frames its work+0x82.
-- While the count is 0 or less and some target is listed: the nearest one within HOMING_CONE of the heading is chased: the heading turns towards it by HOMING_TURN, and the velocity is HOMING_SPEED along the new
-- heading's horizontal direction scaled to the target's vertical share (y over the distance): so he flies the heading, not straight at it. On the first frame (count under 0) a target above him is not chased:
-- the vertical share is zeroed and the count becomes 1. After 180 frames the speed is scaled by 0.7 + 0.1 x a random number. With a target listed but none in the cone the count becomes 1 and the ground rules
-- (groundStep on a flat normal) run. Otherwise (count over 0, or nothing listed) the count grows and his forward speed is scaled by HOMING_DECAY before the ground rules. Returns the new heading, the world
-- velocity (units per second), the brake flag and whether he chased (the game's gravity does not act then, it sets the velocity: the caller gives the engine's back).
local function homingStep(state, entity, hasWish, stickAngle, heading, vx, vy, vz, frames)
	local homing = state.homing
	local listed, best, dx, dy, dz = homingCandidates(entity, heading)
	if homing.count <= 0 and listed then
		if best ~= nil then
			homing.target = best
			local newHeading = heading + clamp(wrapAngle(math.atan(dz, dx) - heading), HOMING_TURN * frames)
			local length = math.sqrt(dx * dx + dy * dy + dz * dz)
			local rise = length > 1e-6 and dy / length or 0
			if rise > 0 and homing.count < 0 then rise, homing.count = 0, 1 end
			if homing.count < 0 then homing.count = 0 end
			local speed = HOMING_SPEED
			if homing.frames > HOMING_LONG_FRAMES then speed = speed * (0.7 + 0.1 * math.random()) end
			local flat = math.sqrt(math.max(0, 1 - rise * rise)) * speed * FRAMES_PER_SECOND
			return newHeading, math.cos(newHeading) * flat, rise * speed * FRAMES_PER_SECOND, math.sin(newHeading) * flat, false, true
		end
		homing.count = 1
	else
		homing.count = homing.count + 1
		local along = (vx * math.cos(heading) + vz * math.sin(heading)) * (HOMING_DECAY - 1)
		vx, vz = vx + along * math.cos(heading), vz + along * math.sin(heading)
	end
	local newHeading, _, _, wx, _, wz, turnAway = groundStep(state, hasWish, stickAngle, heading, vx, vy, vz, 0, 1, 0, frames)
	return newHeading, wx, vy + DRAG_UP * vy * frames, wz, turnAway, false
end

-- The blink counter's step (docs/SADX_SONIC_REFERENCE.md "Ball blink"): called each tick while the ball flag is set, whatever the motion (the jump, the spin dash charge, the roll, the homing flight); it
-- returns whether the Ball entry is drawn instead of the playing motion this tick. The counter is not touched while the flag is clear, so a later ball goes on from the value left.
local function ballBlinkStep(state)
	state.ballTicks = state.ballTicks or 0
	state.ballSubTicks = (state.ballSubTicks or 0) + 1
	if state.ballSubTicks >= BALL_BLINK_TICKS then
		state.ballSubTicks = 0
		state.ballTicks = state.ballTicks + 1
	end
	return state.ballTicks & 0x11 ~= 0
end

-- The blue sphere of the homing burst and the spin dash aura (sf.spawnDashSphere, docs/SADX_SONIC_REFERENCE.md "Burst and aura timing"). Decompile (FUN_8059dd30, FUN_8059dfb0): it sits at the task position plus (0, 5, 0) turned by his frame, and the
-- task position is SADX_FEET_BELOW_BODY under the engine's body position (observed, START_LIFT in replay.lua), so the offset from the entity is 5 along his up axis minus that. ux, uy, uz = his up axis (the ground normal or the air's tilt).
local DASH_SPHERE_UP = 5.0                                -- decompile (DAT_8065ef10)
local SADX_FEET_BELOW_BODY = 5.3                          -- observed (sadx_1 first run: body y 9.30 for a recorded 4.00)
local function dashSphere(kind, entity, ux, uy, uz, heading)
	sf.spawnDashSphere(kind, entity, DASH_SPHERE_UP * ux, DASH_SPHERE_UP * uy - SADX_FEET_BELOW_BODY, DASH_SPHERE_UP * uz, heading, ux, uy, uz)
end

-- The trail of the roll and the homing flight (sf.spawnDashTrail, docs/SADX_SONIC_REFERENCE.md "Trail geometry and draw state"): one segment per tick between his body centre now and one tick later. Decompile (FUN_8059c148): the centre is the task
-- position plus (0, w3+0x11c, 0) = 5.4 (the dump's collision sphere centre), and the task position is SADX_FEET_BELOW_BODY under the engine's body position, so the offset from the entity is 5.4 minus that.
local TRAIL_CENTER_UP = 5.4                               -- decompile (w3+0x11c)
local function dashTrail(entity)
	sf.spawnDashTrail(entity, 0, TRAIL_CENTER_UP - SADX_FEET_BELOW_BODY, 0)
end

-- The curled Jump's squash (docs/SADX_SONIC_REFERENCE.md "Spin dash ball squash"): while the motion is the Jump's (0xE) and a ground bit is set, the draw (FUN_805a9f90) translates by (0, -1, 0), turns 0x2000 (45 degrees)
-- about Z and scales by (0.7, 1.1, 0.8), in the model's own space after the heading. Decompile; the turn's axis is a hypothesis (the function used is the one the draw gives the +0x1c angle).
local function ballSquash(entity, on)
	if on then sf.setModelTransform(entity, 0, -1.0, 0, math.pi / 4, 0.7, 1.1, 0.8) else sf.setModelTransform(entity) end
end

-- One tick of Sonic. replay = SadxReplay.step()'s input (nil without a replay); returns the name of the motion chosen, for SadxReplay.log.
local function playerStep(entity, replay)
	local state = sf.state(entity)
	state.lock = state.lock or 0
	state.invulnerable = state.invulnerable or 0
	if state.invulnerable > 0 then state.invulnerable = state.invulnerable - 1 end
	if state.lock > 0 then
		state.lock = state.lock - 1
		-- A launch ends the curled and the homing states (the game's spring and dash panel states clear the ball flag, FUN_805a6004).
		state.ball, state.rolling, state.charge, state.homing, state.brake, state.turnFrames = false, false, nil, nil, nil, nil
		local _, lockedVy = sf.velocity(entity)
		sf.blink(entity, false)
		ballSquash(entity, false)
		local lockedMotion = sf.grounded(entity) and "sprint" or lockedVy > 0 and "springFall" or "fall"
		sf.animate(entity, lockedMotion, 0, BLEND_STEPS[lockedMotion])
		return lockedMotion
	end

	-- The camera's heading over the ground, and the way to its right.
	local fx, _, fz = sf.cameraForward()
	local length = math.sqrt(fx * fx + fz * fz)
	if length > 1e-4 then fx, fz = fx / length, fz / length else fx, fz = 0, -1 end
	local rx, rz = -fz, fx
	if state.heading == nil then state.heading = math.atan(fz, fx) end

	-- A replay of a recorded run (replay.lua) takes the place of the keyboard while it runs: its stick is already the world stick, the camera applied.
	local controlled = state.active and not sf.debugMode()
	local wx, wz = 0, 0
	if replay ~= nil then
		wx, wz = replay.wx, replay.wz
	else
		if controlled and sf.key("W") then wx, wz = wx + fx, wz + fz end
		if controlled and sf.key("S") then wx, wz = wx - fx, wz - fz end
		if controlled and sf.key("D") then wx, wz = wx + rx, wz + rz end
		if controlled and sf.key("A") then wx, wz = wx - rx, wz - rz end
	end
	local wishLength = math.sqrt(wx * wx + wz * wz)
	local hasWish = wishLength > 1e-4
	state.stickMag = math.min(1, wishLength)
	local stickAngle = hasWish and math.atan(wz, wx) or nil

	local frames = sf.tickTime() * FRAMES_PER_SECOND
	local vx, vy, vz = sf.velocity(entity)
	-- The velocity as the engine step left it, before this tick changes anything, for the replay log (SadxReplay.log).
	state.logV0x, state.logV0y, state.logV0z = vx, vy, vz
	local grounded = sf.grounded(entity)
	-- The vertical speed of the last tick without ground contact: the game's landing reads its vertical speed (w2+8) before the contact's response, the engine's is already cut when the landing is seen.
	if not grounded then state.airVy = vy end

	-- The buttons. External (FUN_804e60cc, FUN_804e6064, FUN_805a4340, FUN_805a3eb8 of the decompile): the jump, the homing attack and the spin dash start on the press; the jump's hold and the spin dash's charge read the
	-- button held. SPACE is the jump button, CTRL the action button (the game's spin dash button; in the air it uncurls him).
	local pressed = controlled and sf.key("SPACE")
	if replay ~= nil then pressed = replay.a end
	local justPressed = pressed and not state.spaceHeld
	state.spaceHeld = pressed
	local actionHeld = controlled and sf.key("CTRL")
	if replay ~= nil then actionHeld = replay.b end
	local actionPressed = actionHeld and (not state.actionHeld or state.actionPending == true)
	state.actionHeld = actionHeld

	-- His ground contact for the rules. The tick after a jump the engine's last move still reports the ground, he is in the air (the game's jump clears its ground flag at once). The charge and the roll survive fewer than
	-- ROLL_GAP_TICKS ticks without contact (placeholder: the engine's ground contact flickers on uneven terrain), longer and they end, the ball flag staying (the game's jump state).
	local leave = state.leaveTicks or 0
	if leave > 0 then state.leaveTicks = leave - 1 end
	local onGround = grounded and leave <= 0
	local gapBefore = state.gap or 0
	state.gap = onGround and 0 or gapBefore + 1
	-- Hypothesis (recording sadx_1, 6.233 s: the original starts a charge on the press, the port stayed running at 270 and never charged, the original releases at 499): the original keeps the floor under him through the ticks the engine
	-- reports no ground (the contact gaps below), so a press that falls on such a tick is taken on the next tick with ground, as if pressed then; a press in a real fall is not kept.
	state.actionPending = actionPressed and not onGround and state.gap < ROLL_GAP_TICKS and not state.jumped and state.homing == nil or nil
	local curled = state.charge ~= nil or state.rolling
	local braking = state.brake ~= nil
	if (curled or braking) and not onGround and state.gap >= ROLL_GAP_TICKS then
		state.charge, state.rolling, state.brake, curled, braking = nil, false, nil, false, false
	end

	-- The states, from the transitions of FUN_805aa66c and the steps of FUN_805af2b0 (docs/SADX_SONIC_REFERENCE.md has the case numbers): on the ground the running state (1, 2: groundStep), the spin dash's charge (4:
	-- curledStep) and the roll after its release (5); in the air the jump (8, the ball flag set), the fall (0xC, flag clear: after the homing attack's end, the action button, a spring) and the homing attack (0xE).
	-- The ball flag (the game's status 0x100) is set by the jump, the charge and the roll, and cleared on landing. Not ported: the Light Speed Attack (state 6, needs the shoes and the Ancient Light).
	local heading = state.heading
	local turnAway, charging = false, false
	local forward, side, excess = 0, 0, 0
	local chasing = false
	local ranGround = false
	local ranSpeeds = false
	local jumpedNow = false
	local plane = nil
	if onGround then
		state.homed, state.homing = false, nil
		local nx, ny, nz = sf.groundNormal(entity)
		plane = {nx, ny, nz}
		state.tilt = ny < 1 and {nx, ny, nz} or nil
		-- The velocity as the engine hands it back is cut: a walkable floor pushes straight up and zeroes the downward part of the vertical speed (CollisionSystem moveCapsules), so a velocity down a slope would come back
		-- with only the horizontal share of itself (cos squared of the slope: a quarter of his speed lost a tick on 30 degrees). The horizontal components are untouched, so the vertical one that lies in the surface's
		-- plane follows from them; the engine's gravity of the tick is put back, planeSpeeds takes it out.
		vy = -(vx * nx + vz * nz) / ny - GRAVITY * FRAMES_PER_SECOND * frames
		-- The game keeps his forward and side speeds as state and writes the velocity from them, so a change of the surface's normal does not change them; the engine's slide of the world velocity onto the new plane
		-- does (the horizontal share stays, the 3D speed grows by 1 / cos of the change). When the engine's horizontal velocity is the one he handed it last tick (nothing hit him: a wall, an object or a spring
		-- changes it), the speeds he stored then are used, laid on the new axes; the engine's gravity is put back, planeSpeeds takes it out.
		local kept = state.kept
		if kept ~= nil and math.abs(vx - kept.vx) < KEPT_VELOCITY_TOLERANCE and math.abs(vz - kept.vz) < KEPT_VELOCITY_TOLERANCE then
			local fx, fy, fz, sx, sy, sz = groundAxes(heading, nx, ny, nz)
			local kf, ks = kept.forward, kept.side
			-- Observed (recordings sadx_3 9.817 s running and sadx_1 7.017 s ball, both exact to 4 digits: 8.3828 against 8.3829 and 7.3613 against 7.3613): the first tick on a plane with another normal sets the
			-- stored forward speed to the old velocity vector (the world velocity he wrote last tick, its vertical share too) dotted with the facing's tangent on the new plane, which is the speed times about
			-- the cosine of the change (3.3 percent on a 14 degree ramp), not the speed kept. The side speed is not observed: it stays. Observed (every compare, dvy +4.8 a second on the ground): the original's vector
			-- holds the frame's gravity (-GRAVITY, -4.8 a second) that the velocity written here lacks; it is added, else the sadx_3 9.833 s charge starts at 8.407 against 8.383 and ends a step short.
			if kept.nx * nx + kept.ny * ny + kept.nz * nz < 1 - PLANE_CHANGED_EPSILON then
				kf = (kept.vx * fx + (kept.vy - GRAVITY * FRAMES_PER_SECOND) * fy + kept.vz * fz) / FRAMES_PER_SECOND
			end
			kf, ks = kf - GRAVITY * fy * frames, ks - GRAVITY * sy * frames
			vx, vy, vz = (fx * kf + sx * ks) * FRAMES_PER_SECOND, (fy * kf + sy * ks) * FRAMES_PER_SECOND, (fz * kf + sz * ks) * FRAMES_PER_SECOND
		end
		-- The wall rules are ported (wallHit) but switched off: no recording tests them and the sadx_3 hit is not reproduced by them. Remove the dashes to enable.
		-- if kept ~= nil and sf.touchingWall(entity) then
		-- 	local wx, wy, wz = sf.wallNormal(entity)
		-- 	local hx, hy, hz, newHeading = wallHit(state, heading, kept, wx, wy, wz, nx, ny, nz, frames)
		-- 	if hx ~= nil then vx, vy, vz, heading = hx, hy, hz, newHeading end
		-- end
		local groundSpeed, groundSide, ax, ay, az, bx, by, bz = planeSpeeds(heading, nx, ny, nz, vx, vy, vz, frames)
		-- The roll ends below RUN_SPEED or on the action button's press edge (FUN_805a3e58, FUN_805aa66c state 5, to state 2; a jump press first); a press on that tick does not start a charge. Observed (recording
		-- sadx_1, 5 of 5 presses in a roll: 5.033, 6.050, 6.483, 6.817, 7.150 s): the press gives behavior S02 with the ball flag clear and animation 0x1d, no charge; the next charge needs a new press, allowed
		-- already during the 0x1d frames (6.233 s). This replaces the earlier observation (user, in play) that the press starts a new spin dash on the same tick (docs/SADX_SONIC_REFERENCE.md "Roll end").
		local rollEnded = false
		if state.rolling and not justPressed and (groundSpeed < RUN_SPEED or actionPressed) then
			state.rolling, rollEnded = false, true
			state.pose = {name = "rollEnd", frames = 0}
		end
		if not (state.rolling or state.charge ~= nil) then state.ball = false end
		if justPressed and state.charge == nil then
			-- Jump (FUN_805a70ac): the local up speed becomes JUMP_SPEED, along the surface's normal (hypothesis: his frame's tilt is the surface's), the other local speeds stay; the air's rules run the same frame.
			vx = (ax * groundSpeed + bx * groundSide) * FRAMES_PER_SECOND + nx * JUMP_SPEED
			vy = (ay * groundSpeed + by * groundSide) * FRAMES_PER_SECOND + ny * JUMP_SPEED
			vz = (az * groundSpeed + bz * groundSide) * FRAMES_PER_SECOND + nz * JUMP_SPEED
			state.jumped, state.jumpGrace, state.holdTicks = true, 2, 0
			jumpedNow = true
			state.ball, state.rolling, state.charge, state.leaveTicks, state.brake = true, false, nil, 2, nil
			-- Hypothesis (observed in 3 jumps with the heading turning on the press tick: sadx_3 10.083 s, sadx_2 10.250 and 11.117 s): the velocity follows the heading's turn on that tick at the ground's grip, 0.990 and
			-- 0.994 of it (the port's air grip gave 0.09 and 0.12: 12 to 15 units a second sideways for the whole jump, the sadx_3 drift of 10 units at 11.3 s). The next tick is the air's (observed: the turn's share is 0.1).
			heading, vx, vy, vz, turnAway = airStep(state, hasWish, stickAngle, heading, vx, vy, vz, frames, ny <= 0.4 and 0.5 or GROUND_GRIP)
			forward = groundSpeed
		else
			-- The spin dash (FUN_805a4340, FUN_805a3eb8): the press starts the charge at the larger of SPIN_DASH_MIN_CHARGE and his forward speed; each tick held while it is under SPIN_DASH_MAX_CHARGE it grows by
			-- SPIN_DASH_CHARGE_STEP; the release sends him rolling at the charge; held for SPIN_DASH_MAX_TICKS it is dropped.
			local launched = nil
			if state.charge ~= nil then
				if state.chargeTicks > SPIN_DASH_MAX_TICKS - 1 then
					state.charge, state.ball, state.ballTicks, state.ballSubTicks = nil, false, 0, 0
				elseif actionHeld then
					if state.charge < SPIN_DASH_MAX_CHARGE then
						state.charge = float32(state.charge + float32(SPIN_DASH_CHARGE_STEP * frames))
					end
				else
					launched, state.rolling, state.charge = state.charge, true, nil
				end
				state.chargeTicks = state.chargeTicks + 1
			elseif actionPressed and not rollEnded and state.brake == nil then
				state.rolling = false
				state.charge, state.chargeTicks, state.ball, state.ballTicks = float32(math.max(SPIN_DASH_MIN_CHARGE, groundSpeed)), 1, true, 0
			end
			if state.charge ~= nil then
				charging = true
				-- The charge (state 4) makes the flattened tilted ellipsoid of FUN_8059e488 every tick (docs/SADX_SONIC_REFERENCE.md "Effects of the spin dash charge and release"); the plain aura is state 5's and 0xE's.
				-- Observed (user, a steep slope): it must follow the model's angle, and the port draws the model turned about Y only (face), never tilted to the ground (his frame's tilt is physics only), so the frame
				-- given is the world's up axis, not the ground normal.
				dashSphere("charge", entity, 0, 1, 0, heading)
				heading, forward, side, vx, vy, vz = curledStep(state, true, hasWish, stickAngle, heading, vx, vy, vz, nx, ny, nz, frames, nil)
			elseif state.rolling then
				heading, forward, side, vx, vy, vz = curledStep(state, false, hasWish, stickAngle, heading, vx, vy, vz, nx, ny, nz, frames, launched)
				-- Roll (state 5): an aura and a trail segment every tick while the forward speed is RUN_SPEED or more (decompile, w3+0x38 >= w3+0xc4).
				if forward >= RUN_SPEED then
					dashSphere("aura", entity, nx, ny, nz, heading)
					dashTrail(entity)
				end
			else
				-- The brake state 0xD (FUN_805aa66c cases 2 and 0xD, FUN_805af2b0 case 0xD; docs/SADX_SONIC_REFERENCE.md "Brake state"). Entered at the start of a frame while running when the stick is more than BRAKE_ANGLE off
				-- his heading and his forward speed is WALK_SPEED or more (the skid: the Brake motion plays, SKID_FRAMES), or on landing from the air when the stick is that far off and the forward speed is over 0 (one frame,
				-- LANDING_SKID_FRAMES: the landing writes another motion, so the next frame leaves it). Inside, only the charge's friction acts (no stick acceleration, no new turn target: the heading stays), the jump
				-- press jumps, the action button does not start the spin dash. Left: the forward speed at 0 or less with the stick still that far off: the running state with the heading set to the stick (the Turn Around
				-- motion, and TURN_AROUND_FRAMES later, at the frame after it ended, WALK_SPEED is added once to the forward speed); else the frames above: the running state.
				local brake = state.brake
				local far = hasWish and math.abs(wrapAngle(stickAngle - heading)) > BRAKE_ANGLE
				if brake == nil then
					if far and groundSpeed > 0 then
						local fromAir = gapBefore >= ROLL_GAP_TICKS
						if fromAir or groundSpeed >= WALK_SPEED then
							brake = {frames = 0, skid = not fromAir}
							state.brake, state.turnFrames = brake, nil
						end
					end
				elseif far and groundSpeed <= STAND_EPSILON then
					state.brake, state.turnFrames, brake = nil, 0, nil
					heading, state.turnTarget = stickAngle, stickAngle
				elseif brake.frames >= (brake.skid and SKID_FRAMES or LANDING_SKID_FRAMES) then
					state.brake, brake = nil, nil
				end
				if brake ~= nil then
					turnAway = brake.skid
					brake.frames = brake.frames + frames
					heading, forward, side, vx, vy, vz = curledStep(state, true, hasWish, stickAngle, heading, vx, vy, vz, nx, ny, nz, frames, nil, true)
				else
					ranGround = true
					if state.turnFrames ~= nil then
						if state.turnFrames >= TURN_AROUND_FRAMES then
							state.turnFrames = nil
							if hasWish or math.abs(groundSpeed) > STAND_EPSILON then
								vx, vy, vz = vx + ax * WALK_SPEED * FRAMES_PER_SECOND, vy + ay * WALK_SPEED * FRAMES_PER_SECOND, vz + az * WALK_SPEED * FRAMES_PER_SECOND
							end
						else
							state.turnFrames = state.turnFrames + frames
						end
					end
					heading, forward, side, vx, vy, vz, turnAway, excess = groundStep(state, hasWish, stickAngle, heading, vx, vy, vz, nx, ny, nz, frames)
				end
			end
			ranSpeeds = true
		end
	elseif curled or braking then
		-- A gap in the ground contact inside the charge, the roll or the skid: the air's rules for the tick, the state goes on (the skid's clock too).
		charging = state.charge ~= nil
		heading, vx, vy, vz, turnAway = airStep(state, hasWish, stickAngle, heading, vx, vy, vz, frames)
		if braking then
			state.brake.frames = state.brake.frames + frames
			turnAway = state.brake.skid
		end
		forward = (vx * math.cos(heading) + vz * math.sin(heading)) / FRAMES_PER_SECOND
	else
		-- The homing attack's transitions (FUN_805aa66c, state 0xE): over HOMING_MAX_FRAMES frames, or its target gone (the enemy's own rule destroyed it and bounces him): back to the jump, which allows the next attack;
		-- the count at HOMING_END_COUNT or the action button: the fall, no further attack until he lands.
		local homing = state.homing
		if homing ~= nil then
			local counted = homing.frames
			homing.frames = counted + 1
			if counted > HOMING_MAX_FRAMES or (homing.target ~= nil and not sf.exists(homing.target)) then
				state.homing, state.homed = nil, false
			elseif homing.count >= HOMING_END_COUNT or actionPressed then
				state.homing, state.ball, state.jumped = nil, false, false
			end
		end
		-- The jump state (FUN_805aa66c, state 8): the action button uncurls him into the fall (FUN_805a7008); the jump button starts the homing attack (FUN_804e60cc, FUN_805a8958: no target listed gives him a forward
		-- speed of HOMING_SPEED, else the target's rules run once with the count at -1 and, if it did not start chasing (count over 0), also HOMING_SPEED); else, held for fewer than JUMP_HOLD_TICKS frames in all
		-- (a counter that is zeroed by a jump only), the jump's hold (FUN_804e6064): JUMP_HOLD_ACCEL a frame on his vertical speed. Nothing cuts the jump short on the release.
		if state.homing == nil and state.ball then
			if actionPressed then
				state.ball, state.jumped = false, false
			elseif justPressed and not state.homed then
				state.homing, state.homed = {count = -1, frames = 0}, true
				-- The burst is made once at the start of the state (FUN_805a8958) and stays where it is.
				local tilt = state.tilt or {0, 1, 0}
				dashSphere("burst", entity, tilt[1], tilt[2], tilt[3], heading)
				if homingCandidates(entity, heading) then
					heading, vx, vy, vz = homingStep(state, entity, hasWish, stickAngle, heading, vx, vy, vz, frames)
				else
					state.homing.count = 1
				end
				if state.homing.count ~= 0 then
					local delta = HOMING_SPEED * FRAMES_PER_SECOND - (vx * math.cos(heading) + vz * math.sin(heading))
					vx, vz = vx + delta * math.cos(heading), vz + delta * math.sin(heading)
				end
			elseif pressed and (state.holdTicks or 0) < JUMP_HOLD_TICKS then
				-- Observed (sadx_3 10.1 to 10.5 s, off a 14.7 degree ramp): the hold adds to the local up speed before the drag, along his tilted frame's up axis, not the world's: the local up speed changes by exactly
				-- -0.01 U - 0.0172 a frame (the share -0.0774 of the gravity, +0.0608 the hold, -0.0006 the drag of it), so the hold takes 0.2538 x 0.0608 a frame off the forward speed on that ramp, a second
				-- a tick that the port's world-y hold left in (a speed 5 a second high at 10.5 s).
				local tilt = state.tilt or {0, 1, 0}
				local hold = JUMP_HOLD_ACCEL * FRAMES_PER_SECOND * frames
				vx, vy, vz = vx + tilt[1] * hold, vy + tilt[2] * hold, vz + tilt[3] * hold
				state.holdTicks = (state.holdTicks or 0) + 1
			end
		end
		if state.homing ~= nil then
			heading, vx, vy, vz, turnAway, chasing = homingStep(state, entity, hasWish, stickAngle, heading, vx, vy, vz, frames)
			-- The homing flight (FUN_805af2b0 state 0xE): an aura and a trail segment every tick, no speed test (the burst was made at its start).
			local tilt = state.tilt or {0, 1, 0}
			dashSphere("aura", entity, tilt[1], tilt[2], tilt[3], heading)
			dashTrail(entity)
		else
			heading, vx, vy, vz, turnAway = airStep(state, hasWish, stickAngle, heading, vx, vy, vz, frames)
		end
		forward = (vx * math.cos(heading) + vz * math.sin(heading)) / FRAMES_PER_SECOND
	end
	if not ranGround then state.turnFrames = nil end
	state.heading = heading

	-- The homing attack's motion and the end of the tick. Decompile: case 0xE writes no motion id, so the jump's motion plays and the ball flag keeps blinking it (docs/SADX_SONIC_REFERENCE.md "Ball blink"; changed from a solid
	-- Ball placeholder). While chasing a target the game's gravity does not act (the velocity is set), so the engine's is given back.
	if state.homing ~= nil then
		if chasing then vy = vy + GRAVITY * FRAMES_PER_SECOND * frames end
		sf.blink(entity, state.ball and ballBlinkStep(state) and "ball" or false)
		sf.animate(entity, "jump", math.max(JUMP_RATE * (state.work0 or 0), 1e-6))
		ballSquash(entity, false)
		face(entity, state.heading)
		sf.setGroundSnap(entity, 0)
		state.kept = nil
		sf.setVelocity(entity, vx, vy, vz)
		return "jump"
	end

	-- Motion. External (FUN_805a99f0, FUN_805aa66c; "Animation" in docs/SADX_SONIC_REFERENCE.md): on the ground by forward speed plus the excess, Walk to WALK_SPEED, Speedwalk to SPEEDWALK_SPEED, Jog to JOG_SPEED, Run to RUN_MAX_SPEED,
	-- Sprint above; standing still is Stand; Brake while the brake state 0xD's skid runs (state.brake), Turn Around after its stop (state.turnFrames); Landing for LANDING_TICKS after a hard landing standing still;
	-- the curled Jump for the jump, the charge and the roll. Placeholder or hypothesis (see the top): Damage while state.damaged, Spring Fall while rising after a spring, else Fall.
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
	-- The playback rate (nil: the motion's own, as before): the table's value times a speed (RATES, JUMP_RATE, ROLL_RATE). Table modes 9 and 10 advance by that product, so a rate of zero holds the pose; the engine's 0 means
	-- its default rate, hence the small floor. The sign is dropped (a negative speed would play backwards in the game; the engine's rate is not tried with a negative there); only Wait Back (table id 6, mode 6) is sent negative, below.
	local rate = nil
	if (state.damaged or 0) > 0 then
		state.damaged = state.damaged - 1
		motion = "damage"
	elseif animGrounded and (state.jumpGrace or 0) > 0 then
		-- The tick after the jump still reports the ground.
		state.jumpGrace = state.jumpGrace - 1
		motion = "jump"
		rate = JUMP_RATE * (state.work0 or 0)
	elseif animGrounded then
		local cameFromJump, cameFromSpring = state.jumped, state.springed
		state.springed, state.jumped = false, false
		-- Landing (FUN_805aa66c states 8 and 0xC): with the brake state's condition false, no stick and a forward speed of exactly 0 he stands (state 1) and the table's Landing plays only when his vertical speed was
		-- HARD_LANDING_VY or lower (else the motion is 2 after a jump, 1 after a fall: standing poses without an entry in Sonic.json, Stand here); any other landing goes to the running state and picks the motion by
		-- speed at once (FUN_805a99f0). A stick or a speed ends the landing motion (state 1 goes to 2 on the stick, motion 9).
		local standing = not hasWish and not charging and not state.rolling and math.abs(forward) < STAND_EPSILON
		if state.wasGrounded == false then
			state.landing = (standing and (state.airVy or 0) / FRAMES_PER_SECOND <= HARD_LANDING_VY) and LANDING_TICKS or 0
		end
		if not standing then state.landing = 0 end
		-- Standing poses (FUN_805aa66c states 8 and 0xC write the motion 2 after a jump and 1 after a fall when the landing is not the hard one; FUN_805a9c28 keeps the motions 0 to 6 in state 1 and after 0x708 steps
		-- of standing writes 4, Wait A, which runs on to 5, Wait B; FUN_805a3e58 writes 0x1D on the roll's end, which FUN_805a99f0 leaves alone until it has ended). Placeholders: the landing from a spring stays Stand (not read),
		-- the step counter (work+0xE) is taken to restart whenever he is not standing (hypothesis), and Wait B runs on to Wait Back (id 6, mode 6: Wait A's data played backwards from its last frame, 4 blend frames + 77 steps of 0.25, then Stand; the engine plays it with a negative rate).
		if state.wasGrounded == false then
			state.pose = standing and state.landing == 0 and not cameFromSpring and {name = cameFromJump and "standAlt" or "landFall", frames = 0} or nil
		end
		local pose = state.pose
		if charging or state.rolling or (pose ~= nil and pose.name ~= "rollEnd" and not standing) then pose = nil end
		if pose ~= nil then
			pose.frames = pose.frames + frames
			if POSE_END[pose.name] ~= nil and pose.frames >= POSE_END[pose.name] then
				local nextName = POSE_NEXT[pose.name]
				pose = nextName ~= nil and (standing or pose.name ~= "rollEnd") and {name = nextName, frames = 0} or nil
			end
		end
		state.idleFrames = standing and (state.idleFrames or 0) + frames or 0
		if state.idleFrames > IDLE_WAIT_FRAMES and (state.landing or 0) == 0 and (pose == nil or POSE_NEXT[pose.name] == "standAlt" or pose.name == "standAlt") then
			pose = {name = "waitA", frames = 0}
			state.idleFrames = 0
		end
		state.pose = pose
		if (state.landing or 0) > 0 then
			state.landing = state.landing - 1
			motion = "landing"
		elseif charging then
			-- The charge: the curled Jump (table entry 0xE) at JUMP_RATE times work[0], which the charge holds (FUN_805a4340 starts it at the larger of 2.0 and the forward speed, FUN_805a3eb8 adds 0.4 a frame).
			motion = "jump"
			state.work0 = state.charge
			rate = JUMP_RATE * state.work0
		elseif state.rolling then
			-- The roll (state 5, table entry 0xF, FUN_805a3eb8): the curled Jump's motion data at ROLL_RATE times the forward speed, under its own entry ("roll" in Sonic.json: the change from the charge's entry blends).
			motion = "roll"
			rate = ROLL_RATE * forward
		elseif turnAway then motion = "brake"
		elseif state.turnFrames ~= nil then
			-- FUN_805aa66c state 0xD: the stop with the stick still far off writes motion 0x1E (Turn Around, 0.9 a frame); FUN_805a99f0 leaves it alone until it has ended.
			motion = "turnAround"
			rate = TURN_AROUND_RATE
		elseif pose ~= nil and (standing or pose.name == "rollEnd") then
			motion = pose.name
			rate = POSE_RATE[motion]
		elseif standing then motion = "stand"
		else
			-- FUN_805a99f0: the forward speed plus PICK_EXCESS_FACTOR times the excess against the four speeds, not its size (a negative forward speed picks Walk), and work[0] is that speed with RATE_EXCESS_FACTOR.
			local pick = forward + PICK_EXCESS_FACTOR * excess
			state.work0 = forward + RATE_EXCESS_FACTOR * excess
			if pick <= WALK_SPEED then motion = "walk"
			elseif pick <= SPEEDWALK_SPEED then motion = "speedwalk"
			elseif pick <= JOG_SPEED then motion = "jog"
			elseif pick <= RUN_MAX_SPEED then motion = "run"
			else motion = "sprint" end
			rate = RATES[motion] * (motion == "sprint" and forward or state.work0)
		end
	else
		if state.springed and vy <= 0 then state.springed = false end
		if state.springed then state.jumped = false end
		motion = state.springed and "springFall" or state.jumped and "jump" or "fall"
		if motion == "jump" then rate = JUMP_RATE * (state.work0 or 0) end
		state.landing, state.pose, state.idleFrames = 0, nil, 0
	end
	state.wasGrounded = animGrounded

	-- Ball blink. Decompile (FUN_805af2b0, FUN_805a9f90, FUN_805a3eb8, FUN_805a4340; docs/SADX_SONIC_REFERENCE.md "Ball blink"): the main function adds 1 to the counter (work+0x7e) once per game step while status 0x100 is set, whatever the
	-- action, and nothing else adds to it; the draw (called at the end of every step) uses the Ball entry (motion 0x20, jumpball model) instead of the playing motion on the steps where counter & 0x11 is not zero, with the same animation clock
	-- (sf.blink). So in each 32 steps the odd ones of the first 16 and all of the last 16 are the ball. The counter is zeroed only by the spin dash start and by the spin dash charge's end (FUN_805a3eb8); a jump, a homing attack and a roll
	-- go on from the value left. The decompile steps the game once per 60 Hz retrace in play (FUN_8047ab7c(1,1); one extra step when a frame ran late), so by the code the port's tick is 1 per step; BALL_BLINK_TICKS stays 4 because
	-- of your observation that 1 was far too fast: the conflict is open (see the reference).
	-- The port steps it on every tick with state.ball (ballBlinkStep), the roll and the homing flight included, whatever motion plays.
	local blink = state.ball and ballBlinkStep(state)
	sf.blink(entity, blink and "ball" or false)
	ballSquash(entity, motion == "jump" and onGround)
	sf.animate(entity, motion, rate and math.max(math.abs(rate), 1e-6) * (motion == "waitBack" and -1 or 1) or 0, BLEND_STEPS[motion] or 0)
	face(entity, state.heading)
	-- Observed (recording sadx_3, 0.8 to 0.82 s): at a ramp's lip the original puts him down on the floor in the same tick although his velocity still points up. The snap may therefore act on a rising body while the
	-- ground step ran without a jump or a spring. Hypothesis (recording sadx_3, 4.68 to 4.72 s the roll and 7.98 to 8.02 s the run, both at a crest where the surface's slope turns): the same for the ticks after the
	-- ground contact is lost (fewer than ROLL_GAP_TICKS) while no jump, spring or homing attack is playing. The engine reports no ground the tick the surface turns away, the original keeps him on the floor (put down
	-- the next tick, never in the air); the port was in the air for three ticks and lost the air's drag (6.7 to 12.7 per second per tick against 1.8 on the ground). The snap's reach still limits it: a launch
	-- faster than the reach (sadx_1 7.1 s, 139 up) leaves the ground.
	local contactGap = not onGround and state.gap < ROLL_GAP_TICKS and not state.jumped and state.homing == nil
	sf.setGroundSnapMode(entity, {whileRising = ((onGround and ranSpeeds) or contactGap) and not jumpedNow and not state.springed})
	sf.setGroundSnap(entity, probeReach((vx * math.cos(state.heading) + vz * math.sin(state.heading)) / FRAMES_PER_SECOND))
	vx, vy, vz = clamp(vx, SPEED_CAP), clamp(vy, SPEED_CAP), clamp(vz, SPEED_CAP)
	-- Observed (recording sadx_2, 1.3 to 2.9 s): with a gentle push the game's stored forward speed (w3+0x38) stays 0 while the world velocity holds one frame's acceleration, so nothing accumulates until that
	-- acceleration reaches about STORED_SPEED_MIN. The clearing code was not found. The velocity is written as is; the speed kept for the next tick is 0.
	if onGround and ranSpeeds and hasWish and math.abs(forward) < STORED_SPEED_MIN then forward = 0 end
	state.kept = onGround and ranSpeeds and {vx = vx, vy = vy, vz = vz, forward = forward, side = side, nx = plane[1], ny = plane[2], nz = plane[3]} or nil
	sf.setVelocity(entity, vx, vy, vz)
	return motion
end

-- The tick's input comes from the replay when one runs (SadxReplay.step also counts its tick, so it runs before the lock's early return); the log line follows the step.
sf.behavior("Player", function(entity)
	local replay = SadxReplay.step()
	local motion = playerStep(entity, replay)
	local wx, wz, mag, a, b = 0, 0, 0, false, false
	if replay ~= nil then wx, wz, mag, a, b = replay.wx, replay.wz, replay.mag, replay.a, replay.b end
	SadxReplay.log(entity, motion, wx, wz, mag, a, b)
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
	-- His shape touches a triangle by its face only (decompile, FUN_8043d5a0): the engine's push-out uses the face normal, no edges or corners.
	sf.setFaceContact(entity, true)
end
