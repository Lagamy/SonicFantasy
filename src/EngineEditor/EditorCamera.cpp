#include "EditorCamera.h"
#include "Globals.h"
#include <cmath>

void EditorCamera::processKeys(GLFWwindow* pWindow_, float deltaTime_)
{
	/* Shift is full speed: speedUp times speed, without the zoomScale slowdown near things. */
	float distance = this->speed * this->zoomScale * deltaTime_;
	if(glfwGetKey(pWindow_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
		distance = this->speed * this->speedUp * deltaTime_;
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
	bool middleDown = glfwGetMouseButton(pWindow_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
	bool down = this->dragging ? (this->panning ? middleDown : rightDown) : (rightDown || middleDown);
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

/* How fast zoomScale closes on its target, per second (the gap shrinks by 1 - e^-rate each second; a guess, fast enough to follow the camera and slow enough to hide jumps). */
constexpr float SpeedScaleRate = 8.0f;

/* Eased in log space, so a change feels the same at any scale. */
void EditorCamera::updateSpeedScale(float nearestDistance_, float deltaTime_)
{
	float target = glm::clamp(nearestDistance_ / this->referenceDistance, this->minZoomScale, this->maxZoomScale);
	this->zoomScale *= std::pow(target / this->zoomScale, 1.0f - std::exp(-SpeedScaleRate * deltaTime_));
}

void EditorCamera::onScroll(GLFWwindow* pWindow_, float yOffset_)
{
	float step = this->zoomPerSpeed * this->speed * this->zoomScale;
	if(glfwGetKey(pWindow_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
		step = this->zoomPerSpeed * this->speed * this->speedUp;
	this->move(this->front * yOffset_ * step);
}
