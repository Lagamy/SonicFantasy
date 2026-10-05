-- Entry script of the Shadow the Hedgehog gameplay (Scripting.h, main.cpp). main.lua's attachPlayer calls attachShadowPlayer(entity), defined in player.lua, for Shadow; the stage's objects have no behaviour yet.
sf.include("shadow/animation.lua")
sf.include("shadow/replay.lua")
sf.include("shadow/replays/live_run.lua")
sf.include("shadow/replays/live_accel.lua")
sf.include("shadow/replays/live_half.lua")
sf.include("shadow/player.lua")
