cmake_minimum_required(VERSION 3.28)
project(SonicFantasy)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# GLAD
set(GLAD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/glad")
set(GLAD_SRC ${GLAD_DIR}/src/glad.c)

# GLM
set(GLM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/glm")

# ImGui
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/imgui")

# GLFW (vendored, Linux static build)
set(GLFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/GLFW")

# Stb_Image setup
set(STB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/stb")

# Lua 5.4.7 (vendored, MIT, https://www.lua.org/ftp/lua-5.4.7.tar.gz, the interpreter and compiler programs left out), built as a static C library. LUA_USE_POSIX only: no dlopen, so scripts can't load native libraries.
set(LUA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/lua")
file(GLOB LUA_SRC ${LUA_DIR}/*.c)
# Compiled as C++ (it supports that) so that a Lua error is a C++ exception: it unwinds our C++ frames, where a longjmp over them would not run their destructors.
set_source_files_properties(${LUA_SRC} PROPERTIES LANGUAGE CXX)
add_library(lua STATIC ${LUA_SRC})
target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
target_compile_options(lua PRIVATE -w)
target_include_directories(lua PUBLIC ${LUA_DIR})

add_executable(SonicFantasy
    src/main.cpp
    src/Misc/Utils.h
    src/Misc/Debug.h
	src/Graphics/Renderer.cpp
	src/Graphics/GPU_API.cpp
	src/Graphics/DebugUI.cpp
	src/Graphics/CollisionView.cpp
	src/Graphics/Text.cpp
	src/Graphics/Resources/Shader.cpp
	src/Graphics/Resources/Texture.cpp
	src/Graphics/Resources/TextureSet.cpp
	src/Graphics/Resources/BasicMesh.cpp
	src/Graphics/Resources/SAMesh.cpp
	src/Graphics/Resources/RenderWareMesh.cpp
	src/Graphics/InstanceData/SAInstance.cpp
	src/Graphics/InstanceData/ONEInstance.cpp
	src/Graphics/InstanceData/SALevelInstance.cpp
	src/Graphics/InstanceData/ONELevelInstance.cpp
	src/Importers/Ninja/SAMdl.cpp
	src/Importers/GameCube/GCDisc.cpp
	src/Importers/GameCube/Rel.cpp
	src/Importers/GameCube/GXTexture.cpp
	src/Importers/GameCube/SaComp.cpp
	src/Importers/Ninja/Gvm.cpp
	src/Importers/Ninja/SAMotion.cpp
	src/Importers/Ninja/SALandTable.cpp
	src/Importers/Ninja/SAShapes.cpp
	src/Importers/Ninja/SASet.cpp
	src/Importers/STH2005/Prs.cpp
	src/Importers/STH2005/ONE.cpp
	src/Importers/STH2005/Bon.cpp
	src/Importers/STH2005/CCL.cpp
	src/Importers/STH2005/Placement.cpp
	src/Importers/STH2005/RingLine.cpp
	src/Importers/STH2005/Model.cpp
	src/Importers/STH2005/Mtp.cpp
	src/Importers/Renderware/Dff.cpp
	src/Importers/Renderware/RwMaterial.cpp
	src/Importers/Renderware/World.cpp
	src/Importers/Renderware/CollisionWorld.cpp
	src/Importers/Renderware/Txd.cpp
	src/Objects/Camera.cpp
	src/Objects/Entity.cpp
	src/Objects/BABehaviors.cpp
	src/Gameplay/GameplaySystem.cpp
	src/Gameplay/GameEvents.cpp
	src/Scripting/Scripting.cpp
	src/Scripting/ScriptAPI.cpp
	src/Objects/Level.cpp
	src/Objects/SpatialIndex.cpp
	src/Loading/ResourceLoader.cpp
	src/DebugSetup.cpp
	src/Physics/Collision.cpp
	src/Physics/QuadTree.cpp
	src/Physics/PhysicsSystem.cpp
	src/Physics/CollisionSystem.cpp

    ${GLAD_SRC}
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
	${STB_DIR}/stb_image.cpp
	src/Misc/API.cpp
)

target_include_directories(SonicFantasy PUBLIC
    src
	src/Objects
	src/Gameplay
	src/Scripting
	src/Graphics
	src/Graphics/Resources
	src/Graphics/InstanceData
	src/Importers
	src/Importers/GameCube
	src/Importers/Ninja
	src/Importers/STH2005
	src/Importers/Renderware
	src/Loading
	src/Physics
	src/Keys
	src/Misc

	${GLAD_DIR}/include
	${GLFW_DIR}/include
    ${GLM_DIR}
    ${IMGUI_DIR}
	${STB_DIR}
)

# GLFW (Linux version). The static lib loads X11 at runtime through dlopen, so it only needs dl, pthread and m.
target_link_libraries(SonicFantasy PRIVATE ${GLFW_DIR}/lib/libglfw3.a pthread m)

# Lua
target_link_libraries(SonicFantasy PRIVATE lua)

# Linux OpenGL
target_link_libraries(SonicFantasy PRIVATE GL dl)

# Shaders live in the source tree and are copied next to the executable, replacing existing files, on every build.
# A custom target without outputs is never up to date, so this runs even when the executable doesn't need relinking.
add_custom_target(CopyShaders ALL
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_CURRENT_SOURCE_DIR}/Shaders
		$<TARGET_FILE_DIR:SonicFantasy>/Assets/Shaders
)
add_dependencies(CopyShaders SonicFantasy)

# The Lua scripts (Scripts/) are copied to Assets/Scripts the same way.
add_custom_target(CopyScripts ALL
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_CURRENT_SOURCE_DIR}/Scripts
		$<TARGET_FILE_DIR:SonicFantasy>/Assets/Scripts
)
add_dependencies(CopyScripts SonicFantasy)

# Enables the Pool / PoolMap validity checks
target_compile_definitions(SonicFantasy PRIVATE $<$<CONFIG:Debug>:ENGINE_DEBUG>)
