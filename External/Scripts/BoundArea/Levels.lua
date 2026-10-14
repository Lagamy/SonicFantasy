-- onTouch functions that act on levels. Each is fn(parent, contact), see Basics.lua.

-- Resets every level that exists (destroyed and built again from its recipe at the start of the next tick). Once per entry ({entry = true}): the reset builds the trigger again with the player still inside it, which must not reset again.
local resetAll = sf.onTouch("Levels/ResetAll", function(parent, contact)
	for _, level in ipairs(sf.levels()) do
		sf.resetLevel(level)
	end
end, {entry = true})

-- The trigger: give this behaviour to a shape (BoundArea tab) of an entity placed where the freeze should happen; the player's body touching it runs Levels/ResetAll.
local tagLevelReset = sf.tag("LevelReset")
local tagBody = sf.tag("Body")
sf.collisionBehavior("LevelReset", {tags = {tagLevelReset}, interactions = {[tagBody] = resetAll}})
