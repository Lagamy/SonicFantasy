#pragma once

#ifdef EDITOR
#include "Camera.h"
#include <GLFW/glfw3.h>

/*
The editor's free camera (Editor::camera). It is a Camera (so Renderer::runAll draws it) plus the controls, which work with the cursor visible:
W A S D / Space / Ctrl fly (Shift = speedUp times faster); holding the right mouse button turns it and holding the left one drags it sideways and up/down (pan); the cursor is hidden and captured for
the hold, so the drag has no screen edge; the wheel moves it along the look direction and also scales the speed of all of this through zoomScale (zoom in = slower, zoom out = faster), leaving the speed setting alone.
The mouse controls start only on a point the debug UI does not cover.
*/
struct EditorCamera : Camera
{
	/* Pan distance per mouse pixel and zoom distance per wheel notch, as a fraction of speed (units per second), so they follow the speed the wheel sets. Shift multiplies the zoom by speedUp. */
	/* Degrees of turn per mouse pixel (Camera::processMouse scales by the gameplay camera's Globals::Mouse::mouseSensitivity, so onMouseMove corrects for that). */
	float lookSensitivity = 0.05f;

	float panPerSpeed = 0.002f;
	float zoomPerSpeed = 0.1f;

	/* Each wheel notch multiplies zoomScale by this (away from the screen = zoom in = slower, towards it = faster), kept between minZoomScale and maxZoomScale. */
	float speedPerNotch = 1.15f;
	float minZoomScale = 0.001f;
	float maxZoomScale = 100.0f;

	/* Multiplies speed in the fly keys, pan and zoom; only the wheel changes it (not a setting: not shown, not saved), so the sliders do not move with it. */
	float zoomScale = 1.0f;
	float minSpeed = 1.0f;
	float maxSpeed = 100000.0f;

	/* True from a button press until its release; panning tells which one: left = pan, right = turn. */
	bool dragging = false;
	bool panning = false;

	/* The fly keys, scaled by the frame time. */
	void processKeys(GLFWwindow* pWindow_, float deltaTime_);

	/* Starts or ends the button hold (call once per frame); a press the UI had the mouse at does not start one. Returns whether dragging changed. */
	bool updateDrag(GLFWwindow* pWindow_, bool uiWantsMouse_);

	/* A mouse movement in pixels: turns the camera (right button) or pans it (left); nothing unless dragging. */
	void onMouseMove(GLFWwindow* pWindow_, float xOffset_, float yOffset_);

	/* A wheel movement (GLFW yoffset): moves along the look direction, then scales speed. */
	void onScroll(GLFWwindow* pWindow_, float yOffset_);

	EditorCamera(glm::vec3 position_) : Camera(position_) {}
	EditorCamera() = default;
};
#endif
