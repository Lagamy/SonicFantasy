#include "Camera.h"
#include "Globals.h"

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
	this->updateProjection((float)Globals::App::windowWidth / (float)Globals::App::windowHeight);
	this->updateUBO();	
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
        glm::radians(45.0f),
        aspect,
        0.1f,
        100.0f
    );
}

void Camera::move(glm::vec3 by_) { 
	this->position += by_; 
	this->updateView(); 
	this->updateUBO(); 
}

void Camera::set(glm::vec3 position_) { 
	this->position = position_; 
	this->updateView();
	this->updateUBO(); 
}; 

void Camera::destroy() { 
	glDeleteBuffers(1, &this->UBOId); 
}
