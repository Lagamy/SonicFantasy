#include "EditorCamera.h"
#include "Globals.h"
#include <cmath>

void EditorCamera::processKeys(GLFWwindow* pWindow_, float deltaTime_)
{
	float distance = this->speed * this->zoomScale * deltaTime_;
	if(glfwGetKey(pWindow_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
		distance *= this->speedUp;
	if(glfwGetKey(pWindow_, GLFW_KEY_W) == GLFW_PRESS)
		this->move(this->front * distance);
	if(glfwGetKey(pWindow_, GLFW_KEY_S) == GLFW_PRESS)
		this->move(-this->front * distance);

	glm::vec3 right = glm::normalize(glm::cross(this->front, this->up));
	if(glfwGetKey(pWindow_, GLFW_KEY_A) == GLFW_PRESS)
		this->move(-right * distance);
	if(glfwGetKey(pWindow_, GLFW_KEY_D) == GLFW_PRESS)
		this->move(right * distance);

	if(glfwGetKey(pWindow_, GLFW_KEY_SPACE) == GLFW_PRESS)
		this->move(glm::vec3(0.0f, 1.0f, 0.0f) * distance);
	if(glfwGetKey(pWindow_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
		this->move(-glm::vec3(0.0f, 1.0f, 0.0f) * distance);
}

bool EditorCamera::updateDrag(GLFWwindow* pWindow_, bool uiWantsMouse_)
{
	bool rightDown = glfwGetMouseButton(pWindow_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
	bool leftDown = glfwGetMouseButton(pWindow_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
	bool down = this->dragging ? (this->panning ? leftDown : rightDown) : (rightDown || leftDown);
	if(down && !this->dragging && !uiWantsMouse_)
	{
		this->panning = !rightDown;
		/* The cursor is hidden and captured for the hold; the next position is the baseline so the camera does not jump (mouseCallback, firstMouse). */
		this->dragging = true;
		glfwSetInputMode(pWindow_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		Globals::Mouse::firstMouse = true;
		return true;
	}
	if(!down && this->dragging)
	{
		this->dragging = false;
		glfwSetInputMode(pWindow_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		return true;
	}
	return false;
}

void EditorCamera::onMouseMove(GLFWwindow* pWindow_, float xOffset_, float yOffset_)
{
	if(!this->dragging) return;
	if(this->panning)
	{
		/* Pan: the scene follows the cursor, so the camera goes the opposite way along its own right and up axes. */
		glm::vec3 right = glm::normalize(glm::cross(this->front, this->up));
		glm::vec3 cameraUp = glm::cross(right, this->front);
		this->move((-right * xOffset_ + cameraUp * yOffset_) * this->panPerSpeed * this->speed * this->zoomScale);
		return;
	}

	/* processMouse multiplies by the gameplay sensitivity; the offsets are scaled so that the result is lookSensitivity degrees per pixel. */
	float scale = this->lookSensitivity / Globals::Mouse::mouseSensitivity;
	this->processMouse(xOffset_ * scale, yOffset_ * scale);
}

void EditorCamera::onScroll(GLFWwindow* pWindow_, float yOffset_)
{
	float step = this->zoomPerSpeed * this->speed * this->zoomScale;
	if(glfwGetKey(pWindow_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
		step *= this->speedUp;
	this->move(this->front * yOffset_ * step);

	/* Scrolling forward (zoom in) slows fly, pan and zoom down, so close to a thing the camera is fine to steer; scrolling back speeds them up. Exponential, so a notch feels the same at any scale. It is zoomScale, not speed, that changes: the fly speed slider stays where the user put it. */
	this->zoomScale = glm::clamp(this->zoomScale * std::pow(this->speedPerNotch, -yOffset_), this->minZoomScale, this->maxZoomScale);
}
