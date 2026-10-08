#pragma once
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include <array>
#include "PoolId.h"
#include "Constants.h"

/* True if a sphere is not fully outside any of the planes (see Camera::frustumPlanes). May keep a sphere near a corner that misses the frustum. */
inline bool sphereInFrustum(const std::array<glm::vec4, 6>& rPlanes_, const glm::vec3& rCenter_, float radius_)
{
	for(const glm::vec4& rPlane : rPlanes_)
	{
		if(glm::dot(glm::vec3(rPlane), rCenter_) + rPlane.w < -radius_) return false;
	}
	return true;
}

namespace Graphics
{
struct RenderTarget;
}

struct Camera {
	GLuint UBOId;
    glm::vec3 position; 
    glm::vec3 target {0.0f, 0.0f, 0.0f}; 
    glm::vec3 up {0.0f, 1.0f, 0.0f};
	glm::vec3 front{0.0f, 0.0f, -1.0f};

	/* Free-fly speed in units per second; speedUp multiplies it while the speed-up key is held. */
	float speed = 100.0f;
	float speedUp = 10.0f;

	float yaw = -90.0f;
	float pitch = 0.0f;
	/*
	Near plane of the projection. Depth precision (24 bit) is worst far away and grows in proportion to this: the smallest
	depth difference that is told apart at distance z is about z * z / (nearPlane * 2^24), so 1.0 instead of 0.1 is ten times
	finer everywhere. Geometry closer to the camera than this is cut off.
	*/
	float nearPlane = 1.0f;

	/* Far plane of the projection; it is also the one render distance: levels stream by it and entities are drawn and active within it (the Camera tab changes it). */
	float farPlane = 50000.0f;

	/* Vertical field of view in degrees. */
	float fov = 45.0f;

	/* Whether the renderer draws this camera (Renderer::runAll). */
	bool enabled = true;

	/* Where it renders: a Graphics::RenderTarget in Globals::Game::renderTargets, or UninitializedPoolId = the screen (the window's framebuffer). Set it with setRenderTarget / setRenderToScreen. */
	PoolId renderTargetId = UninitializedPoolId;

	/* The entity that carries this camera as its module (Entity::cameraId), UninitializedPoolId for a camera made by hand. */
	PoolId entityId = UninitializedPoolId;
    
	glm::mat4 view;
    glm::mat4 projection;

	void processMouse(float xoffset, float yoffset); 

    void updateView();
    void updateProjection(float aspect);

	/* The render target it renders to, nullptr for the screen; also nullptr when its target was removed (the camera then falls back to the screen). */
	Graphics::RenderTarget* renderTarget() const;

	/* The size of what it renders to: its render target, or the window while it renders to the screen (also when its target was removed). */
	glm::uvec2 viewportSize() const;

	/* Rebuilds the projection from fov, the planes and the aspect of viewportSize, and uploads it. Call it after any of them changed, and when the window is resized (screen cameras). */
	void refreshProjection();

	/* Render into a render target (its size decides the aspect), or back to the screen. */
	void setRenderTarget(PoolId targetId_);
	void setRenderToScreen();
	void updateUBO(); 

	/*
	The six planes of the view frustum (left, right, bottom, top, near, far) as (normal, d), normals pointing inside and
	normalized, so that dot(normal, point) + d >= 0 inside. The far plane is at farPlane.
	*/
	std::array<glm::vec4, 6> frustumPlanes() const;

	/* move and set put the camera's entity (entityId) at the new position too, so the two never differ (Entity::setPosition / moveTo move the camera the same way). */
	void move(glm::vec3 by_);
	void set(glm::vec3 position_); 
	void moveEntity();
	void destroy(); 
	
	Camera(glm::vec3 position_);  
	Camera() = default; 
};
