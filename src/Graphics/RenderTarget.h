#pragma once
#include <glad/glad.h>
#include <cstdint>

namespace Graphics
{
/*
A texture a camera can render into instead of the screen (Camera::setRenderTarget), stored in Globals::Game::renderTargets and found by its PoolId. A framebuffer with a colour texture (SRGB8_ALPHA8, like
the window's framebuffer, so the scene is encoded the same way) and a 24 bit depth renderbuffer.
displayTexture is a view of the colour texture as plain RGBA8, without the sRGB decode: for showing it with ImGui (which draws with the sRGB framebuffer off, see DebugUI::draw) or any code that
wants the stored, display-space values; sample colorTexture instead to get linear values.
resize recreates the storage (the contents are lost); the PoolId stays. destroy is called by the pool on remove.
*/
struct RenderTarget
{
	GLuint framebuffer = 0;
	GLuint colorTexture = 0;
	GLuint displayTexture = 0;
	GLuint depthRenderbuffer = 0;
	uint32_t width = 0;
	uint32_t height = 0;

	RenderTarget(uint32_t width_, uint32_t height_);
	RenderTarget() = default;

	void create(uint32_t width_, uint32_t height_);
	void resize(uint32_t width_, uint32_t height_);
	/* Binds the framebuffer for drawing and sets the viewport to its whole size. */
	void bind() const;
	void destroy();
};
}
