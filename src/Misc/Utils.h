#pragma once

#include <stdexcept>
#ifdef EDITOR
#include "Debug.h"
#include "Editor.h"
#endif
#include "Globals.h"

/**
 * \brief Initializes GLAD. Must be called AFTER creating a GLFW OpenGL context.
 */
inline void initGlad(GLFWwindow* window) {
    // Load OpenGL function pointers using GLFW
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        throw std::runtime_error("Failed to initialize GLAD");
    }

#ifdef EDITOR
    // Setup debug messages for OpenGL
    if (glDebugMessageCallback) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(debugCallback, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
    }
#endif
}

inline void framebufferSizeCallback(GLFWwindow*, int width, int height) // window and buffer resize 
{
    Globals::App::windowWidth = width;
    Globals::App::windowHeight = height;
	Globals::Mouse::lastX = width / 2.0f;
	Globals::Mouse::lastY = height / 2.0f;

	/* Only the cameras that render to the screen follow the window; a render target keeps its size. */
	for(uint32_t i = 0; i < Globals::Game::cameras.objects.size(); i++)
	{
		if(Globals::Game::cameras.alive[i] && Globals::Game::cameras.objects[i].renderTargetId == UninitializedPoolId) Globals::Game::cameras.objects[i].refreshProjection();
	}
#ifdef EDITOR
	if(editor.initialized) editor.camera.refreshProjection();
#endif

    glViewport(0, 0, width, height);
	
}

/*
When the window gets focus again the cursor position can be far from the last one reported, which would turn the camera
by the difference. The next position is taken as the new baseline instead (mouseCallback, firstMouse).
*/
inline void focusCallback(GLFWwindow*, int focused)
{
	if (focused) Globals::Mouse::firstMouse = true;
}

inline void scrollCallback(GLFWwindow*, double, double yoffset)
{
#ifdef EDITOR
	editor.onScroll(static_cast<float>(yoffset));
#endif
}

inline void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
#ifdef EDITOR
	if (editor.cursorFree) return;
#endif
    if (Globals::Mouse::firstMouse) {
		Globals::Mouse::lastX = xpos;
		Globals::Mouse::lastY = ypos;
		Globals::Mouse::firstMouse = false;
    }

    float xoffset = xpos - Globals::Mouse::lastX;
    float yoffset = ypos - Globals::Mouse::lastY;

	Globals::Mouse::lastX = xpos;
	Globals::Mouse::lastY = ypos;

	/* While the editor is active the mouse turns its camera, not the gameplay one. */
#ifdef EDITOR
	if (editor.active)
	{
		if (editor.initialized) editor.onMouse(xoffset, yoffset);
		return;
	}
#endif
	if (Globals::Game::mainCamera != UninitializedPoolId) // Main camera exists
	{
    	Globals::Game::cameras[Globals::Game::mainCamera].processMouse(xoffset, yoffset);
	}
}

/**
 * \brief Creates a new window using GLFW3.
 */
inline GLFWwindow* createWindow(const uint32_t width, const uint32_t height) {
    if (GLFW_TRUE != glfwInit())
        throw std::runtime_error("Failed to initialize GLFW");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);

    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
#ifdef EDITOR
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(
        static_cast<int>(width),
        static_cast<int>(height),
        "OpenGL Example",
        nullptr,
        nullptr
    );

    if (!window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

	
    glfwMakeContextCurrent(window);
	glfwSwapInterval(Globals::App::vsync ? 1 : 0);
	glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

	glfwSetCursorPosCallback(window, mouseCallback);
	glfwSetScrollCallback(window, scrollCallback);
	glfwSetWindowFocusCallback(window, focusCallback);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);	

    return window;
}


/**
 * \brief Destroys the window and terminates GLFW3.
 */
inline void destroyWindow(GLFWwindow* window) {
    glfwDestroyWindow(window);
    glfwTerminate();
}



