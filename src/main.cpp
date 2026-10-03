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

inline bool OPressed = false;
inline bool PPressed = false; 

void processInputs() {
	Camera& cam = Globals::Game::cameras[0]; 
	// front / back
    float speed = Globals::Game::cameraSpeed * Globals::App::deltaTime;
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
	Globals::Game::cameras.emplace_back(Camera({0.0f, 0.0f, 0.0f}));
	
	PoolId saShaderId = Graphics::addShader("SA");
	PoolId oneShaderId = Graphics::addShader("STH2005");

	/*
	The discs stay open for the whole run: everything an instance's resource keys (ResourceKeys.h) name is loaded from
	them the first time an instance needs it.
	*/
	Globals::Discs::sadx = GameCube::openGCDisc(sadxDiscPath.c_str());
	Globals::Discs::shadow = GameCube::openGCDisc(shadowDiscPath.c_str());

	Entity& rSonic = Globals::Game::entities[addEntity("SASonic", {0, 5.3, 5})];
	rSonic.setSAGPUInstance(saShaderId, SonicMesh, SonicTextures);
	rSonic.playSAMotion(SonicIdle);

	Entity& rShadow = Globals::Game::entities[addEntity("Shadow", {0, 0, -5})];
	rShadow.setONEGPUInstance(oneShaderId, ShadowMesh, ShadowTextures);
	rShadow.playONEMotion(ShadowIdle);
	rShadow.playONEMotion(ShadowIdleLeftHand);
	rShadow.playONEMotion(ShadowIdleRightHand);

	while (!glfwWindowShouldClose(Globals::App::window)) {
		float currentFrame = glfwGetTime();
		Globals::App::deltaTime = currentFrame - Globals::App::lastFrame;
		Globals::App::lastFrame = currentFrame;

        glfwPollEvents();
		processInputs();
		for(uint32_t i = 0; i < Globals::Game::entities.objects.size(); i++)
		{
			if(Globals::Game::entities.alive[i]) Globals::Game::entities.objects[i].update(Globals::App::deltaTime);
		}
		Graphics::renderer.run(Globals::Game::cameras[0]);	
    }
	
	Graphics::renderer.destroy();
    destroyWindow(Globals::App::window);

    return 0;
}

 
