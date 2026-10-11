-- Entry script of the SADX gameplay (Scripting.h, main.cpp). Everything the game does with its objects and the player is in the files below; the engine calls
-- the prefab behaviours (sf.prefabBehavior) for every object a level makes and main.lua's attachPlayer calls attachSonicPlayer(entity) for Sonic; both are defined there.
sf.include("sadx/objects.lua")
sf.include("sadx/replay.lua")
sf.include("sadx/replays/sadx_1.lua")
sf.include("sadx/replays/sadx_2.lua")
sf.include("sadx/replays/sadx_3.lua")
sf.include("sadx/replays/sadx_4.lua")
sf.include("sadx/replays/sadx_homing.lua")
sf.include("sadx/player.lua")
