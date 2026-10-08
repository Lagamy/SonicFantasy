-- Entry script (main.cpp loads it). The engine calls attachPlayer(entity) once for each playable entity and the prefab behaviours registered with sf.prefabBehavior for every object a level makes (sadx/objects.lua). One player is active at a time:
-- only the active one reads the keys (state.active, read by both player scripts), the other stands where it is under the same physics. The first one attached is active; J switches, moving only the camera (sf.followCamera)
-- to the new one. R puts every player back at the position it had when it was attached, with no velocity.
sf.include("sadx/main.lua")
sf.include("shadow/main.lua")

local ATTACH = {SASonic = attachSonicPlayer, Shadow = attachShadowPlayer}
local INIT_STATE = {SASonic = initSonicState, Shadow = initShadowState}

local players = {}
local active = 0
local starts = {}
local jHeld = false
local rHeld = false

local function activate(index)
	active = index
	for i, entity in ipairs(players) do sf.state(entity).active = i == index end
	sf.followCamera(players[index])
end

sf.behavior("PlayerSwitch", function(entity)
	local controlled = not sf.debugMode()
	local jPressed = controlled and sf.key("J")
	local switch = jPressed and not jHeld
	jHeld = jPressed
	local rPressed = controlled and sf.key("R")
	local respawn = rPressed and not rHeld
	rHeld = rPressed
	if switch and #players >= 2 then activate(active % #players + 1) end
	if respawn then
		for i, player in ipairs(players) do
			local start = starts[i]
			sf.setPosition(player, start[1], start[2], start[3])
			sf.setVelocity(player, 0, 0, 0)
		end
		if ShadowReplay.enabled then
			for i, player in ipairs(players) do
				if sf.name(player) == "Shadow" then
					activate(i)
					ShadowReplay.start(player)
				end
			end
		end
	end
end)

function attachPlayer(entity)
	local attach = ATTACH[sf.name(entity)]
	if attach == nil then error("no player script for the entity '" .. sf.name(entity) .. "'") end
	attach(entity)
	players[#players + 1] = entity
	starts[#players] = {sf.position(entity)}
	if #players == 1 then
		sf.give(entity, "PlayerSwitch")
		activate(1)
	else
		sf.state(entity).active = false
	end
end

-- Gives every player's state table (sf.state) its starting contents again, the active flag kept as it is (game.lua calls it when the engine resets the game).
function resetPlayers()
	for i, player in ipairs(players) do
		local state = sf.state(player)
		for key in pairs(state) do state[key] = nil end
		INIT_STATE[sf.name(player)](state)
		state.active = i == active
	end
end

-- Records the players' current positions as the ones R returns them to (game.lua calls it after moving them to a new level's start).
function recordPlayerStarts()
	for i, player in ipairs(players) do starts[i] = {sf.position(player)} end
end
