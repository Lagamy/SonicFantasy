-- Startup of the game (main.cpp runs it once the discs are open and the shaders exist, after main.lua): which level is loaded and which players stand in it, and the L key that switches to the next level while running.
-- START is the level at startup. Each level has a camera start and a start position per player (observed or placeholders, as they were in main.cpp).
local START = "EmeraldCoast"

local ORDER = {"EmeraldCoast", "StationSquare4", "Stg0100"}
local LEVELS = {
	EmeraldCoast = {
		game = "sadx",
		camera = {-12.0, 30.0, 142.0},
		players = {sonic = {0, 10, 5}, shadow = {0, 4, -5}},
	},
	-- Station Square levels are named by their land table (landtabless0N of _Adv00.rel = level "StationSquareN"); only StationSquare4 has a start of its own, the others get DEFAULT_START.
	-- Placeholders: a spot above piece 24 of landtabless04 (centre -332, 0, 1715, radius 140, solid and visible; the table's piece nearest the median of its solid pieces), with the camera behind it as in EmeraldCoast; the real start is not known.
	StationSquare4 = {
		game = "sadx",
		camera = {-332.0, 170.0, 1850.0},
		players = {sonic = {-332, 150, 1715}, shadow = {-332, 150, 1735}},
	},
	-- The camera starts at the middle of the largest sector of the first section of stg0100 (observed in its world, with the 90 degree yaw of ONEInstance.h applied); the real start of the stage is not known.
	Stg0100 = {
		game = "sth2005",
		camera = {-3440, -4860, 0},
		players = {sonic = {-3430, -4542 + 5.3, 5}, shadow = {-3430, -4541.5, -5}},
	},
}
-- A level without a start of its own (a file of Levels/ loaded from the editor) gets this one: placeholders.
local DEFAULT_START = {camera = {0.0, 30.0, 100.0}, players = {sonic = {0, 10, 5}, shadow = {0, 4, -5}}}
-- The other land tables of _Adv00.rel: SADX levels with the placeholder start.
for _, name in ipairs({"StationSquare0", "StationSquare1", "StationSquare2", "StationSquare3", "StationSquare5"}) do
	LEVELS[name] = {game = "sadx", camera = DEFAULT_START.camera, players = DEFAULT_START.players}
end
local CHARACTERS = {"sonic", "shadow"}

local current = START
local level = sf.none
local players = {}

-- Experimental: the ground snap also depends on the game the loaded level belongs to. A character in a level of its own game keeps the snap its script sets. In a level of the other game the behaviour GroundSnap is given to it (once, on
-- the level load that needs it) and applies that game's snap mode after the character's own behaviour every tick (sf.setGroundSnapMode: a field left out keeps the character's value). Hypothesis, not observed: the other game's
-- character in a level uses the snap of the level's game. A level with no `game` (a file of Levels/ loaded from the editor) changes nothing.
local CHARACTER_GAME = {SASonic = "sadx", Shadow = "sth2005"}
local SNAP_OF_GAME = {
	-- The stepped probe straight down (PhysicsBody::snapDistance) of SADX Sonic, without his rising rule.
	sadx = {ray = false, blockedByWall = false, whileRising = false},
	-- The game's ray probe along the last floor's normal, as Shadow's: radius 0 = the body's own shape, accepts within 80 degrees of the normal (docs/ai/STH2005_COLLISION.md).
	sth2005 = {ray = true, radius = 0.0, minNormalY = math.cos(math.rad(80)), blockedByWall = true, whileRising = false},
}
-- What the character's script sets itself (attachShadowPlayer; Sonic: the engine's defaults, his rising rule is set by his script each tick). Written once when a level of the character's own game loads, because the foreign mode above stays in the body
-- after GroundSnap stops applying it.
local NATIVE_SNAP = {
	SASonic = {ray = false, radius = 0.0, minNormalY = math.cos(math.rad(80)), blockedByWall = false},
	Shadow = {ray = true, radius = 5.0, minNormalY = math.cos(math.rad(80)), blockedByWall = true, whileRising = true},
}
local snapGiven = {}
local snapMode = {}

sf.behavior("GroundSnap", function(entity)
	local mode = snapMode[entity]
	if mode ~= nil then sf.setGroundSnapMode(entity, mode) end
end)

-- Called on every level load for one player: attaches GroundSnap on demand and sets which mode it applies (nil = the character's own).
local function setLevelSnap(entity, levelGame)
	local own = CHARACTER_GAME[sf.name(entity)]
	local foreign = levelGame ~= nil and own ~= nil and levelGame ~= own
	snapMode[entity] = foreign and SNAP_OF_GAME[levelGame] or nil
	if not foreign and NATIVE_SNAP[sf.name(entity)] ~= nil then sf.setGroundSnapMode(entity, NATIVE_SNAP[sf.name(entity)]) end
	if foreign and not snapGiven[entity] then
		snapGiven[entity] = true
		sf.give(entity, "GroundSnap")
	end
end

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
	for _, player in ipairs(players) do setLevelSnap(player, data.game) end
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
