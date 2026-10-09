#include <format>
#include <cstdio>
#include <cstdlib>
#include "RenderStats.h"
#include "Scripting.h"
#include <cstdint>
#include <glm/fwd.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "GPU_API.h"
#include "Utils.h"
#include "Globals.h"
#include "GPU_Globals.h"
#include "API.h"
#include "Camera.h"
#include "GCDisc.h"
#include "ResourceKeys.h"
#include "DebugSetup.h"
#include "LevelLoader.h"
#include "PlayerLoader.h"
#ifdef EDITOR
#include "Editor.h"
#include "ExternalPrefabs.h"
#endif

inline bool OPressed = false;
inline bool PPressed = false; 

/*
The gameplay camera sits behind the player it follows, where the mouse look points while the editor is not active (the mouse and the fly keys then move the editor camera, see Editor.h); placeholder distance and height. The player is controlled only outside the editor (Lua sf.debugMode).
*/
constexpr float FollowDistance = 70.0f;
constexpr float FollowHeight = 18.0f;

void updateFollowCamera(PoolId targetId_)
{
	Camera& cam = getMainCamera();
	glm::vec3 position = getEntity(targetId_).renderPosition() + glm::vec3(0.0f, FollowHeight, 0.0f) - cam.front * FollowDistance;

	/* Through the camera's entity, which carries the camera along (Entity::moveTo); a camera made by hand has none. */
	if(entityExists(cam.entityId)) getEntity(cam.entityId).moveTo(position);
	else cam.set(position);
}

void processInputs() {
#ifdef EDITOR
	editor.processInputs();
#endif

	// increas / decrease block height
	// bool ODown = glfwGetKey(Globals::window, GLFW_KEY_O) == GLFW_PRESS; 
	// bool PDown = glfwGetKey(Globals::window, GLFW_KEY_P) == GLFW_PRESS; 

	// if (ODown && !OPressed) {
    	// for(FaceInstance& rInstance : BlockGlobals::faces.instances)
		// {
			// float h = rInstance.height;
    		// h = std::min(h + 0.2f, 1.0f);
    		// rInstance.height = h;

			// BlockGlobals::faces.updateSSBO();
    		// OPressed = true;

    		// // printf("height: %f\n", h);
		// }
	// }

	// if (!ODown) OPressed = false;

	// if (PDown && !PPressed) {
    	// for(FaceInstance& rInstance : BlockGlobals::faces.instances)
		// {
			// float h = rInstance.height;
    		// h = std::max(h - 0.2f, 0.0f);
    		// rInstance.height = h;
			// BlockGlobals::faces.updateSSBO();
    		// PPressed = true;

    		// // printf("height: %f\n", h);
		// }
	// }

	// if (!PDown) PPressed = false;
}


