#pragma once
#include "Prefab.h"
#include <string>
#include <vector>

namespace Graphics
{
/*
The first two levels of the file browsers of the Assets and Prefabs tabs (AssetPanel, PrefabPanel), which are the same in both: the scope (openScope -1 = "Global" / "Level" not chosen yet, 0 = Global, 1 = Level), then the game (openGame -1 = the list of the two games and "External" not chosen yet, 0 = SADX, 1 = Shadow, Global + openScope = External of the open scope). The folders below a game are each panel's own.
*/
struct ScopeNav
{
	/* The game index of External under Global; External under Level is Global + 1. */
	static constexpr int Global = 2;

	int openScope = -1;
	int openGame = -1;

	/* The labels of the folder tiles of the first two levels, and the click on one (a click of -1 changes nothing). */
	static std::vector<std::string> scopeLabels()
	{
		return {"Global", "Level"};
	}
	static std::vector<std::string> gameLabels()
	{
		return {prefabGameFolder(SA_NINJA), prefabGameFolder(STH2005_ONE), "External"};
	}
	void pickScope(int clicked_)
	{
		if(clicked_ >= 0) this->openScope = clicked_;
	}
	void pickGame(int clicked_)
	{
		if(clicked_ >= 0) this->openGame = clicked_ < 2 ? clicked_ : Global + this->openScope;
	}

	/* True inside a disc game under Level: the lists show only what the open level uses. */
	bool inLevel() const
	{
		return this->openScope == 1 && this->openGame >= 0 && this->openGame < Global;
	}

	/* Adds the path bar names of the chosen scope and game. */
	void addPathNames(std::vector<std::string>& rNames_) const
	{
		if(this->openScope >= 0) rNames_.push_back(this->openScope == 0 ? "Global" : "Level");
		if(this->openGame >= 0) rNames_.push_back(this->openGame >= Global ? "External" : prefabGameFolder(this->openGame == 0 ? SA_NINJA : STH2005_ONE));
	}

	/* A path bar click on the button at depth depth_ (0 = the browser's own name, 1 = the scope, 2 = the game): forgets everything below it. */
	void goBack(int depth_)
	{
		if(depth_ <= 0) this->openScope = -1;
		if(depth_ <= 1) this->openGame = -1;
	}
};

/* The names in a "a/b/c" folder path, for a path bar; empty path = none. */
inline std::vector<std::string> splitFolderPath(const std::string& path_)
{
	std::vector<std::string> names;
	size_t from = 0;
	while(from < path_.size())
	{
		size_t slash = path_.find('/', from);
		if(slash == std::string::npos) slash = path_.size();
		names.push_back(path_.substr(from, slash - from));
		from = slash + 1;
	}
	return names;
}
}
