#pragma once 
#include "BasicMesh.h"
#include "Globals.h"
#include "Shader.h"
#include "BasicMesh.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <glad/glad.h>

namespace Graphics 
{
/* Most skin matrices one draw can use: the size of the shared bone buffer (SSBO, binding 1, Shaders/STH2005). */
constexpr uint32_t MaxSkinBones = 256;

struct Renderer { 
	GLuint boneBuffer;

	// Main Graphics Objects init
	Renderer() = default; 
	~Renderer() = default; 
	void init(); 
	/* Draws the scene seen by one camera into the framebuffer and viewport that are bound; clearColor_ false keeps the colour already there (a second camera on the same target) and clears only the depth. */
	void run(Camera& rCamera_, bool clearColor_ = true);
	/* Draws only this entity (no levels, no other entities) seen by the camera, into the framebuffer and viewport that are bound, without clearing: the editor's prefab thumbnails (PrefabThumbnails). The entity needs not be in the game's pool. */
	void drawEntity(Camera& rCamera_, Entity& rEntity_);
	/*
	Draws every enabled camera (Globals::Game::cameras) into its target: first those with a render target, each in its own framebuffer and size, then those that render to the screen, in pool order. A later
	screen camera draws over an earlier one (there are no viewport rectangles yet). The screen is cleared even when no camera renders to it, and the default framebuffer is bound again at the end, for the debug UI. pEditorCamera_ (the Editor's camera, while it is active) replaces the screen cameras: only it is drawn to the screen.
	*/
	void runAll(Camera* pEditorCamera_ = nullptr);
	void destroy();
}; 
}