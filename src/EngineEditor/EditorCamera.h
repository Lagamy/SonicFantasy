#pragma once

#ifdef EDITOR
#include "Camera.h"
#include <GLFW/glfw3.h>

/*
The editor's free camera (Editor::camera). It is a Camera (so Renderer::runAll draws it) plus the controls, which work with the cursor visible:
W A S D / Space / Ctrl fly (Shift = full speed: speedUp times speed, without the zoomScale slowdown); holding the right mouse button turns it and holding the middle one drags it sideways and up/down (pan); the cursor is hidden and captured for
the hold, so the drag has no screen edge; the wheel moves it along the look direction. All of this is scaled by zoomScale, which follows the distance to the selected entity, else to the model the camera looks at (updateSpeedScale): close to it the camera is slow, far from everything it is fast.
The mouse controls start only on a point the debug UI does not cover.
*/
struct EditorCamera : Camera
{
	/* Pan distance per mouse pixel and zoom distance per wheel notch, as a fraction of speed (units per second), so they follow the speed the wheel sets. Shift makes the zoom speedUp times speed, unscaled. */
	/* Degrees of turn per mouse pixel (Camera::processMouse scales by the gameplay camera's Globals::Mouse::mouseSensitivity, so onMouseMove corrects for that). */
	float lookSensitivity = 0.05f;

	float panPerSpeed = 0.002f;
	float zoomPerSpeed = 0.1f;

	/* zoomScale is the distance to the nearest entity divided by referenceDistance (the distance at which the speed settings apply as they are), kept between minZoomScale and maxZoomScale. */
	float referenceDistance = 10.0f;
	float minZoomScale = 0.001f;
	float maxZoomScale = 10.0f;

	/* Multiplies speed in the fly keys, pan and zoom; set by updateSpeedScale, not a setting (not shown, not saved). */
	float zoomScale = 1.0f;
	float minSpeed = 1.0f;
	float maxSpeed = 100000.0f;

	/* True from a button press until its release; panning tells which one: middle = pan, right = turn. */
	bool dragging = false;
	bool panning = false;

	/* The fly keys, scaled by the frame time. */
	void processKeys(GLFWwindow* pWindow_, float deltaTime_);

	/* Starts or ends the button hold (call once per frame); a press the UI had the mouse at does not start one. Returns whether dragging changed. */
	bool updateDrag(GLFWwindow* pWindow_, bool uiWantsMouse_);

	/* A mouse movement in pixels: turns the camera (right button) or pans it (middle); nothing unless dragging. */
	void onMouseMove(GLFWwindow* pWindow_, float xOffset_, float yOffset_);

	/* Moves zoomScale towards nearestDistance_ / referenceDistance (call once per frame with the distance to the model the camera looks at); it eases there, so passing a large object does not make the speed jump. */
	void updateSpeedScale(float nearestDistance_, float deltaTime_);

	/* A wheel movement (GLFW yoffset): moves along the look direction. */
	void onScroll(GLFWwindow* pWindow_, float yOffset_);

	EditorCamera(glm::vec3 position_) : Camera(position_) {}
	EditorCamera() = default;
};
#endif
