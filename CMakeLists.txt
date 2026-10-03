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

add_executable(SonicFantasy
    src/main.cpp
    src/Misc/Utils.h
    src/Misc/Debug.h
	src/Graphics/Renderer.cpp
	src/Graphics/GPU_API.cpp
	src/Graphics/Resources/Shader.cpp
	src/Graphics/Resources/Texture.cpp
	src/Graphics/Resources/TextureSet.cpp
	src/Graphics/Resources/BasicMesh.cpp
	src/Graphics/Resources/SAMesh.cpp
	src/Graphics/Resources/RenderWareMesh.cpp
	src/Graphics/InstanceData/SAInstance.cpp
	src/Graphics/InstanceData/ONEInstance.cpp
	src/Importers/Ninja/SAMdl.cpp
	src/Importers/GameCube/GCDisc.cpp
	src/Importers/GameCube/Rel.cpp
	src/Importers/Ninja/Gvm.cpp
	src/Importers/Ninja/SAMotion.cpp
	src/Importers/SonicTeam/Prs.cpp
	src/Importers/SonicTeam/STH2005ONE.cpp
	src/Importers/SonicTeam/Bon.cpp
	src/Importers/SonicTeam/STH2005Model.cpp
	src/Importers/SonicTeam/STH2005Mtp.cpp
	src/Importers/Renderware/Dff.cpp
	src/Importers/Renderware/Txd.cpp
	src/Objects/Camera.cpp
	src/Objects/Entity.cpp
	src/Loading/ResourceLoader.cpp

    ${GLAD_SRC}
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
	${STB_DIR}/stb_image.cpp
	src/Misc/API.cpp
)

target_include_directories(SonicFantasy PUBLIC
    src
	src/Objects
	src/Graphics
	src/Graphics/Resources
	src/Graphics/InstanceData
	src/Importers
	src/Importers/GameCube
	src/Importers/Ninja
	src/Importers/SonicTeam
	src/Importers/Renderware
	src/Loading
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

# Enables the Pool / PoolMap validity checks
target_compile_definitions(SonicFantasy PRIVATE $<$<CONFIG:Debug>:ENGINE_DEBUG>)
