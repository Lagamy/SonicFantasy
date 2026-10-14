#include "Collision.h"
#include <algorithm>

namespace Physics
{
void Collision::computeBounds()
{
	if(this->mesh.vertices.empty())
	{
		this->boundsMin = glm::vec3(0.0f);
		this->boundsMax = glm::vec3(0.0f);
		return;
	}
	this->boundsMin = this->mesh.vertices[0];
	this->boundsMax = this->mesh.vertices[0];
	for(const glm::vec3& rVertex : this->mesh.vertices)
	{
		this->boundsMin = glm::min(this->boundsMin, rVertex);
		this->boundsMax = glm::max(this->boundsMax, rVertex);
	}
	this->boundsMin += this->position;
	this->boundsMax += this->position;
}

void Collision::setRotation(const glm::mat3& rRotation_)
{
	if(this->baseVertices.empty()) this->baseVertices = this->mesh.vertices;
	for(size_t i = 0; i < this->baseVertices.size(); i++) this->mesh.vertices[i] = rRotation_ * this->baseVertices[i];
	if(rRotation_ == glm::mat3(1.0f))
	{
		this->baseVertices.clear();
		this->baseVertices.shrink_to_fit();
	}
	this->computeBounds();
}

void Collision::setPosition(const glm::vec3& rPosition_)
{
	glm::vec3 delta = rPosition_ - this->position;
	this->boundsMin += delta;
	this->boundsMax += delta;
	this->position = rPosition_;
}
}
