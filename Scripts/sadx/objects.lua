-- SADX stage objects: the ring, springs, dash panels and the enemies, ported from the C++ placeholders of SADXGameplay.cpp. The numbers marked "placeholder" are made up, not decoded from the game.
-- What is decoded (external, pseudocode of _Main.rel; units per frame at 60 a second and frame = tick are hypotheses): the dash panel's speed (scale.x) and disable time (scale.y) with their defaults
-- 14 and 60, and its direction, (1, 0, 0) turned about z, x and y - 0x4000 by the record's angles. The spring's launch (FUN_804bab4c / FUN_804ca0ec): speed 5.0 + scale.y (units per frame) along its up axis,
-- and a control lock of (int) max(scale.x, 0) frames, written into the player's work area (+10), which the stick read FUN_804e6230 checks. Its trigger distances and its re-launch cooldown are not decoded.

local SPRING_BASE_SPEED = 5.0     -- external, units per frame
local SPRING_COOLDOWN_TICKS = 30  -- placeholder
local ENEMY_BOUNCE_SPEED = 40     -- placeholder
local ENEMY_KNOCKBACK_SPEED = 90  -- placeholder
local INVULNERABLE_TICKS = 90     -- placeholder
local DAMAGE_TICKS = 40           -- placeholder: ticks the player plays his damage motion
local DASH_PANEL_DEFAULT_SPEED = 14
local DASH_PANEL_DEFAULT_DISABLE_TICKS = 60

local tagBody = sf.tag("Body")
local tagPickup = sf.tag("Pickup")
local tagHazard = sf.tag("Hazard")
local tagRing = sf.tag("Ring")
local tagSpring = sf.tag("Spring")
local tagDashPanel = sf.tag("DashPanel")
local tagEnemy = sf.tag("Enemy")

-- The entities the homing attack can lock on to (player.lua reads it): the keys are entity handles. The scripts have no way to list entities, so the objects register themselves.
homingTargets = {}

-- A body the interactions act on: an entity with a physics body (the player).
local function isMover(entity)
	return sf.exists(entity) and sf.hasBody(entity)
end

-- The player's state table has what the objects write to it (see player.lua): lock = ticks the control is locked for, invulnerable = ticks left, damaged = ticks of the damage motion left, springed = a spring launched him.
local function playerState(player)
	local state = sf.state(player)
	state.lock = state.lock or 0
	state.invulnerable = state.invulnerable or 0
	return state
end

-- Counts down state.cooldown every tick (the dash panel's and the spring's launch cooldowns).
sf.behavior("Cooldown", function(entity)
	local state = sf.state(entity)
	if state.cooldown > 0 then state.cooldown = state.cooldown - 1 end
end)

-- The ring (placeholder: not decoded): touched by the player it is counted and removed. state.collected is set at once, since contacts are tested every tick and the removal happens at the start of the next.
local function ringTouched(ring, player)
	if not sf.exists(ring) or not isMover(player) then return end
	local state = sf.state(ring)
	if state.collected then return end
	state.collected = true
	sf.setRings(sf.rings() + 1)
	sf.postEvent("RingCollected", ring)
	sf.destroy(ring)
end

-- The dash panel: state.speed (units per frame), state.dx/dy/dz the direction, state.disable the disable time in ticks, state.cooldown the countdown to its next launch.
local function dashPanelTouched(panel, player)
	if not sf.exists(panel) or not isMover(player) then return end
	local state = sf.state(panel)
	if state.cooldown > 0 then return end
	local speed = state.speed * sf.tickRate()
	sf.setVelocity(player, state.dx * speed, state.dy * speed, state.dz * speed)
	playerState(player).lock = state.disable
	state.cooldown = state.disable
	sf.postEvent("DashPanelLaunch", panel)
end

-- The spring: launches the player along its up axis (state.ux/uy/uz) at state.speed (units per frame) and locks the control for state.lock ticks. The cooldown is a placeholder.
local function springTouched(spring, player)
	if not sf.exists(spring) or not isMover(player) then return end
	local state = sf.state(spring)
	if state.cooldown > 0 then return end
	local speed = state.speed * sf.tickRate()
	sf.setVelocity(player, state.ux * speed, state.uy * speed, state.uz * speed)
	playerState(player).lock = state.lock
	playerState(player).springed = true
	state.cooldown = SPRING_COOLDOWN_TICKS
	sf.postEvent("SpringLaunch", spring)
