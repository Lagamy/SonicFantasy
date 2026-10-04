#include "DebugUI.h"
#include "Globals.h"
#include "GPU_Globals.h"
#include <glad/glad.h>
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <vector>
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

namespace Graphics
{
void DebugUI::init(GLFWwindow* pWindow_)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	/* No imgui.ini next to the executable: window placement is not kept between runs. */
	ImGui::GetIO().IniFilename = nullptr;
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(pWindow_, true);
	ImGui_ImplOpenGL3_Init("#version 450 core");
}

/*
Starts a frame, builds the panels and draws them over the scene. The window's framebuffer is sRGB (GL_FRAMEBUFFER_SRGB),
which would encode ImGui's colors, already in display space, a second time and wash them out, so it is off while drawing.
*/
void DebugUI::draw(const Camera& rCamera_)
{
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	/*
	One window with a tab per panel. The cursor is captured by the camera until the free-cursor key is pressed (see main.cpp), so the controls can only be used then.
	*/
	ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(460.0f, 420.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_NoSavedSettings);
	if(ImGui::BeginTabBar("DebugTabs"))
	{
		if(ImGui::BeginTabItem("Camera"))
		{
			ImGui::Text("Position: %.1f, %.1f, %.1f", rCamera_.position.x, rCamera_.position.y, rCamera_.position.z);
			ImGui::Text("Tab: free the cursor for these panels, click the scene to look around again.");
			ImGui::EndTabItem();
		}

		/* Physics state of every entity that has a physics: where it is, where the physics wanted it, and its velocity. */
		if(ImGui::BeginTabItem("Physics"))
		{
			ImGui::Text("Gravity: %.1f, %.1f, %.1f", Globals::Physics::gravity.x, Globals::Physics::gravity.y, Globals::Physics::gravity.z);
			for(uint32_t i = 0; i < Globals::Game::entities.objects.size(); i++)
			{
				if(!Globals::Game::entities.alive[i]) continue;
				const Entity& rEntity = Globals::Game::entities.objects[i];
				if(rEntity.physicsId == UninitializedPoolId) continue;
				const Physics::PhysicsBody& rBody = Globals::Physics::bodies[rEntity.physicsId];
				ImGui::Text("%s", Globals::Game::entities.names[i].c_str());
				ImGui::Text("  position: %.2f, %.2f, %.2f", rEntity.position.x, rEntity.position.y, rEntity.position.z);
				ImGui::Text("  staging:  %.2f, %.2f, %.2f", rEntity.stagingPosition.x, rEntity.stagingPosition.y, rEntity.stagingPosition.z);
				ImGui::Text("  velocity: %.2f, %.2f, %.2f", rBody.velocity.x, rBody.velocity.y, rBody.velocity.z);
			}
			ImGui::EndTabItem();
		}

		/*
		The collision lines view: its switches, and the legend of the raw attribute values of the shown level collisions (Shadow attributes / SADX piece flags), most triangles
		first, in the color their lines have; clicking a value highlights it (the arrow keys do the same), clicking it again shows all.
		*/
		if(ImGui::BeginTabItem("Collision"))
		{
			ImGui::Checkbox("Show collision lines (C)", &collisionView.enabled);
			ImGui::Checkbox("Color by attribute, otherwise by slope (V)", &collisionView.colorByAttribute);
			ImGui::SliderFloat("Draw distance", &collisionView.drawDistance, 100.0f, 20000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
			if(collisionView.enabled && collisionView.colorByAttribute && !collisionView.attributes.empty())
			{
				ImGui::Separator();
				ImGui::Text("%u values", static_cast<uint32_t>(collisionView.attributes.size()));
				ImGui::BeginChild("CollisionValues", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
				for(int i = 0; i < static_cast<int>(collisionView.attributes.size()); i++)
				{
					const CollisionView::AttributeCount& rEntry = collisionView.attributes[i];
					char line[64];
					std::snprintf(line, sizeof(line), "0x%08X  %u triangles", rEntry.attribute, rEntry.count);
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(rEntry.color.r, rEntry.color.g, rEntry.color.b, 1.0f));
					if(ImGui::Selectable(line, i == collisionView.selectedAttribute)) collisionView.selectedAttribute = i == collisionView.selectedAttribute ? -1 : i;
					ImGui::PopStyleColor();
				}
				ImGui::EndChild();
			}
			ImGui::EndTabItem();
		}

		/* The markers (crosses of placed objects without a model): one line per value, with its label, how many there are and the color of its crosses. */
		if(ImGui::BeginTabItem("Markers"))
		{
			struct MarkerCount
			{
				uint32_t value;
				const char* label;
				uint32_t count;
			};
			std::vector<MarkerCount> counts;
			for(const CollisionView::Marker& rMarker : collisionView.markers)
			{
				auto found = std::find_if(counts.begin(), counts.end(), [&](const MarkerCount& rCount_) { return rCount_.value == rMarker.value; });
				if(found == counts.end()) counts.push_back(MarkerCount{rMarker.value, rMarker.label, 1});
				else found->count++;
			}
			std::stable_sort(counts.begin(), counts.end(), [](const MarkerCount& rA_, const MarkerCount& rB_) { return rA_.count > rB_.count; });
			ImGui::Text("Objects without a model: %u kinds", static_cast<uint32_t>(counts.size()));
			for(const MarkerCount& rCount : counts)
			{
				glm::vec4 rgba = CollisionView::colorOf(rCount.value);
				ImGui::TextColored(ImVec4(rgba.r, rgba.g, rgba.b, 1.0f), "%u %s  x%u", rCount.value, rCount.label != nullptr ? rCount.label : "", rCount.count);
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();

	ImGui::Render();
	glDisable(GL_FRAMEBUFFER_SRGB);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	glEnable(GL_FRAMEBUFFER_SRGB);
}

bool DebugUI::wantsMouse() const
{
	return ImGui::GetIO().WantCaptureMouse;
}

void DebugUI::destroy()
{
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}
}
