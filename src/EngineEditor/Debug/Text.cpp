#include "Text.h"
#include "Globals.h"
#include "GPU_Globals.h"
#include "Editor.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>
#include <string>

namespace Graphics
{
void Text::selectMode(int step_)
{
	this->mode = static_cast<Mode>((static_cast<int>(this->mode) + step_ + ModeCount) % ModeCount);
}

/* The name an entity has in the shown mode (a reference into the pools' names, valid for the frame); empty if it has none (no instance, no collision). */
static const std::string& nameOf(const Entity& rEntity_, uint32_t index_, Text::Mode mode_)
{
	static const std::string None;
	if(mode_ == Text::EntityName) return Globals::Game::entities.names[index_];
	if(mode_ == Text::CollisionName)
	{
		if(rEntity_.collisionId == UninitializedPoolId) return None;
		return Globals::Physics::collisions[rEntity_.collisionId].name;
	}
	if(rEntity_.instanceId == UninitializedPoolId) return rEntity_.isExternalOnly() && rEntity_.pOverrideModel != nullptr ? rEntity_.pOverrideModel->name : None;
	if(rEntity_.sourceEngine == SA_NINJA) return saMeshes.names[saInstances[rEntity_.instanceId].saMeshId.id];
	const ONEInstance& rInstance = oneInstances[rEntity_.instanceId];
	return oneArchives[rInstance.archiveId].meshNames[rInstance.meshIndex];
}

void Text::draw(const Camera& rCamera_)
{
	/* The point the label hangs from: this far above the entity's position (a guess). */
	constexpr float LabelHeight = 10.0f;
	ImDrawList* pList = ImGui::GetBackgroundDrawList();
	ImVec2 size = ImGui::GetIO().DisplaySize;

	/*
	One color per mode: entity names yellow, mesh names green, collision names cyan. The mode's word ("Entity", "Mesh", "Collision") is written centered at the top of the screen and as the
	first line of every label, in the same color. The size is TextScale times ImGui's font size at ReferenceDistance from the camera and shrinks or grows in proportion to the distance (a plane of fixed size in the world), kept between MinFontSize and MaxFontSize pixels.
	*/
	constexpr float TextScale = 1.3f;
	constexpr float ReferenceDistance = 100.0f;
	constexpr float MinFontSize = 6.0f;
	constexpr float MaxFontSize = 64.0f;
	constexpr ImU32 Colors[ModeCount] = {IM_COL32(255, 220, 60, 255), IM_COL32(110, 255, 110, 255), IM_COL32(80, 220, 255, 255)};
	constexpr const char* Titles[ModeCount] = {"Entity", "Mesh", "Collision"};
	ImU32 color = Colors[this->mode];
	ImFont* pFont = ImGui::GetFont();
	float baseFontSize = ImGui::GetFontSize() * TextScale;
	auto lineSize = [&](float fontSize_, const char* pText_) { return pFont->CalcTextSizeA(fontSize_, FLT_MAX, 0.0f, pText_); };
	auto drawLine = [&](float fontSize_, ImVec2 at_, const char* pText_)
	{
		at_ = ImVec2(std::round(at_.x), std::round(at_.y));
		pList->AddText(pFont, fontSize_, ImVec2(at_.x + 1.0f, at_.y + 1.0f), IM_COL32(0, 0, 0, 255), pText_);
		pList->AddText(pFont, fontSize_, at_, color, pText_);
	};
	/*
	Text at whole pixel positions and whole pixel font sizes, otherwise the glyphs are sampled between texels and blur. The labels are drawn farthest first (by the clip w, the depth along the view),
	so a closer label covers a farther one; the scene's depth buffer is not read, so a label is not hidden by geometry in front of it.
	*/
	struct Label
	{
		float depth;
		float centerX;
		float bottomY;
		const std::string* pText;
	};
	std::vector<Label> labels;
	float titleFontSize = std::round(baseFontSize);
	drawLine(titleFontSize, ImVec2((size.x - lineSize(titleFontSize, Titles[this->mode]).x) * 0.5f, 6.0f), Titles[this->mode]);
	glm::mat4 viewProjection = rCamera_.projection * rCamera_.view;
	std::array<glm::vec4, 6> planes = rCamera_.frustumPlanes();
	/* A label farther than the distance at which its size would get below MinFontSize is not drawn at all (it could not be read, and drawing hundreds of them every frame is what made the debug mode slow). */
	float distance = std::min({editor.collisionView.drawDistance, rCamera_.farPlane, baseFontSize * ReferenceDistance / MinFontSize});
	for(uint32_t i = 0; i < Globals::Game::entities.objects.size(); i++)
	{
		if(!Globals::Game::entities.alive[i]) continue;
		const Entity& rEntity = Globals::Game::entities.objects[i];
		glm::vec3 position = rEntity.renderPosition();
		if(glm::distance(position, rCamera_.position) > distance) continue;
		glm::vec3 anchor = position + glm::vec3(0.0f, LabelHeight, 0.0f);
		if(!sphereInFrustum(planes, anchor, 0.0f)) continue;
		const std::string& rLabel = nameOf(rEntity, i, this->mode);
		if(rLabel.empty()) continue;

		glm::vec4 clip = viewProjection * glm::vec4(anchor, 1.0f);
		if(clip.w <= 0.0f) continue;
		glm::vec2 ndc = glm::vec2(clip) / clip.w;
		labels.push_back(Label{clip.w, (ndc.x * 0.5f + 0.5f) * size.x, (1.0f - (ndc.y * 0.5f + 0.5f)) * size.y, &rLabel});
	}
	std::sort(labels.begin(), labels.end(), [](const Label& rA_, const Label& rB_) { return rA_.depth > rB_.depth; });
	for(const Label& rLabel : labels)
	{
		float fontSize = std::round(std::clamp(baseFontSize * ReferenceDistance / rLabel.depth, MinFontSize, MaxFontSize));
		ImVec2 nameSize = lineSize(fontSize, rLabel.pText->c_str());
		ImVec2 titleSize = lineSize(fontSize, Titles[this->mode]);
		drawLine(fontSize, ImVec2(rLabel.centerX - nameSize.x * 0.5f, rLabel.bottomY - nameSize.y), rLabel.pText->c_str());
		drawLine(fontSize, ImVec2(rLabel.centerX - titleSize.x * 0.5f, rLabel.bottomY - nameSize.y - titleSize.y), Titles[this->mode]);
	}
}
}
