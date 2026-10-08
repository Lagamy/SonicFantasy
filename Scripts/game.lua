-- Startup of the game (main.cpp runs it once the discs are open and the shaders exist, after main.lua): which level is loaded and which players stand in it, and the L key that switches to the next level while running.
-- START is the level at startup. Each level has a camera start and a start position per player (observed or placeholders, as they were in main.cpp).
local START = "EmeraldCoast"

local ORDER = {"EmeraldCoast", "Stg0100"}
local LEVELS = {
	EmeraldCoast = {
		camera = {-12.0, 30.0, 142.0},
		players = {sonic = {0, 10, 5}, shadow = {0, 4, -5}},
	},
	-- The camera starts at the middle of the largest sector of the first section of stg0100 (observed in its world, with the 90 degree yaw of ONEInstance.h applied); the real start of the stage is not known.
	Stg0100 = {
		camera = {-3440, -4860, 0},
		players = {sonic = {-3430, -4542 + 5.3, 5}, shadow = {-3430, -4541.5, -5}},
	},
}
-- A level without a start of its own (a file of Levels/ loaded from the editor) gets this one: placeholders.
local DEFAULT_START = {camera = {0.0, 30.0, 100.0}, players = {sonic = {0, 10, 5}, shadow = {0, 4, -5}}}
local CHARACTERS = {"sonic", "shadow"}

local current = START
local level = sf.none
local players = {}

-- Loads the level and puts the camera and the players at its start.
local function enter(name)
	local data = LEVELS[name] or DEFAULT_START
	current = name
	level = sf.loadLevel(name)
	sf.setCamera(table.unpack(data.camera))
	for i, character in ipairs(CHARACTERS) do
		local start = data.players[character]
		if players[i] == nil then
			players[i] = sf.spawnPlayer(character, table.unpack(start))
			attachPlayer(players[i])
		else
			sf.setPosition(players[i], table.unpack(start))
			sf.setVelocity(players[i], 0, 0, 0)
		end
	end
	recordPlayerStarts()
end

-- L unloads the level and loads the next one of ORDER. The old level goes at the start of the next tick (sf.unloadLevel), so the new one is loaded on the tick after the key.
local lHeld = false
local unloading = false
sf.behavior("LevelSwitch", function(entity)
	if unloading then
		if sf.levelExists(level) then return end
		unloading = false
		local index = 1
		for i, name in ipairs(ORDER) do
			if name == current then index = i % #ORDER + 1 end
		end
		enter(ORDER[index])
		return
	end
	local lPressed = not sf.debugMode() and sf.key("L")
	local switch = lPressed and not lHeld
	lHeld = lPressed
	if switch then
		sf.unloadLevel(level)
		unloading = true
	end
end)

-- The engine's reset (the editor's Stop and File > Load level, Scripting::resetGame): first call, the level is asked to go; second call, it is gone, so the same level (or the one named, a file of Levels/) is loaded with its objects and the players at its start.
function resetGame(unloaded, name)
	if not unloaded then
		sf.unloadLevel(level)
		return
	end
	unloading = false
	lHeld = false
	enter(name or current)
	resetPlayers()
end

enter(START)
sf.give(players[1], "LevelSwitch")