int main() {
	Globals::App::window = createWindow(Globals::App::windowWidth, Globals::App::windowHeight);
	Graphics::renderer.init(); 
	Globals::Game::baBehaviors.init();
	/* The raw collision values of each game and the behaviour each has (Assets/External/Collision/Remaps.json); throws if it is wrong, nothing can be resolved without it. */
	Globals::Game::baBehaviors.loadRemaps("Assets/External/Collision/Remaps.json");
	/* Where the players' models and motions are on the discs (Assets/External/Players/Sonic.json, Shadow.json); throws if one is wrong, no player can be made without them. */
	Loading::loadPlayerKeys();
	/* The gameplay is the Lua scripts of Assets/Scripts (main.lua loads sadx/main.lua for Sonic and shadow/main.lua for Shadow, J switches the player, see Scripting.h; game.lua, run once the discs are open, loads the level and the players); a script that fails to load is reported on stderr and the game runs without it. */
	Scripting::init();
	Scripting::loadFile("main.lua");
	/* The project's own scripts (Assets/External/Scripts/main.lua, if there is one): after the games', before registerPrefabs reads the behaviours. */
	Scripting::loadExternalScripts();
	/* The tags and behaviours the editor made (Assets/External/Collision/BoundAreas.json, if there is one): they name onTouch functions, so after the scripts; throws if it is wrong. */
	Globals::Game::baBehaviors.loadBoundAreas("Assets/External/Collision/BoundAreas.json");
	/* After main.lua: a prefab reads the behaviours the scripts registered for it. Before any level or entity is made. */
	registerPrefabs();
	/* Their groups and the Shadow objects' placement layouts (Assets/External/Prefabs/Groups.json, STH2005/Placement.json). */
	Loading::loadPrefabData();
	/* The prefabs' Changed files (Assets/External/Prefabs/<game>/<KEY>/Changed/prefab.json) name behaviours, so after the scripts. */
	Loading::loadChangedPrefabs();
#ifdef EDITOR
	/* The editor's Global External prefabs (Assets/External/Prefabs/Global/prefabs.json); the level's own are read when it opens. */
	Loading::loadExternalPrefabs(Loading::ExternalScope::Project);
#endif
	addCamera("MainCamera", {0.0f, 0.0f, 0.0f});
#ifdef EDITOR
	editor.init(Globals::App::window);
#endif
	
	/* The scripts find these by name (sf.loadLevel, sf.spawnPlayer). */
	Graphics::addShader("SA");
	Graphics::addShader("STH2005");
	Graphics::addShader("GLTF");

	/*
	The discs stay open for the whole run: everything an instance's resource keys (ResourceKeys.h) name is loaded from
	them the first time an instance needs it.
	*/
	Globals::Discs::sadx = GameCube::openGCDisc(sadxDiscPath.c_str());
	Globals::Discs::shadow = GameCube::openGCDisc(shadowDiscPath.c_str());

	/* The level and the players are the script game.lua's (it needs the discs and shaders above). */
	Scripting::loadFile("game.lua");
	Globals::Game::spatialIndex.rebuild();
#ifdef EDITOR
	/* game.lua placed the gameplay camera: the editor camera starts there. */
	editor.matchGameplayCamera();
#endif
	/*
	The first frame's deltaTime would otherwise be the time since glfwInit, i.e. the whole loading time (seconds): the physics
	would move everything by that much at once, and the entities would land in a single frame.
	*/
	Globals::App::lastFrame = glfwGetTime();
	int fpsFrames = 0;
	float fpsTime = 0.0f;

	// /*
	// SF_STATS=1 prints the frame's costs to stderr once a second: the averages of the CPU time spent updating (gameplay and
	// entities), submitting the render, the debug UI and swapping buffers, the GPU time of the render (GL_TIME_ELAPSED, read three
	// frames late so it never stalls) and the GL calls counted in RenderStats.h. Printing is off otherwise.
	// */
	// bool statsOn = std::getenv("SF_STATS") != nullptr;
	// GLuint gpuQueries[3] = {0, 0, 0};
	// glGenQueries(3, gpuQueries);
	// uint64_t frameNumber = 0;
	// double gameplayMs = 0.0, updateMs = 0.0, renderMs = 0.0, uiMs = 0.0, swapMs = 0.0, gpuMs = 0.0;
	// uint32_t gpuSamples = 0;
	// uint32_t statFrames = 0;
	// double statTime = 0.0;
	// Graphics::RenderStats statSum;

	while (!glfwWindowShouldClose(Globals::App::window)) {
		float currentFrame = glfwGetTime();
		Globals::App::deltaTime = currentFrame - Globals::App::lastFrame;
		Globals::App::lastFrame = currentFrame;

		/* The frame rate over half a second at a time, also written in the window's title. */
		fpsFrames++;
		fpsTime += Globals::App::deltaTime;
		if(fpsTime >= 0.5f)
		{
			Globals::App::fps = static_cast<float>(fpsFrames) / fpsTime;
			glfwSetWindowTitle(Globals::App::window, std::format("SonicSunset - {:.0f} FPS", Globals::App::fps).c_str());
			fpsFrames = 0;
			fpsTime = 0.0f;
		}

        glfwPollEvents();
		// double updateStart = glfwGetTime();
		processInputs();
		Globals::Game::gameplaySystem.update(Globals::App::deltaTime);
		// double gameplayEnd = glfwGetTime();
		for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
		{
			if(Globals::Game::levels.alive[i]) Globals::Game::levels.objects[i].update(Globals::App::deltaTime);
		}
		for(uint32_t i = 0; i < Globals::Game::entities.objects.size(); i++)
		{
			if(Globals::Game::entities.alive[i]) Globals::Game::entities.objects[i].update(Globals::App::deltaTime);
		}
		Globals::Game::gameEvents.drain();
		if(entityExists(Globals::Game::cameraTarget)) updateFollowCamera(Globals::Game::cameraTarget);
		// double renderStart = glfwGetTime();
		// Graphics::renderStats.resetFrame();
		// if(statsOn) glBeginQuery(GL_TIME_ELAPSED, gpuQueries[frameNumber % 3]);
#ifdef EDITOR
		Graphics::renderer.runAll(editor.active ? &editor.camera : nullptr);
#else
		Graphics::renderer.runAll();
#endif
		// if(statsOn) glEndQuery(GL_TIME_ELAPSED);
		// double uiStart = glfwGetTime();
#ifdef EDITOR
		editor.draw();
#endif
		// double swapStart = glfwGetTime();
		glfwSwapBuffers(Globals::App::window);

		// if(statsOn)
		// {
			// double frameEnd = glfwGetTime();
			// gameplayMs += (gameplayEnd - updateStart) * 1000.0;
			// updateMs += (renderStart - gameplayEnd) * 1000.0;
			// renderMs += (uiStart - renderStart) * 1000.0;
			// uiMs += (swapStart - uiStart) * 1000.0;
			// swapMs += (frameEnd - swapStart) * 1000.0;
			// if(frameNumber >= 3)
			// {
				// GLuint64 nanoseconds = 0;
				// glGetQueryObjectui64v(gpuQueries[(frameNumber + 1) % 3], GL_QUERY_RESULT, &nanoseconds);
				// gpuMs += static_cast<double>(nanoseconds) / 1.0e6;
				// gpuSamples++;
			// }
			// const Graphics::RenderStats& rNow = Graphics::renderStats;
			// statSum.draws += rNow.draws;
			// statSum.indices += rNow.indices;
			// statSum.programCalls += rNow.programCalls;
			// statSum.programChanges += rNow.programChanges;
			// statSum.vaoCalls += rNow.vaoCalls;
			// statSum.vaoChanges += rNow.vaoChanges;
			// statSum.textureCalls += rNow.textureCalls;
			// statSum.textureChanges += rNow.textureChanges;
			// statSum.uniformUploads += rNow.uniformUploads;
			// statSum.boneUploads += rNow.boneUploads;
			// statSum.entitiesDrawn += rNow.entitiesDrawn;
			// statSum.levelPiecesDrawn += rNow.levelPiecesDrawn;
			// statFrames++;
			// statTime += Globals::App::deltaTime;
			// if(statTime >= 1.0)
			// {
				// double n = statFrames;
				// double g = gpuSamples ? gpuSamples : 1;
				// fprintf(stderr, "[stats] fps %.0f | ms: gameplay %.2f update %.2f render(cpu) %.2f ui %.2f swap %.2f gpu %.2f | per frame: draws %.0f tris %.0f ents %.0f lvlsub %.0f | program %.0f/%.0f vao %.0f/%.0f tex %.0f/%.0f (calls/changes) uniforms %.0f bones %.0f\n",
					// n / statTime, gameplayMs / n, updateMs / n, renderMs / n, uiMs / n, swapMs / n, gpuMs / g,
					// statSum.draws / n, statSum.indices / 3.0 / n, statSum.entitiesDrawn / n, statSum.levelPiecesDrawn / n,
					// statSum.programCalls / n, statSum.programChanges / n, statSum.vaoCalls / n, statSum.vaoChanges / n,
					// statSum.textureCalls / n, statSum.textureChanges / n, statSum.uniformUploads / n, statSum.boneUploads / n);
				// statSum = Graphics::RenderStats();
				// statFrames = 0;
				// statTime = 0.0;
				// gameplayMs = updateMs = renderMs = uiMs = swapMs = gpuMs = 0.0;
				// gpuSamples = 0;
			// }
		// }
		// frameNumber++;
    }
	
	Scripting::destroy();
#ifdef EDITOR
	editor.destroy();
#endif
	Graphics::renderer.destroy();
    destroyWindow(Globals::App::window);

    return 0;
}

 
