#pragma once

/*
The fixed-step simulation: the entity behaviours (EntityBehavior.h), then the physics and the collision system, once per gameplay tick, and the removal of the entities marked for destroy at the start of every tick
(destroyedCleanUp). A tick does not run once a frame: gameplay has its own rate, tickRate times a second whatever the frame rate is. update(frameDeltaTime_) adds the frame's time to an accumulator and runs a
tick for every whole tickTime in it (at most maxTicksPerFrame; after a long frame the rest of the time is dropped, so the game slows down instead of spiralling), so a frame can run no tick or several. It runs
every frame right after the inputs (main.cpp); what is left in the accumulator is how far the frame is into the next tick, interpolationAlpha, which the renderer uses to draw entities between their positions
at the last two ticks or past the latest (Entity::renderPosition, renderRotation; see extrapolate). Every tick, before anything moves, each entity's previousPosition and previousRotation are set to its position and rotation.
tickRate is a guess (60, the rate the games' own logic is assumed to run at).
*/
struct GameplaySystem
{
	float tickRate = 60.0f;
	int maxTicksPerFrame = 5;
	float accumulator = 0.0f;

	/* How the renderer shows the state of the latest tick: extrapolated forward by the time since it (no tick of lag, may overshoot a little when a move stops) or, off, interpolated from the one before (smooth, a tick behind). */
	bool extrapolate = true;

	/* An entity farther from the camera than the render distance (Camera::farPlane) is not active: its behaviours, physics and collision (its moves, its contacts and interactions) do not run (Entity::active, SpatialIndex::updateActive), unless it has behaviorNotCulled. Only the distance counts, not the view: what is behind the camera stays active. */
	bool cullFarEntities = true;

	/* For the debug UI: how many ticks the last frame ran and how long they took together (milliseconds). */
	int ticksLastFrame = 0;
	float tickMillisecondsLastFrame = 0.0f;

	float tickTime() const
	{
		return 1.0f / this->tickRate;
	}

	/* How far into the next tick the frame is, 0 to 1: the accumulator over the tick's time. */
	float interpolationAlpha() const
	{
		return this->accumulator * this->tickRate;
	}

	void update(float frameDeltaTime_);

	/* Removes the entities marked for destroy; the first thing a tick does. */
	void destroyedCleanUp();

	/* One tick: every entity's previous position is kept, then every behaviour of every entity that has some and is active (cullFarEntities) runs once, then the physics and the collision system. */
	void tick();
};
