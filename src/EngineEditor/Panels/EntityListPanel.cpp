#include "EntityListPanel.h"
#include "Globals.h"
#include "API.h"
#include "Editor.h"
#include "Prefab.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <functional>
#include <string>
#include <vector>

namespace Graphics
{
namespace
{
/* A folder of the tree built each frame: node 0 is the root (not shown). userFolder is the index into EntityListPanel::userFolders for a user folder, -1 for a built-in one. */
struct Node
{
	std::string label;
	std::string key;
	int parent = -1;
	int userFolder = -1;
	std::vector<int> children;
	std::vector<uint32_t> entities;
	uint32_t total = 0;
};

/* One line of the list: a folder (node >= 0) or an entity (node -1, entity the slot in Globals::Game::entities). */
struct Row
{
	int node;
	uint32_t entity;
	int depth;
};

uint64_t pack(PoolId id_)
{
	return (static_cast<uint64_t>(id_.generation) << 32) | id_.id;
}

constexpr float IndentPerLevel = 14.0f;
}

void EntityListPanel::draw()
{
	/* The height is fixed by the constraints, so only the right edge can be dragged; the width it ends with is read back and is what the next frame (and the other panels) use. */
	ImVec2 display = ImGui::GetIO().DisplaySize;
	float top = ImGui::GetFrameHeight();
	ImGui::SetNextWindowPos(ImVec2(0.0f, top));
	ImGui::SetNextWindowSizeConstraints(ImVec2(150.0f, display.y - top), ImVec2(display.x * 0.4f, display.y - top));
	ImGui::SetNextWindowSize(ImVec2(this->width, display.y - top));
	ImGui::Begin("Level", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
	this->width = ImGui::GetWindowSize().x;

	if(ImGui::BeginTabBar("LeftTabs"))
	{
		if(ImGui::BeginTabItem("Entities"))
		{
			this->drawEntities();
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("Collision"))
		{
			editor.collisionView.drawPanel();
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("Editor camera"))
		{
			this->drawCameraSettings();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

void EntityListPanel::drawCameraSettings()
{
	EditorCamera& rCamera = editor.camera;
	ImGui::SliderFloat("Look sensitivity (degrees/pixel)", &rCamera.lookSensitivity, 0.005f, 0.5f, "%.3f", ImGuiSliderFlags_Logarithmic);
	ImGui::SliderFloat("Pan (per pixel, x speed)", &rCamera.panPerSpeed, 0.0005f, 0.05f, "%.4f", ImGuiSliderFlags_Logarithmic);
	ImGui::SliderFloat("Zoom (per notch, x speed)", &rCamera.zoomPerSpeed, 0.01f, 2.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
	ImGui::SliderFloat("Full-speed distance (to nearest entity)", &rCamera.referenceDistance, 0.1f, 10000.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
	ImGui::SliderFloat("Fly speed (units/s)", &rCamera.speed, rCamera.minSpeed, rCamera.maxSpeed, "%.0f", ImGuiSliderFlags_Logarithmic);
	ImGui::SliderFloat("Boost (Shift multiplier)", &rCamera.speedUp, 1.0f, 100.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
	if(ImGui::SliderFloat("Field of view", &rCamera.fov, 10.0f, 150.0f, "%.0f")) rCamera.refreshProjection();
	if(ImGui::SliderFloat("Near plane", &rCamera.nearPlane, 0.01f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) rCamera.refreshProjection();
	if(ImGui::SliderFloat("Render distance (far plane)", &rCamera.farPlane, 100.0f, 500000.0f, "%.0f", ImGuiSliderFlags_Logarithmic)) rCamera.refreshProjection();
}

void EntityListPanel::drawEntities()
{
	if(ImGui::Button("Add camera"))
	{
		glm::vec3 position = editor.camera.position;
		editor.recordCreated(editor.addLevelCamera("Camera", position));
	}
	ImGui::SameLine();
	if(ImGui::Button("New folder"))
	{
		/* Inside the selected user folder if there is one, and open, so the new folder is seen. */
		this->userFolders.push_back(UserFolder{this->newFolderName, this->selectedFolder, true});
		if(this->selectedFolder >= 0) this->openFolders.insert(std::format("u{}", this->selectedFolder));
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputText("##NewFolderName", this->newFolderName, sizeof(this->newFolderName));
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##EntityFilter", "filter by name", this->filter, sizeof(this->filter));

	/* A selection whose entity was removed is dropped, and so are the folder assignments of removed entities. */
	if(this->selected != UninitializedPoolId && !entityExists(this->selected)) this->selected = UninitializedPoolId;
	for(auto it = this->entityFolder.begin(); it != this->entityFolder.end();)
	{
		PoolId id(static_cast<uint32_t>(it->first & 0xFFFFFFFFu), static_cast<uint32_t>(it->first >> 32));
		if(entityExists(id)) ++it;
		else it = this->entityFolder.erase(it);
	}

	/*
	The tree: the user's folders (in the order they were made, parents first), then the built-in ones, which are made when an entity needs them: game, group, prefab. Each live entity whose name holds the filter text (case does not matter)
	goes into its user folder, or into the folder of its prefab ("Ungrouped" without one).
	*/
	std::string needle = this->filter;
	std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c_) { return static_cast<char>(std::tolower(c_)); });
	bool filtering = !needle.empty();

	std::vector<Node> nodes(1);
	auto addChild = [&](int parent_, std::string label_, std::string key_, int userFolder_)
	{
		Node node;
		node.label = std::move(label_);
		node.key = std::move(key_);
		node.parent = parent_;
		node.userFolder = userFolder_;
		nodes.push_back(std::move(node));
		int index = static_cast<int>(nodes.size()) - 1;
		nodes[parent_].children.push_back(index);
		return index;
	};

	std::vector<int> userNode(this->userFolders.size(), -1);
	for(int i = 0; i < static_cast<int>(this->userFolders.size()); i++)
	{
		if(!this->userFolders[i].alive) continue;
		int parentNode = this->userFolders[i].parent < 0 ? 0 : userNode[this->userFolders[i].parent];
		userNode[i] = addChild(parentNode, this->userFolders[i].name, std::format("u{}", i), i);
	}

	std::array<int, 2> gameNode{-1, -1};
	std::array<std::array<int, 5>, 2> groupNode;
	for(auto& rRow : groupNode) rRow.fill(-1);
	std::vector<int> prefabNode(Globals::Game::prefabs.objects.size(), -1);
	int ungroupedNode = -1;
	auto nodeOfPrefab = [&](PoolId prefabId_)
	{
		if(prefabId_ == UninitializedPoolId || prefabId_.id >= prefabNode.size() || !Globals::Game::prefabs.alive[prefabId_.id])
		{
			if(ungroupedNode < 0) ungroupedNode = addChild(0, "Ungrouped", "a/Ungrouped", -1);
			return ungroupedNode;
		}
		int& rPrefabNode = prefabNode[prefabId_.id];
		if(rPrefabNode >= 0) return rPrefabNode;
		const Prefab& rPrefab = Globals::Game::prefabs.objects[prefabId_.id];
		int game = rPrefab.game == SA_NINJA ? 0 : 1;
		int group = static_cast<int>(rPrefab.group);
		if(gameNode[game] < 0) gameNode[game] = addChild(0, prefabGameFolder(rPrefab.game), std::format("a/{}", prefabGameName(rPrefab.game)), -1);
		if(groupNode[game][group] < 0) groupNode[game][group] = addChild(gameNode[game], prefabGroupFolder(rPrefab.group), std::format("a/{}/{}", prefabGameName(rPrefab.game), prefabGroupFolder(rPrefab.group)), -1);
		rPrefabNode = addChild(groupNode[game][group], rPrefab.name, std::format("a/{}", Globals::Game::prefabs.names[prefabId_.id]), -1);
		return rPrefabNode;
	};

	uint32_t liveCount = 0;
	uint32_t shownCount = 0;
	for(uint32_t i = 0; i < Globals::Game::entities.objects.size(); i++)
	{
		if(!Globals::Game::entities.alive[i]) continue;
		liveCount++;
		if(filtering)
		{
			std::string name = Globals::Game::entities.names[i];
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c_) { return static_cast<char>(std::tolower(c_)); });
			if(name.find(needle) == std::string::npos) continue;
		}
		shownCount++;
		int node = -1;
		if(!this->entityFolder.empty())
		{
			auto assigned = this->entityFolder.find(pack(PoolId(i, Globals::Game::entities.generation[i])));
			if(assigned != this->entityFolder.end()) node = userNode[assigned->second];
		}
		if(node < 0) node = nodeOfPrefab(Globals::Game::entities.objects[i].prefabId);
		nodes[node].entities.push_back(i);
	}
	ImGui::Text("%u of %u", shownCount, liveCount);

	/* Totals (an entity count per folder, with its subfolders: nodes come after their parents, so back to front adds each into its parent), and the folders in order: the user's first, then the built-in by name. */
	for(Node& rNode : nodes) rNode.total = static_cast<uint32_t>(rNode.entities.size());
	for(int i = static_cast<int>(nodes.size()) - 1; i > 0; i--) nodes[nodes[i].parent].total += nodes[i].total;
	for(Node& rNode : nodes)
	{
		std::stable_sort(rNode.children.begin(), rNode.children.end(), [&](int a_, int b_)
		{
			bool aUser = nodes[a_].userFolder >= 0;
			bool bUser = nodes[b_].userFolder >= 0;
			if(aUser != bUser) return aUser;
			if(aUser) return nodes[a_].userFolder < nodes[b_].userFolder;
			return nodes[a_].label < nodes[b_].label;
		});
	}

	/* The lines to show: depth first, the folders of a folder and then its entities, those of a closed folder left out. */
	std::vector<Row> rows;
	std::function<void(int, int)> flatten = [&](int index_, int depth_)
	{
		const Node& rNode = nodes[index_];
		if(filtering && rNode.total == 0) return;
		rows.push_back(Row{index_, 0, depth_});
		if(!filtering && !this->openFolders.contains(rNode.key)) return;
		for(int child : rNode.children) flatten(child, depth_ + 1);
		for(uint32_t entity : rNode.entities) rows.push_back(Row{-1, entity, depth_ + 1});
	};
	for(int child : nodes[0].children) flatten(child, 0);

	ImGui::BeginChild("EntityRows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
	ImGuiListClipper clipper;
	clipper.Begin(static_cast<int>(rows.size()));
	while(clipper.Step())
	{
		for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
		{
			const Row& rRow = rows[row];
			ImGui::Indent(rRow.depth * IndentPerLevel);
			if(rRow.node >= 0)
			{
				const Node& rNode = nodes[rRow.node];
				bool open = filtering || this->openFolders.contains(rNode.key);
				std::string label = std::format("{} {} ({})", open ? "[-]" : "[+]", rNode.label, rNode.total);
				ImGui::PushID(rNode.key.c_str());
				if(ImGui::Selectable(label.c_str(), rNode.userFolder >= 0 && rNode.userFolder == this->selectedFolder))
				{
					if(this->openFolders.contains(rNode.key)) this->openFolders.erase(rNode.key);
					else this->openFolders.insert(rNode.key);
					this->selectedFolder = rNode.userFolder;
				}
				/* An entity dropped on a user folder goes into it, on a built-in one back to its own. */
				if(ImGui::BeginDragDropTarget())
				{
					if(const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload("ENTITY"))
					{
						uint64_t packed = *static_cast<const uint64_t*>(pPayload->Data);
						if(rNode.userFolder >= 0) this->entityFolder[packed] = rNode.userFolder;
						else this->entityFolder.erase(packed);
					}
					ImGui::EndDragDropTarget();
				}
				if(rNode.userFolder >= 0 && ImGui::BeginPopupContextItem("FolderMenu"))
				{
					if(ImGui::MenuItem("Delete folder"))
					{
						int deleted = rNode.userFolder;
						this->userFolders[deleted].alive = false;
						for(UserFolder& rFolder : this->userFolders)
						{
							if(rFolder.parent == deleted) rFolder.parent = this->userFolders[deleted].parent;
						}
						for(auto it = this->entityFolder.begin(); it != this->entityFolder.end();)
						{
							if(it->second == deleted) it = this->entityFolder.erase(it);
							else ++it;
						}
						if(this->selectedFolder == deleted) this->selectedFolder = -1;
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}
			else
			{
				uint32_t index = rRow.entity;
				PoolId id(index, Globals::Game::entities.generation[index]);
				ImGui::PushID(static_cast<int>(index));
				if(ImGui::Selectable(Globals::Game::entities.names[index].c_str(), id == this->selected))
				{
					this->selected = id;
					this->selectedFolder = -1;
				}
				if(ImGui::BeginDragDropSource())
				{
					uint64_t packed = pack(id);
					ImGui::SetDragDropPayload("ENTITY", &packed, sizeof(packed));
					ImGui::TextUnformatted(Globals::Game::entities.names[index].c_str());
					ImGui::EndDragDropSource();
				}
				if(ImGui::BeginPopupContextItem("EntityMenu"))
				{
					if(ImGui::BeginMenu("Move to folder"))
					{
						for(int folder = 0; folder < static_cast<int>(this->userFolders.size()); folder++)
						{
							if(this->userFolders[folder].alive && ImGui::MenuItem(this->userFolders[folder].name.c_str())) this->entityFolder[pack(id)] = folder;
						}
						ImGui::EndMenu();
					}
					if(this->entityFolder.contains(pack(id)) && ImGui::MenuItem("Remove from folder")) this->entityFolder.erase(pack(id));
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}
			ImGui::Unindent(rRow.depth * IndentPerLevel);
		}
	}
	ImGui::EndChild();
}
}
