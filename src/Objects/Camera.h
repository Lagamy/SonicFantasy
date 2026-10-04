#pragma once
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include <array>

/* True if a sphere is not fully outside any of the planes (see Camera::frustumPlanes). May keep a sphere near a corner that misses the frustum. */
inline bool sphereInFrustum(const std::array<glm::vec4, 6>& rPlanes_, const glm::vec3& rCenter_, float radius_)
{
	for(const glm::vec4& rPlane : rPlanes_)
	{
		if(glm::dot(glm::vec3(rPlane), rCenter_) + rPlane.w < -radius_) return false;
	}
	return true;
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
    
	glm::mat4 view;
    glm::mat4 projection;

	void processMouse(float xoffset, float yoffset); 

    void updateView();
    void updateProjection(float aspect);
	void updateUBO(); 

	/*
	The six planes of the view frustum (left, right, bottom, top, near, far) as (normal, d), normals pointing inside and
	normalized, so that dot(normal, point) + d >= 0 inside. The far plane is at farPlane.
	*/
	std::array<glm::vec4, 6> frustumPlanes() const;

	void move(glm::vec3 by_);
	void set(glm::vec3 position_); 
	void destroy(); 
	
	Camera(glm::vec3 position_);  
	Camera() = default; 
};
