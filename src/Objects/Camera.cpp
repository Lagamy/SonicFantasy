#include "Camera.h"
#include "Globals.h"
#include "API.h"

Camera::Camera(glm::vec3 position_) { 
	this->position = position_; 

	glCreateBuffers(1, &this->UBOId);
	glNamedBufferStorage(
    	this->UBOId,
    	sizeof(glm::mat4) * 2,
    	nullptr,
    	GL_DYNAMIC_STORAGE_BIT
	);

	this->updateView();
	this->refreshProjection();
}

void Camera::processMouse(float xoffset, float yoffset) {
    xoffset *= Globals::Mouse::mouseSensitivity;
    yoffset *= Globals::Mouse::mouseSensitivity;

    yaw += xoffset;
    pitch -= yoffset;

    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    glm::vec3 dir;
    dir.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    dir.y = sin(glm::radians(pitch));
    dir.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));

    front = glm::normalize(dir);
	this->updateView();
	this->updateUBO();
}

void Camera::updateUBO() { 
	glNamedBufferSubData(
    	this->UBOId,
    	0,
    	sizeof(glm::mat4),
    	glm::value_ptr(this->view)
	);

	glNamedBufferSubData(
    	this->UBOId,
    	sizeof(glm::mat4),
    	sizeof(glm::mat4),
    	glm::value_ptr(this->projection)
	);
}

void Camera::updateView() {
	this->target = this->position + this->front;
	this->view = glm::lookAt(this->position, this->target, this->up);
}

void Camera::updateProjection(float aspect) {
    this->projection = glm::perspective(
        glm::radians(this->fov),
        aspect,
        this->nearPlane,
        this->farPlane
    );
}

Graphics::RenderTarget* Camera::renderTarget() const {
	PoolId id = this->renderTargetId;
	Pool<Graphics::RenderTarget>& rTargets = Globals::Game::renderTargets;
	bool exists = id != UninitializedPoolId && id.id < rTargets.alive.size() && rTargets.alive[id.id] && rTargets.generation[id.id] == id.generation;
	return exists ? &rTargets.objects[id.id] : nullptr;
}

glm::uvec2 Camera::viewportSize() const {
	if(const Graphics::RenderTarget* pTarget = this->renderTarget())
	{
		return glm::uvec2(pTarget->width, pTarget->height);
	}
	return glm::uvec2(Globals::App::windowWidth, Globals::App::windowHeight);
}

void Camera::refreshProjection() {
	glm::uvec2 size = this->viewportSize();
	this->updateProjection(static_cast<float>(size.x) / static_cast<float>(size.y));
	this->updateUBO();
}

void Camera::setRenderTarget(PoolId targetId_) {
	this->renderTargetId = targetId_;
	this->refreshProjection();
}

void Camera::setRenderToScreen() {
	this->renderTargetId = UninitializedPoolId;
	this->refreshProjection();
}

/*
Planes from the rows of projection * view (Gribb and Hartmann): a point is inside when -w <= x, y, z <= w in clip space
(OpenGL), so each plane is row 3 plus or minus one of rows 0 to 2.
*/
std::array<glm::vec4, 6> Camera::frustumPlanes() const {
	glm::mat4 m = this->projection * this->view;
	glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
	glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
	glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
	glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);
	std::array<glm::vec4, 6> planes = {row3 + row0, row3 - row0, row3 + row1, row3 - row1, row3 + row2, row3 - row2};
	for(glm::vec4& rPlane : planes)
	{
		rPlane /= glm::length(glm::vec3(rPlane));
	}
	return planes;
}

void Camera::move(glm::vec3 by_) { 
	this->position += by_; 
	this->updateView(); 
	this->updateUBO(); 
	this->moveEntity();
}

/* The camera's entity (if it has one) goes to the camera's position; Entity::moveCameraModule does the reverse, and each stops when the other is already there. */
void Camera::moveEntity() {
	if(!entityExists(this->entityId)) return;
	Entity& rEntity = getEntity(this->entityId);
	if(rEntity.position != this->position) rEntity.setPosition(this->position);
}

void Camera::set(glm::vec3 position_) { 
	this->position = position_; 
	this->updateView();
	this->updateUBO(); 
	this->moveEntity();
}; 

void Camera::destroy() { 
	glDeleteBuffers(1, &this->UBOId); 
}
