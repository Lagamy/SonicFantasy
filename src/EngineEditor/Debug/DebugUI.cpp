#include "DebugUI.h"
#include "Globals.h"
#include "GPU_Globals.h"
#include "Editor.h"
#include "GLTFImport.h"
#include <glad/glad.h>
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
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
void DebugUI::draw(Editor& rEditor_)
{
	CollisionView& collisionView = rEditor_.collisionView;
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	collisionView.entityTabActive = false;
	collisionView.boundAreaTabActive = false;

	/*
	The entity list (left), the inspector (right) and the prefab panel (bottom, between them) are windows of their own along the edges of the window. The rest is one window with a tab per panel. The cursor is captured by the camera until the free-cursor key is pressed (Editor::processInputs), so the controls can only be used then.
	*/
	if(rEditor_.active)
	{
		/*
		The File menu: Save writes the current level's edits into its file, Save as into a file of another name (the level's own file is not changed), Save Source Game Level makes the level as it is now its source (written at once, the level is loaded again), Restore Source Game Level puts the level back as that source has it (not
		written until Save). The level's name is the file name Save writes to.
		*/
		bool openSaveAs = false;
		bool openLoad = false;
		bool openNew = false;
		bool openSwapModel = false;
		bool openAddCollision = false;
		bool openSwapCollision = false;
		bool openImportTexture = false;
		bool openImportModel = false;
		if(ImGui::BeginMainMenuBar())
		{
			if(ImGui::BeginMenu("File"))
			{
				PoolId levelId = rEditor_.currentLevel();
				std::string levelName = levelId != UninitializedPoolId ? Globals::Game::levels.names[levelId.id] : std::string();
				if(ImGui::MenuItem("Save", nullptr, false, levelId != UninitializedPoolId)) rEditor_.saveLevel(levelName);
				if(ImGui::MenuItem("Save as...", nullptr, false, levelId != UninitializedPoolId))
				{
					std::snprintf(this->saveAsName, sizeof(this->saveAsName), "%s", levelName.c_str());
					openSaveAs = true;
				}
				if(ImGui::MenuItem("Load level..."))
				{
					this->levelNames.clear();
					std::error_code error;
					for(const std::filesystem::directory_entry& rFile : std::filesystem::directory_iterator("Assets/Levels", error))
					{
						if(rFile.is_directory() && std::filesystem::exists(rFile.path() / (rFile.path().filename().string() + ".json"))) this->levelNames.push_back(rFile.path().filename().string());
					}
					std::sort(this->levelNames.begin(), this->levelNames.end());
					openLoad = true;
				}
				if(ImGui::MenuItem("New level...")) openNew = true;

				/* glTF levels only: Swap level model takes a .gltf/.glb path; Swap collision's side menu has Add collision file (a path) and the level's collision files (the ones in use ticked); picking a file asks for the glTF that replaces it. */
				bool gltfLevel = levelId != UninitializedPoolId && Loading::isGLTFLevel(levelId);
				if(ImGui::MenuItem("Swap level model...", nullptr, false, gltfLevel)) openSwapModel = true;
				if(ImGui::BeginMenu("Swap collision", gltfLevel))
				{
					if(ImGui::MenuItem("Add collision file...")) openAddCollision = true;
					ImGui::Separator();
					std::vector<std::string> active = Loading::activeGLTFCollisions(levelId);
					for(const std::string& rCollision : Loading::listGLTFLevelCollisions(levelName))
					{
						bool inUse = std::find(active.begin(), active.end(), rCollision) != active.end();
						if(ImGui::MenuItem(rCollision.c_str(), nullptr, inUse))
						{
							this->swapCollisionName = rCollision;
							openSwapCollision = true;
						}
					}
					ImGui::EndMenu();
				}
				if(ImGui::MenuItem("Save Source Game Level", nullptr, false, levelId != UninitializedPoolId)) rEditor_.saveSourceLevel();
				if(ImGui::MenuItem("Restore Source Game Level", nullptr, false, levelId != UninitializedPoolId)) rEditor_.restoreSourceValues();
				ImGui::EndMenu();
			}
			if(ImGui::BeginMenu("Import"))
			{
				if(ImGui::MenuItem("Texture (.png/.jpg)...")) openImportTexture = true;
				if(ImGui::MenuItem("Model (.gltf/.glb)...")) openImportModel = true;
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}
		if(openSaveAs) ImGui::OpenPopup("Save level as");
		if(openLoad) ImGui::OpenPopup("Load level");
		if(openNew) ImGui::OpenPopup("New level");
		if(openSwapModel) ImGui::OpenPopup("Swap level model");
		if(openAddCollision) ImGui::OpenPopup("Add collision file");
		if(openSwapCollision) ImGui::OpenPopup("Swap collision file");
		if(openImportTexture) ImGui::OpenPopup("Import texture");
		if(openImportModel) ImGui::OpenPopup("Import model");
		if(ImGui::BeginPopupModal("Import texture", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Copies the image into Assets/External as an external texture (the name must be new).");
			ImGui::InputText("Image (.png/.jpg)", this->importTexturePath, sizeof(this->importTexturePath));
			ImGui::InputText("Name (empty = file name)", this->importTextureName, sizeof(this->importTextureName));
			ImGui::Checkbox("Into the open level (else the project)", &this->importTextureInLevel);
			if(ImGui::Button("Import"))
			{
				rEditor_.importTexture(this->importTexturePath, this->importTextureName, this->importTextureInLevel);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if(ImGui::BeginPopupModal("Import model", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Converts the glTF into an external model in Assets/External, with its textures (a model of that name is replaced).");
			ImGui::InputText("Model (.gltf/.glb)", this->importModelPath, sizeof(this->importModelPath));
			ImGui::InputText("Name (empty = file name)", this->importModelName, sizeof(this->importModelName));
			ImGui::Checkbox("Into the open level (else the project)", &this->importModelInLevel);
			if(ImGui::Button("Import"))
			{
				rEditor_.importModel(this->importModelPath, this->importModelName, this->importModelInLevel);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if(ImGui::BeginPopupModal("Swap collision file", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Replaces the collision '%s' with the triangles of the glTF.", this->swapCollisionName.c_str());
			ImGui::InputText("New file (.gltf/.glb)", this->swapCollisionPath, sizeof(this->swapCollisionPath));
			if(ImGui::Button("Swap"))
			{
				rEditor_.swapLevelCollision(this->swapCollisionName, this->swapCollisionPath);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if(ImGui::BeginPopupModal("Swap level model", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Replaces the level's visual with the glTF (cut into chunks in the level's folder).");
			ImGui::InputText("Model (.gltf/.glb)", this->swapModelPath, sizeof(this->swapModelPath));
			if(ImGui::Button("Swap"))
			{
				rEditor_.swapLevelModel(this->swapModelPath);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if(ImGui::BeginPopupModal("Add collision file", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Adds the glTF's triangles to the level's collision (kept as a collision file of the level).");
			ImGui::InputText("Collision (.gltf/.glb)", this->addCollisionPath, sizeof(this->addCollisionPath));
			if(ImGui::Button("Add"))
			{
				rEditor_.addLevelCollision(this->addCollisionPath);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if(ImGui::BeginPopupModal("New level", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Writes Levels/<name>/<name>.json (disc paths as in ResourceKeys.h) and loads it; lists are comma separated.");
			ImGui::InputText("Name", this->newName, sizeof(this->newName));
			if(ImGui::Combo("Engine", &this->newEngine, "sadx\0sth2005\0gltf\0"))
			{
				std::snprintf(this->newShader, sizeof(this->newShader), "%s", this->newEngine == 1 ? "STH2005" : this->newEngine == 2 ? "GLTF" : "SA");
			}
			ImGui::InputText("Shader", this->newShader, sizeof(this->newShader));
			if(this->newEngine == 0)
			{
				ImGui::InputText("Land table module (.rel)", this->newLandTableRel, sizeof(this->newLandTableRel));
				ImGui::InputText("Land table offset", this->newLandTableOffset, sizeof(this->newLandTableOffset));
				ImGui::InputText("Textures (.gvm)", this->newTextures, sizeof(this->newTextures));
			}
			else if(this->newEngine == 2)
			{
				ImGui::InputText("Model (.gltf/.glb)", this->newModel, sizeof(this->newModel));
				ImGui::InputText("Collision files (.gltf/.glb)", this->newCollisions, sizeof(this->newCollisions));
			}
			else
			{
				ImGui::InputText("Sections (.one)", this->newSections, sizeof(this->newSections));
				ImGui::InputText("Texture archive (.one)", this->newTextures, sizeof(this->newTextures));
				ImGui::InputText("Texture .TXD entry", this->newTxd, sizeof(this->newTxd));
			}
			if(this->newEngine != 2)
			{
				ImGui::InputText("Placement files", this->newPlacementFiles, sizeof(this->newPlacementFiles));
				ImGui::InputText("Entity table", this->newEntityTable, sizeof(this->newEntityTable));
				ImGui::InputText("Type names", this->newTypeNames, sizeof(this->newTypeNames));
			}
			if(ImGui::Button("Create"))
			{
				Loading::NewLevelFile level;
				level.name = this->newName;
				level.engine = this->newEngine == 0 ? "sadx" : this->newEngine == 1 ? "sth2005" : "gltf";
				level.shader = this->newShader;
				level.landTableRel = this->newLandTableRel;
				level.landTableOffset = this->newLandTableOffset;
				level.textures = this->newTextures;
				level.txd = this->newTxd;
				level.sections = this->newSections;
				level.placementFiles = this->newPlacementFiles;
				level.entityTable = this->newEntityTable;
				level.typeNames = this->newTypeNames;
				level.model = this->newModel;
				level.collisions = this->newCollisions;
				rEditor_.newLevel(level);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if(ImGui::BeginPopupModal("Load level", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Replaces the level; unsaved edits are lost.");
			for(const std::string& rName : this->levelNames)
			{
				if(ImGui::Button(rName.c_str()))
				{
					rEditor_.loadLevel(rName);
					ImGui::CloseCurrentPopup();
				}
			}
			if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if(ImGui::BeginPopupModal("Save level as", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::InputText("File name", this->saveAsName, sizeof(this->saveAsName));
			if(ImGui::Button("Save"))
			{
				rEditor_.saveLevel(this->saveAsName);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		/* The toolbar: the Play button, centered at the top, and the last File message. */
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetFrameHeight() + 4.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
		ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
		if(ImGui::Button("Play")) rEditor_.setPlaying(true);
		if(!rEditor_.fileMessage.empty()) ImGui::TextUnformatted(rEditor_.fileMessage.c_str());
		ImGui::End();

		rEditor_.entityList.draw();
		rEditor_.inspector.draw(rEditor_.entityList.selected);
		rEditor_.prefabPanel.draw(rEditor_.entityList.selected, rEditor_.camera);
	}

	/* The Debug window is for playing only (Tab); the editor has the same collision settings in the Level panel's Collision tab. */
	if(!rEditor_.active)
	{
		ImGui::SetNextWindowPos(ImVec2(rEditor_.entityList.width + 10.0f, 10.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(460.0f, 420.0f), ImGuiCond_FirstUseEver);
		ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_NoSavedSettings);
		if(!rEditor_.active && ImGui::Button("Stop (back to the editor)")) rEditor_.setPlaying(false);
		if(ImGui::BeginTabBar("DebugTabs"))
		{
			/* The game being played: the ring count (the player's movement is a placeholder script, Scripts/sadx/player.lua). */
			if(ImGui::BeginTabItem("Game"))
			{
				ImGui::Text("Rings: %d", Globals::Game::rings);
				ImGui::Text("Editor: W A S D, Space, Ctrl fly the editor camera. Play controls the player (Space jumps) with the gameplay camera, which follows him; Tab opens and closes this menu while playing (placeholder controls).");
				ImGui::Text("FPS: %.0f", Globals::App::fps);
				ImGui::Text("Entities: %u static (%u kept by the last query), %u dynamic, %u active", static_cast<uint32_t>(Globals::Game::spatialIndex.items.size()), Globals::Game::spatialIndex.lastVisibleStatic, static_cast<uint32_t>(Globals::Game::spatialIndex.dynamicEntities.size()), static_cast<uint32_t>(Globals::Game::spatialIndex.activeEntities.size()));
				if(ImGui::Checkbox("VSync", &Globals::App::vsync)) glfwSwapInterval(Globals::App::vsync ? 1 : 0);
				ImGui::Text("Frame: %.1f ms, gameplay ticks this frame: %d (%.2f ms)", Globals::App::deltaTime * 1000.0f, Globals::Game::gameplaySystem.ticksLastFrame, Globals::Game::gameplaySystem.tickMillisecondsLastFrame);
				ImGui::Checkbox("Log game events", &Globals::Game::gameEvents.logEvents);
				ImGui::Checkbox("Extrapolate entities (off: interpolate, a tick behind)", &Globals::Game::gameplaySystem.extrapolate);
				ImGui::EndTabItem();
			}

			if(ImGui::BeginTabItem("Collision"))
			{
				collisionView.drawPanel();
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
	}

	if(rEditor_.active) rEditor_.text.draw(rEditor_.camera);
	if(rEditor_.active) rEditor_.gizmo.draw(rEditor_.camera, rEditor_.entityList.selected);

	ImGui::Render();
	glDisable(GL_FRAMEBUFFER_SRGB);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	glEnable(GL_FRAMEBUFFER_SRGB);
}

bool DebugUI::wantsMouse() const
{
	return ImGui::GetIO().WantCaptureMouse;
}

bool DebugUI::wantsKeyboard() const
{
	return ImGui::GetIO().WantCaptureKeyboard;
}

void DebugUI::destroy()
{
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}
}
