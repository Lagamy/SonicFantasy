#include "SALevelInstance.h"
#include "LevelStreaming.h"
#include "GPU_Globals.h"

namespace Graphics
{
/*
Streams the pieces by the camera (levelPieceAction): a piece that is Load or Draw has its GPU buffers uploaded (SAMesh::use,
a no-op if they are there), and a Draw one is drawn; an Unload one gives its GPU buffers back (SAMesh::destroy; the CPU
data stays, so use uploads it again). The sphere is a piece's bounding sphere moved by rBase_.
*/
void SALevelInstance::draw(const glm::mat4& rBase_, const Camera& rCamera_)
{
	std::array<glm::vec4, 6> planes = rCamera_.frustumPlanes();
	for(const SALevelPiece& rPiece : this->pieces)
	{
		glm::vec3 center = glm::vec3(rBase_ * glm::vec4(rPiece.center, 1.0f));
		SAInstance& rInstance = saInstances[rPiece.saInstanceId];

		switch(levelPieceAction(rCamera_, planes, center, rPiece.radius))
		{
		case LevelPieceAction::Unload:
			saMeshes[rInstance.saMeshId].destroy();
			break;
		case LevelPieceAction::Skip:
			break;
		case LevelPieceAction::Load:
			saMeshes[rInstance.saMeshId].use();
			break;
		case LevelPieceAction::Draw:
			saMeshes[rInstance.saMeshId].use();
			rInstance.draw(rBase_);
			break;
		}
	}
}

void SALevelInstance::destroy()
{
	for(const SALevelPiece& rPiece : this->pieces)
	{
		saInstances.remove(rPiece.saInstanceId);
	}
	this->pieces.clear();
}
}
