#pragma once
#include <vector>
#include "PoolId.h"
#include "Uva.h"

namespace Graphics
{
/*
The effect meshes Jump_ballA and Jump_ballB (EFFCHRBALLA.DFF / EFFCHRBALLB.DFF of common/CommonEffect.one) attached to an entity: docs/STH2005_PLAYER_REFERENCE.md "Jump_ballA / Jump_ballB decoded" has what is decompile, disc file or hypothesis.
An effect is one of three kinds: JumpBallA (the jump's ball, ballA mesh beside the entity), DashBallB (the jump dash's ball, ballB mesh beside the entity) and FallBallB (the Fall's ball, ballB mesh beside the entity, turned down). `setActive(entity, kind, on)` starts it (nothing if that entity already has that kind) or ends it (the original's stop request starts a fade out: the ball stays and fades to nothing, then goes).
Each ball is its own draw, after the entities (Renderer::run, like DashTrail): textured and additive (source alpha, destination one), no depth write, no culling, the mesh's texture ef_chbl scrolling by its UV animation,
tinted (255, 128, 0), faded in from 0 to 255 over FadeInSeconds and, once stopped, out over FadeOutSeconds. The uniforms are the STH2005 shader's uvOffset and effectTint.
The balls are placed by the entity's transform, the RenderWareYaw turn and a fixed offset (not the animation's pose); the mesh's root frame is not applied.
*/
struct EffectBalls
{
	enum Kind
	{
		JumpBallA,
		DashBallB,
		FallBallB
	};

	/* Starts or ends the entity's effect of that kind. Loads the meshes on the first start. */
	void setActive(PoolId entityId_, Kind kind_, bool on_);

	/* One gameplay tick (deltaTime_ seconds): ages the effects, removes those whose entity is gone. */
	void tick(float deltaTime_);

	void draw();
	void destroy();

private:
	struct Ball
	{
		PoolId entityId;
		Kind kind;
		float age;
		/* Seconds since the stop request, negative while running. */
		float stoppedFor = -1.0f;
	};

	/* The instance holding a ball mesh (made with Loading::addONEInstance) and the UV animations of its .UVA. */
	struct Mesh
	{
		PoolId instanceId;
		std::vector<Renderware::UvAnimation> uvAnimations;
	};

	void loadMesh(Mesh& rMesh_, bool ballB_);

	std::vector<Ball> balls;
	Mesh meshes[2];
};
}
