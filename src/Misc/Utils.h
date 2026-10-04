#pragma once

#include <stdexcept>
#include "Debug.h"
#include "Globals.h"

/**
 * \brief Initializes GLAD. Must be called AFTER creating a GLFW OpenGL context.
 */
inline void initGlad(GLFWwindow* window) {
    // Load OpenGL function pointers using GLFW
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        throw std::runtime_error("Failed to initialize GLAD");
    }

    // Setup debug messages for OpenGL
    if (glDebugMessageCallback) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(debugCallback, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
    }
}

inline void framebufferSizeCallback(GLFWwindow*, int width, int height) // window and buffer resize 
{
    Globals::App::windowWidth = width;
    Globals::App::windowHeight = height;
	Globals::Mouse::lastX = width / 2.0f;
	Globals::Mouse::lastY = height / 2.0f;

	for(Camera& camera : Globals::Game::cameras)
	{
		camera.updateProjection((float)width / (float)height);
		camera.updateUBO();
	}

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

inline void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
	if (Globals::Mouse::cursorFree) return;
    if (Globals::Mouse::firstMouse) {
		Globals::Mouse::lastX = xpos;
		Globals::Mouse::lastY = ypos;
		Globals::Mouse::firstMouse = false;
    }

    float xoffset = xpos - Globals::Mouse::lastX;
    float yoffset = ypos - Globals::Mouse::lastY;

	Globals::Mouse::lastX = xpos;
	Globals::Mouse::lastY = ypos;

	if (!Globals::Game::cameras.empty()) // Player camera exists
	{
    	Globals::Game::cameras[0].processMouse(xoffset, yoffset);
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
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

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
	glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

	glfwSetCursorPosCallback(window, mouseCallback);
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