end

-- Enemies (placeholder: a simplified idea of the game's rules): touched by a player that is in the air they are destroyed and the player bounces; touched on the ground the player loses its rings and is
-- knocked back, and cannot be hurt again for INVULNERABLE_TICKS. "In the air" is the body's own ground contact (sf.grounded), not a guess from its vertical speed.
local function enemyTouched(enemy, player)
	if not sf.exists(enemy) or not isMover(player) then return end
	local state = sf.state(enemy)
	if state.defeated then return end
	local vx, vy, vz = sf.velocity(player)
	if not sf.grounded(player) then
		state.defeated = true
		sf.postEvent("EnemyDefeated", enemy)
		sf.destroy(enemy)
		sf.setVelocity(player, vx, ENEMY_BOUNCE_SPEED, vz)
		return
	end
	local hurt = playerState(player)
	if hurt.invulnerable > 0 then return end
	sf.setRings(0)
	sf.postEvent("PlayerHurt", player)
	hurt.invulnerable = INVULNERABLE_TICKS
	hurt.damaged = DAMAGE_TICKS
	local ex, _, ez = sf.position(enemy)
	local px, _, pz = sf.position(player)
	local ax, az = px - ex, pz - ez
	local length = math.sqrt(ax * ax + az * az)
	if length > 1e-4 then ax, az = ax / length, az / length else ax, az = 0, 1 end
	sf.setVelocity(player, ax * ENEMY_KNOCKBACK_SPEED, 30, az * ENEMY_KNOCKBACK_SPEED)
end

-- Each object's collision behaviour has its own tags and acts on a Body that touches it.
local ringBehavior = sf.collisionBehavior("Ring", {tags = {tagPickup, tagRing}, interactions = {[tagBody] = ringTouched}})
local springBehavior = sf.collisionBehavior("Spring", {tags = {tagSpring}, interactions = {[tagBody] = springTouched}})
local dashPanelBehavior = sf.collisionBehavior("DashPanel", {tags = {tagDashPanel}, interactions = {[tagBody] = dashPanelTouched}})
local enemyBehavior = sf.collisionBehavior("Enemy", {tags = {tagHazard, tagEnemy}, interactions = {[tagBody] = enemyTouched}})

-- Each prefab's setup (sf.prefabBehavior): the engine calls it for every entity it makes from the prefab "SADX/<object name>" (placement: the SET record, see Scripting.h; nil for an object the stage creates).
sf.prefabBehavior("SADX/RING", function(entity)
	sf.setShapeBehavior(entity, ringBehavior)
end)

local function setupSpring(entity, placement)
	sf.setShapeBehavior(entity, springBehavior)
	local state = sf.state(entity)
	state.ux, state.uy, state.uz = sf.axis(entity, 0, 1, 0)
	local scale = placement ~= nil and placement.scale or {x = 0, y = 0}
	state.speed = SPRING_BASE_SPEED + scale.y
	state.lock = math.floor(math.max(scale.x, 0))
	state.cooldown = 0
	sf.give(entity, "Cooldown")
end
sf.prefabBehavior("SADX/SPRING", setupSpring)
sf.prefabBehavior("SADX/SPRINGB", setupSpring)

sf.prefabBehavior("SADX/O AXPNL", function(entity, placement)
	if placement == nil then return end
	sf.setShapeBehavior(entity, dashPanelBehavior)
	local state = sf.state(entity)
	state.speed = placement.scale.x > 0 and placement.scale.x or DASH_PANEL_DEFAULT_SPEED
	state.disable = placement.scale.y > 0 and math.floor(placement.scale.y) or DASH_PANEL_DEFAULT_DISABLE_TICKS
	state.dx, state.dy, state.dz = sf.rotateBamsZXY(1, 0, 0, placement.rotation.z, placement.rotation.x, placement.rotation.y - 0x4000)
	state.cooldown = 0
	sf.give(entity, "Cooldown")
end)

local function setupEnemy(entity)
	sf.setShapeBehavior(entity, enemyBehavior)
	homingTargets[entity] = true
end
sf.prefabBehavior("SADX/E SARU", setupEnemy)
sf.prefabBehavior("SADX/E SAITO", setupEnemy)
sf.prefabBehavior("SADX/E AMEMB", setupEnemy)
