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

inline bool OPressed = false;
inline bool PPressed = false; 
inline bool CPressed = false;
inline bool VPressed = false;
inline bool LeftPressed = false;
inline bool RightPressed = false;
inline bool UpPressed = false;
inline bool DownPressed = false;
inline bool TabPressed = false;
inline bool EscPressed = false;

/*
Outside the debug mode (Globals::App::debugMode) the player is controlled and the camera sits behind it, where the mouse look points (placeholder distance and height).
*/
constexpr float FollowDistance = 45.0f;
constexpr float FollowHeight = 12.0f;

void updateFollowCamera(PoolId targetId_)
{
	Camera& cam = Globals::Game::cameras[0];
	cam.set(getEntity(targetId_).renderPosition() + glm::vec3(0.0f, FollowHeight, 0.0f) - cam.front * FollowDistance);
}

void processInputs() {
	Camera& cam = Globals::Game::cameras[0]; 
	/* The keys that fly the camera (W A S D, Space, Ctrl) only do so in the debug mode (Tab): outside it they control the player, and the camera follows it (updateFollowCamera). */
	if(Globals::App::debugMode)
	{
	// front / back
    float speed = cam.speed * Globals::App::deltaTime;
    if (glfwGetKey(Globals::App::window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        speed *= cam.speedUp;
    if (glfwGetKey(Globals::App::window, GLFW_KEY_W) == GLFW_PRESS)
        cam.move(cam.front * speed);

    if (glfwGetKey(Globals::App::window, GLFW_KEY_S) == GLFW_PRESS)
        cam.move(-cam.front * speed);

    // right / left
    glm::vec3 right = glm::normalize(glm::cross(cam.front, cam.up));
    if (glfwGetKey(Globals::App::window, GLFW_KEY_A) == GLFW_PRESS)
        cam.move(-right * speed);

    if (glfwGetKey(Globals::App::window, GLFW_KEY_D) == GLFW_PRESS)
        cam.move(right * speed);

	// up / down 
	if(glfwGetKey(Globals::App::window, GLFW_KEY_SPACE))
		cam.move(glm::vec3({0.0, 1.0, 0.0f}) * speed);
	if(glfwGetKey(Globals::App::window, GLFW_KEY_LEFT_CONTROL)) 
		cam.move(-glm::vec3({0.0, 1.0, 0.0f}) * speed);
	}

	/*
	Tab toggles the debug mode: the debug UI and the entity labels show (the camera keeps working, the cursor stays captured); leaving it captures the cursor again. While it is on, Esc frees the cursor
	(visible, no camera look) so the UI can be used. The next mouse position becomes the baseline when the camera gets the cursor back, so it does not jump.
	*/
	bool tabDown = glfwGetKey(Globals::App::window, GLFW_KEY_TAB) == GLFW_PRESS;
	if(tabDown && !TabPressed)
	{
		Globals::App::debugMode = !Globals::App::debugMode;
		if(!Globals::App::debugMode && Globals::Mouse::cursorFree)
		{
			Globals::Mouse::cursorFree = false;
			glfwSetInputMode(Globals::App::window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			Globals::Mouse::firstMouse = true;
		}
	}
	TabPressed = tabDown;
	bool escDown = glfwGetKey(Globals::App::window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
	if(escDown && !EscPressed && Globals::App::debugMode)
	{
		Globals::Mouse::cursorFree = !Globals::Mouse::cursorFree;
		glfwSetInputMode(Globals::App::window, GLFW_CURSOR, Globals::Mouse::cursorFree ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
		Globals::Mouse::firstMouse = true;
	}
	EscPressed = escDown;
	if(Globals::Mouse::cursorFree && !Graphics::debugUI.wantsMouse() && glfwGetMouseButton(Globals::App::window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
	{
		Globals::Mouse::cursorFree = false;
		glfwSetInputMode(Globals::App::window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		Globals::Mouse::firstMouse = true;
	}

	/* C toggles the collision lines view (Graphics::CollisionView). */
	bool CDown = glfwGetKey(Globals::App::window, GLFW_KEY_C) == GLFW_PRESS;
	if(CDown && !CPressed) Graphics::collisionView.enabled = !Graphics::collisionView.enabled;
	CPressed = CDown;

	/* V switches the lines view between coloring by triangle attribute and by slope. */
	bool VDown = glfwGetKey(Globals::App::window, GLFW_KEY_V) == GLFW_PRESS;
	if(VDown && !VPressed) Graphics::collisionView.colorByAttribute = !Graphics::collisionView.colorByAttribute;
	VPressed = VDown;

	/* Left / Right move the highlighted attribute of the lines view along the legend, through "none": the entities' legend while the debug UI shows its Entities sub tab, otherwise the level's. */
	bool leftDown = glfwGetKey(Globals::App::window, GLFW_KEY_LEFT) == GLFW_PRESS;
	bool rightDown = glfwGetKey(Globals::App::window, GLFW_KEY_RIGHT) == GLFW_PRESS;
	if(leftDown && !LeftPressed) Graphics::collisionView.entityTabActive ? Graphics::collisionView.selectEntityAttribute(-1) : Graphics::collisionView.selectAttribute(-1);
	if(rightDown && !RightPressed) Graphics::collisionView.entityTabActive ? Graphics::collisionView.selectEntityAttribute(1) : Graphics::collisionView.selectAttribute(1);
	LeftPressed = leftDown;
	RightPressed = rightDown;

	/* Up / Down switch the labels over the entities (Graphics::Text) between entity, mesh and collision names. */
	bool upDown = glfwGetKey(Globals::App::window, GLFW_KEY_UP) == GLFW_PRESS;
	bool downDown = glfwGetKey(Globals::App::window, GLFW_KEY_DOWN) == GLFW_PRESS;
	if(upDown && !UpPressed) Graphics::text.selectMode(-1);
	if(downDown && !DownPressed) Graphics::text.selectMode(1);
	UpPressed = upDown;
	DownPressed = downDown;

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
	/* The gameplay is the Lua scripts of Assets/Scripts (main.lua loads sadx/main.lua for Sonic and shadow/main.lua for Shadow, J switches the player, see Scripting.h); a script that fails to load is reported on stderr and the game runs without it. */
	Scripting::init();
	Scripting::loadFile("main.lua");
	Graphics::debugUI.init(Globals::App::window);
	Globals::Game::cameras.emplace_back(Camera({0.0f, 0.0f, 0.0f}));
	
	PoolId saShaderId = Graphics::addShader("SA");
	PoolId oneShaderId = Graphics::addShader("STH2005");

	/*
	The discs stay open for the whole run: everything an instance's resource keys (ResourceKeys.h) name is loaded from
	them the first time an instance needs it.
	*/
	Globals::Discs::sadx = GameCube::openGCDisc(sadxDiscPath.c_str());
	Globals::Discs::shadow = GameCube::openGCDisc(shadowDiscPath.c_str());
	PoolId sonicId = addEntity("SASonic", {-1000, -1000, -1000});
	PoolId shadowId =  addEntity("Shadow", {-1000, -1000, -1000}); 

	getEntity(sonicId).setSAGPUInstance(saShaderId, SonicMesh, SonicTextures);
	getEntity(sonicId).playSAMotion(SonicIdle);

	getEntity(shadowId).setONEGPUInstance(oneShaderId, ShadowMesh, ShadowTextures);
	getEntity(shadowId).playONEMotion(ShadowIdle);
	getEntity(shadowId).playONEMotion(ShadowIdleLeftHand);
	getEntity(shadowId).playONEMotion(ShadowIdleRightHand);



	/*
	Emerald Coast (SADX), with its collision (off while Shadow plays in Westopolis; the two would overlap at similar coordinates; its player script is sadx/main.lua).
	*/
	// PoolId emeraldCoastId = addLevel("EmeraldCoast", {0, 0, 0});
	// getLevel(emeraldCoastId).setSAGPUInstance(saShaderId, EmeraldCoastLandTable, EmeraldCoastTextures);
	// getLevel(emeraldCoastId).setSACollision(EmeraldCoastLandTable);
	// getLevel(emeraldCoastId).setSAEntities(saShaderId, EmeraldCoastLandTable);
	// setCameraPosition({-12.0f, 30.0f, 142.0f});
	// getEntity(sonicId).setPosition({0, 10, 5});
	// getEntity(shadowId).setPosition({0, 4, -5});
	// DebugSetup::addSonicShape(sonicId);
	// DebugSetup::addShadowShape(shadowId);
	// DebugSetup::enablePhysics(sonicId, shadowId);
	// Scripting::attachPlayer(sonicId);
	// Scripting::attachPlayer(shadowId);

	/*
	A Shadow stage (Stg0000Level is the one with a single section). The camera starts at the middle of the largest sector of
	the first section of stg0100 (observed in its world, with the 90 degree yaw of ONEInstance.h applied); the real start
	position of the stage is not known. The far plane is the render distance (Camera::farPlane).
	*/
	PoolId westopolisId = addLevel("Stg0100", {0, 0, 0});
	getLevel(westopolisId).setONEGPUInstance(oneShaderId, Stg0100Level);
	getLevel(westopolisId).setONECollision(Stg0100Level);
	getEntity(sonicId).setPosition({-3430, -4542 + 5.3, 5});
	getEntity(shadowId).setPosition({-3430, -4541.5, -5}); 
	setCameraPosition({-3440, -4860, 0}); 
	// getLevel(westopolisId).setONEEntities(oneShaderId, Stg0100Level);
	
	DebugSetup::enablePhysics(sonicId, shadowId);
	DebugSetup::addSonicShape(sonicId);
	DebugSetup::addShadowShape(shadowId);
	Scripting::attachPlayer(sonicId);
	Scripting::attachPlayer(shadowId);
	Globals::Game::spatialIndex.rebuild();
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
			glfwSetWindowTitle(Globals::App::window, std::format("SonicFantasy - {:.0f} FPS", Globals::App::fps).c_str());
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
		if(!Globals::App::debugMode && entityExists(Globals::Game::cameraTarget)) updateFollowCamera(Globals::Game::cameraTarget);
		// double renderStart = glfwGetTime();
		// Graphics::renderStats.resetFrame();
		// if(statsOn) glBeginQuery(GL_TIME_ELAPSED, gpuQueries[frameNumber % 3]);
		Graphics::renderer.run(Globals::Game::cameras[0]);
		// if(statsOn) glEndQuery(GL_TIME_ELAPSED);
		// double uiStart = glfwGetTime();
		if(Globals::App::debugMode) Graphics::debugUI.draw(Globals::Game::cameras[0]);
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
	Graphics::debugUI.destroy();
	Graphics::renderer.destroy();
    destroyWindow(Globals::App::window);

    return 0;
}

 
