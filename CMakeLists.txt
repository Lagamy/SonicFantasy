cmake_minimum_required(VERSION 3.28)
project(SonicSunset)

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

# GLFW (vendored static builds: lib/libglfw3.a for Linux, lib/glfw3.lib for Windows)
set(GLFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/GLFW")

# Stb_Image setup
set(STB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/stb")

# cgltf 1.15 (vendored, MIT, https://github.com/jkuhlmann/cgltf): single-header glTF 2.0 parser, implementation in cgltf.cpp
set(CGLTF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/cgltf")

# Lua 5.4.7 (vendored, MIT, https://www.lua.org/ftp/lua-5.4.7.tar.gz, the interpreter and compiler programs left out), built as a static C library. LUA_USE_POSIX only on non-Windows (Lua picks its Windows setup itself): no dlopen, so scripts can't load native libraries.
set(LUA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/lua")
file(GLOB LUA_SRC ${LUA_DIR}/*.c)
# Compiled as C++ (it supports that) so that a Lua error is a C++ exception: it unwinds our C++ frames, where a longjmp over them would not run their destructors.
set_source_files_properties(${LUA_SRC} PROPERTIES LANGUAGE CXX)
add_library(lua STATIC ${LUA_SRC})
if(WIN32)
	target_compile_options(lua PRIVATE /w)
else()
	target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
	target_compile_options(lua PRIVATE -w)
endif()
target_include_directories(lua PUBLIC ${LUA_DIR})

# The editor: the debug UI (ImGui), the collision view, the entity labels, the editor camera (src/EngineEditor/Editor.h) and the GL debug output.
# Edit the value below to switch it. FORCE makes this line win over the cache, so a change here is picked up by the next
# CMake run with no -D flag; a -DEDITOR=... on the command line is ignored.
set(EDITOR ON CACHE BOOL "Build the editor / debug tools" FORCE)

add_executable(SonicSunset
    src/main.cpp
    src/Misc/Utils.h
    src/EngineEditor/Debug/Debug.h
	src/Graphics/Renderer.cpp
	src/Graphics/RenderTarget.cpp
	src/Graphics/GPU_API.cpp
	src/Graphics/DashTrail.cpp
	src/Graphics/EffectBalls.cpp
	src/Graphics/Resources/Shader.cpp
	src/Graphics/Resources/Texture.cpp
	src/Graphics/Resources/TextureSet.cpp
	src/Graphics/Resources/BasicMesh.cpp
	src/Graphics/Resources/ExternalModel.cpp
	src/Graphics/Resources/VertexLayout.cpp
	src/Graphics/Resources/SAMesh.cpp
	src/Graphics/Resources/RenderWareMesh.cpp
	src/Graphics/InstanceData/SAInstance.cpp
	src/Graphics/InstanceData/ONEInstance.cpp
	src/Graphics/InstanceData/SALevelInstance.cpp
	src/Graphics/InstanceData/ONELevelInstance.cpp
	src/Graphics/InstanceData/GLTFLevelInstance.cpp
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
	src/Importers/Renderware/Uva.cpp
	src/Importers/External/ModelFile.cpp
	src/Importers/External/TextureFile.cpp
	src/Importers/External/GLTFFile.cpp
	src/Importers/External/CollisionFile.cpp
	src/Objects/Camera.cpp
	src/Objects/Entity.cpp
	src/Objects/BABehaviors.cpp
	src/Gameplay/GameplaySystem.cpp
	src/Gameplay/GameEvents.cpp
	src/Scripting/Scripting.cpp
	src/Scripting/ScriptAPI.cpp
	src/Objects/Level.cpp
	src/Objects/Prefab.cpp
	src/Objects/SpatialIndex.cpp
	src/Loading/ResourceLoader.cpp
	src/Loading/LevelLoader.cpp
	src/Loading/PlayerLoader.cpp
	src/Loading/ExternalAssets.cpp
	src/EngineEditor/Debug/DebugSetup.cpp
	src/Physics/Collision.cpp
	src/Physics/QuadTree.cpp
	src/Physics/PhysicsSystem.cpp
	src/Physics/CollisionSystem.cpp

    ${GLAD_SRC}
	${STB_DIR}/stb_image.cpp
	${STB_DIR}/stb_image_write.cpp
	${CGLTF_DIR}/cgltf.cpp
	src/Misc/API.cpp
)

if(EDITOR)
	target_sources(SonicSunset PRIVATE
		src/EngineEditor/Editor.cpp
		src/EngineEditor/EditorCamera.cpp
		src/EngineEditor/Gizmo.cpp
		src/EngineEditor/Debug/DebugUI.cpp
		src/EngineEditor/Debug/CollisionView.cpp
		src/Loading/GLTFImport.cpp
		src/Loading/ExternalPrefabs.cpp
		src/EngineEditor/Debug/Text.cpp
		src/EngineEditor/Panels/EntityListPanel.cpp
		src/EngineEditor/Panels/InspectorPanel.cpp
		src/EngineEditor/Panels/AssetPanel.cpp
		src/EngineEditor/Panels/AssetThumbnails.cpp
		src/EngineEditor/Panels/PrefabPanel.cpp
		src/EngineEditor/Panels/PrefabThumbnails.cpp
		${IMGUI_DIR}/imgui.cpp
		${IMGUI_DIR}/imgui_draw.cpp
		${IMGUI_DIR}/imgui_widgets.cpp
		${IMGUI_DIR}/imgui_tables.cpp
		${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
		${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
	)
	target_compile_definitions(SonicSunset PRIVATE EDITOR)
	# Where the editor saves level folders (Levels/ of the source tree; the copy in Assets/Levels is written too).
	target_compile_definitions(SonicSunset PRIVATE SF_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
endif()

target_include_directories(SonicSunset PUBLIC
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
	src/Importers/External
	src/Loading
	src/Physics
	src/Keys
	src/Misc
	src/EngineEditor
	src/EngineEditor/Debug
	src/EngineEditor/Panels

	${GLAD_DIR}/include
	${GLFW_DIR}/include
    ${GLM_DIR}
    ${IMGUI_DIR}
	${STB_DIR}
	${CGLTF_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/third-party
)

# Platform libraries. Linux: the static GLFW lib loads X11 at runtime through dlopen, so it only needs dl, pthread and m.
# Windows: the vendored glfw3.lib is a static MSVC build, so it needs the system libraries GLFW calls into.
if(WIN32)
	target_link_libraries(SonicSunset PRIVATE ${GLFW_DIR}/lib/glfw3.lib opengl32 gdi32 user32 shell32)
	target_compile_definitions(SonicSunset PRIVATE NOMINMAX _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
	if(MSVC)
		target_compile_options(SonicSunset PRIVATE /utf-8)
		target_compile_options(lua PRIVATE /utf-8)
	endif()
else()
	target_link_libraries(SonicSunset PRIVATE ${GLFW_DIR}/lib/libglfw3.a pthread m GL dl)
endif()

# Lua
target_link_libraries(SonicSunset PRIVATE lua)

# Shaders live in the source tree and are copied next to the executable, replacing existing files, on every build.
# A custom target without outputs is never up to date, so this runs even when the executable doesn't need relinking.
add_custom_target(CopyShaders ALL
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_CURRENT_SOURCE_DIR}/Shaders
		$<TARGET_FILE_DIR:SonicSunset>/Assets/Shaders
)
add_dependencies(CopyShaders SonicSunset)

# The Lua scripts (Scripts/) are copied to Assets/Scripts the same way.
add_custom_target(CopyScripts ALL
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_CURRENT_SOURCE_DIR}/Scripts
		$<TARGET_FILE_DIR:SonicSunset>/Assets/Scripts
)
add_dependencies(CopyScripts SonicSunset)

# The levels (Levels/<name>/: <name>.json and the level's own Models, Textures, Collision, prefabs.json; read by Loading::loadLevel) are copied to Assets/Levels the same way.
add_custom_target(CopyLevels ALL
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_CURRENT_SOURCE_DIR}/Levels
		$<TARGET_FILE_DIR:SonicSunset>/Assets/Levels
)
add_dependencies(CopyLevels SonicSunset)

# The asset catalog (Catalog/, made by tools/genAssetCatalog.py, read by the editor's Assets tab) is copied to Assets/Catalog the same way.
add_custom_target(CopyCatalog ALL
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_CURRENT_SOURCE_DIR}/Catalog
		$<TARGET_FILE_DIR:SonicSunset>/Assets/Catalog
)
add_dependencies(CopyCatalog SonicSunset)

# The editor-wide ("Global") external assets (External/: Models, Textures, Prefabs/Global/prefabs.json; the level's own are in its Levels/<name>/ folder; read by Loading::scanExternalAssets) are copied to Assets/External the same way.
add_custom_target(CopyExternal ALL
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_CURRENT_SOURCE_DIR}/External
		$<TARGET_FILE_DIR:SonicSunset>/Assets/External
)
add_dependencies(CopyExternal SonicSunset)

# Enables the Pool / PoolMap validity checks
target_compile_definitions(SonicSunset PRIVATE $<$<CONFIG:Debug>:ENGINE_DEBUG>)
