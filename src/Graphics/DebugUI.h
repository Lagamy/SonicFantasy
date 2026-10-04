#pragma once
#include "Camera.h"
#include <GLFW/glfw3.h>

namespace Graphics
{
/*
Dear ImGui overlay, kept apart from the scene renderer: init after the renderer (it needs the GL context and the window's
callbacks, which it chains), draw once per frame after Renderer::run and before the buffer swap, destroy before the renderer.
The panels are one window with a tab per panel. The cursor is captured by the camera until it is freed (main.cpp), so the controls only work then; wantsMouse says whether ImGui
is using the mouse (the cursor is over its window), so a click elsewhere can give the mouse back to the camera.
*/
struct DebugUI
{
	void init(GLFWwindow* pWindow_);
	void draw(const Camera& rCamera_);
	bool wantsMouse() const;
	void destroy();
};
}
