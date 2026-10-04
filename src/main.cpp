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
inline bool TabPressed = false;

void processInputs() {
	Camera& cam = Globals::Game::cameras[0]; 
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

	/*
	Tab frees the cursor (visible, no camera look) so the debug UI can be used; a left click anywhere the UI does not cover captures it again (the next position becomes the mouse baseline, so the
	camera does not jump).
	*/
	bool tabDown = glfwGetKey(Globals::App::window, GLFW_KEY_TAB) == GLFW_PRESS;
	if(tabDown && !TabPressed)
	{
		Globals::Mouse::cursorFree = !Globals::Mouse::cursorFree;
		glfwSetInputMode(Globals::App::window, GLFW_CURSOR, Globals::Mouse::cursorFree ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
		Globals::Mouse::firstMouse = true;
	}
	TabPressed = tabDown;
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

	/* Left / Right (or Up / Down) move the highlighted attribute of the lines view along the legend, through "none". */
	bool leftDown = glfwGetKey(Globals::App::window, GLFW_KEY_LEFT) == GLFW_PRESS || glfwGetKey(Globals::App::window, GLFW_KEY_UP) == GLFW_PRESS;
	bool rightDown = glfwGetKey(Globals::App::window, GLFW_KEY_RIGHT) == GLFW_PRESS || glfwGetKey(Globals::App::window, GLFW_KEY_DOWN) == GLFW_PRESS;
	if(leftDown && !LeftPressed) Graphics::collisionView.selectAttribute(-1);
	if(rightDown && !RightPressed) Graphics::collisionView.selectAttribute(1);
	LeftPressed = leftDown;
	RightPressed = rightDown;

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
	Emerald Coast (SADX), with its collision. The Shadow stage below is off while it is shown; the two would overlap at similar coordinates.
	*/
	// PoolId emeraldCoastId = addLevel("EmeraldCoast", {0, 0, 0});
	// getLevel(emeraldCoastId).setSAGPUInstance(saShaderId, EmeraldCoastLandTable, EmeraldCoastTextures);
	// getLevel(emeraldCoastId).setSACollision(EmeraldCoastLandTable);
	// getLevel(emeraldCoastId).setSAEntities(saShaderId, EmeraldCoastLandTable);
	// setCameraPosition({-12.0f, 30.0f, 142.0f});
	// getEntity(sonicId).setPosition({0, 4, 5}); 
	// getEntity(shadowId).setPosition({0, 4, -5}); 

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
	getLevel(westopolisId).setONEEntities(oneShaderId, Stg0100Level);
	
	DebugSetup::registerBoundAreas();
	DebugSetup::enablePhysics(sonicId, shadowId);
	DebugSetup::addSonicShape(sonicId);
	DebugSetup::addShadowShape(shadowId);
	/*
	The first frame's deltaTime would otherwise be the time since glfwInit, i.e. the whole loading time (seconds): the physics
	would move everything by that much at once, and the entities would land in a single frame.
	*/
	Globals::App::lastFrame = glfwGetTime();

	while (!glfwWindowShouldClose(Globals::App::window)) {
		float currentFrame = glfwGetTime();
		Globals::App::deltaTime = currentFrame - Globals::App::lastFrame;
		Globals::App::lastFrame = currentFrame;

        glfwPollEvents();
		processInputs();
		for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
		{
			if(Globals::Game::levels.alive[i]) Globals::Game::levels.objects[i].update(Globals::App::deltaTime);
		}
		for(uint32_t i = 0; i < Globals::Game::entities.objects.size(); i++)
		{
			if(Globals::Game::entities.alive[i]) Globals::Game::entities.objects[i].update(Globals::App::deltaTime);
		}
		Globals::Physics::physicsSystem.update(Globals::App::deltaTime);
		Globals::Physics::collisionSystem.update();
		Graphics::renderer.run(Globals::Game::cameras[0]);	
		Graphics::debugUI.draw(Globals::Game::cameras[0]);
		glfwSwapBuffers(Globals::App::window);
    }
	
	Graphics::debugUI.destroy();
	Graphics::renderer.destroy();
    destroyWindow(Globals::App::window);

    return 0;
}

 
