-- Generic onTouch functions for the editor's bound area rules (Inspector > BoundArea > Tags and behaviours). Each is fn(parent, contact):
-- parent is the entity that owns the area, contact the entity it touched (sf.none when it touched the level). Made-up helpers, not the original game's objects.

-- Writes both names to the Output tab: to see that a rule fires.
sf.onTouch("Basics/Log", function(parent, contact)
	local contactName = contact ~= sf.none and sf.name(contact) or "(level)"
	print(string.format("touch: %s touched %s", sf.name(parent), contactName))
end)

-- Removes the entity that owns the area.
sf.onTouch("Basics/DestroySelf", function(parent, contact)
	if sf.exists(parent) then sf.destroy(parent) end
end)

-- One more ring.
sf.onTouch("Basics/AddRing", function(parent, contact)
	sf.setRings(sf.rings() + 1)
end)

-- One more ring, and removes the entity that owns the area (a pickup). Each tick it is touched counts once, so a second touch before the removal is ignored.
-- The mark is in the entity's own sf.state, which a level reset gives back with the entity; a table kept here would outlive it.
sf.onTouch("Basics/Collect", function(parent, contact)
	if not sf.exists(parent) then return end
	local state = sf.state(parent)
	if state.collected then return end
	state.collected = true
	sf.setRings(sf.rings() + 1)
	sf.destroy(parent)
end)
