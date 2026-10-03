#pragma once
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>

struct Camera {
	GLuint UBOId;
    glm::vec3 position; 
    glm::vec3 target {0.0f, 0.0f, 0.0f}; 
    glm::vec3 up {0.0f, 1.0f, 0.0f};
	glm::vec3 front{0.0f, 0.0f, -1.0f};

	float yaw = -90.0f;
	float pitch = 0.0f;
    
	glm::mat4 view;
    glm::mat4 projection;

	void processMouse(float xoffset, float yoffset); 

    void updateView();
    void updateProjection(float aspect);
	void updateUBO(); 

	void move(glm::vec3 by_);
	void set(glm::vec3 position_); 
	void destroy(); 
	
	Camera(glm::vec3 position_);  
	Camera() = default; 
};
