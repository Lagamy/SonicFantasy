#include "RenderTarget.h"
#include <format>
#include <stdexcept>

namespace Graphics
{
RenderTarget::RenderTarget(uint32_t width_, uint32_t height_)
{
	this->create(width_, height_);
}

void RenderTarget::create(uint32_t width_, uint32_t height_)
{
	if(width_ == 0 || height_ == 0)
	{
		throw std::runtime_error(std::format("RenderTarget: size {}x{} is not valid.", width_, height_));
	}
	this->width = width_;
	this->height = height_;

	glCreateTextures(GL_TEXTURE_2D, 1, &this->colorTexture);
	glTextureStorage2D(this->colorTexture, 1, GL_SRGB8_ALPHA8, width_, height_);
	glTextureParameteri(this->colorTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(this->colorTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(this->colorTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(this->colorTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	/* A texture view needs a name that was never bound, so glGenTextures and not glCreateTextures. */
	glGenTextures(1, &this->displayTexture);
	glTextureView(this->displayTexture, GL_TEXTURE_2D, this->colorTexture, GL_RGBA8, 0, 1, 0, 1);
	glTextureParameteri(this->displayTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(this->displayTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(this->displayTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(this->displayTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glCreateRenderbuffers(1, &this->depthRenderbuffer);
	glNamedRenderbufferStorage(this->depthRenderbuffer, GL_DEPTH_COMPONENT24, width_, height_);

	glCreateFramebuffers(1, &this->framebuffer);
	glNamedFramebufferTexture(this->framebuffer, GL_COLOR_ATTACHMENT0, this->colorTexture, 0);
	glNamedFramebufferRenderbuffer(this->framebuffer, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, this->depthRenderbuffer);
	if(glCheckNamedFramebufferStatus(this->framebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		this->destroy();
		throw std::runtime_error(std::format("RenderTarget: the {}x{} framebuffer is not complete.", width_, height_));
	}
}

void RenderTarget::resize(uint32_t width_, uint32_t height_)
{
	this->destroy();
	this->create(width_, height_);
}

void RenderTarget::bind() const
{
	glBindFramebuffer(GL_FRAMEBUFFER, this->framebuffer);
	glViewport(0, 0, this->width, this->height);
}

void RenderTarget::destroy()
{
	glDeleteFramebuffers(1, &this->framebuffer);
	glDeleteRenderbuffers(1, &this->depthRenderbuffer);
	glDeleteTextures(1, &this->displayTexture);
	glDeleteTextures(1, &this->colorTexture);
	this->framebuffer = 0;
	this->depthRenderbuffer = 0;
	this->displayTexture = 0;
	this->colorTexture = 0;
}
}
