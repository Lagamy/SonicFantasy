#pragma once
#include <cstdint>
#include <glad/glad.h>

namespace Graphics
{
/*
Per frame counters of what the renderer asks of the GL, and the times of the frame's parts, printed to stderr once a second
by main.cpp when the environment variable SF_STATS is set. A call site notes what it is about to do (program, vao, texture,
draw) before the GL call; "changes" count only the calls that differ from the previous one, so calls - changes is what
state caching would save. Times are in milliseconds, summed over the frames of the report and divided when printed.
*/
struct RenderStats
{
	uint32_t draws = 0;
	uint64_t indices = 0;
	uint32_t programCalls = 0;
	uint32_t programChanges = 0;
	uint32_t vaoCalls = 0;
	uint32_t vaoChanges = 0;
	uint32_t textureCalls = 0;
	uint32_t textureChanges = 0;
	uint32_t uniformUploads = 0;
	uint32_t boneUploads = 0;
	uint32_t entitiesDrawn = 0;
	uint32_t levelPiecesDrawn = 0;

	GLuint lastProgram = 0;
	GLuint lastVAO = 0;
	GLuint lastTexture = 0;

	void program(GLuint id_)
	{
		this->programCalls++;
		if(id_ != this->lastProgram) this->programChanges++;
		this->lastProgram = id_;
	}

	void vao(GLuint id_)
	{
		this->vaoCalls++;
		if(id_ != this->lastVAO) this->vaoChanges++;
		this->lastVAO = id_;
	}

	void texture(GLuint id_)
	{
		this->textureCalls++;
		if(id_ != this->lastTexture) this->textureChanges++;
		this->lastTexture = id_;
	}

	void draw(uint64_t indexCount_)
	{
		this->draws++;
		this->indices += indexCount_;
	}

	void resetFrame()
	{
		this->draws = 0;
		this->indices = 0;
		this->programCalls = 0;
		this->programChanges = 0;
		this->vaoCalls = 0;
		this->vaoChanges = 0;
		this->textureCalls = 0;
		this->textureChanges = 0;
		this->uniformUploads = 0;
		this->boneUploads = 0;
		this->entitiesDrawn = 0;
		this->levelPiecesDrawn = 0;
	}
};

inline RenderStats renderStats;
}
