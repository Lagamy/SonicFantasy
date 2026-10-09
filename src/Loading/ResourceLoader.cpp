#include "ResourceLoader.h"
#include "GPU_Globals.h"
#include "Globals.h"
#include "Rel.h"
#include "Gvm.h"
#include "SAMdl.h"
#include "SAMotion.h"
#include "SALandTable.h"
#include "SAShapes.h"
#include "ONE.h"
#include "World.h"
#include "CollisionWorld.h"
#include "CCL.h"
#include "ONEInstance.h"
#include "CollisionFile.h"
#include "ModelFile.h"
#include "TextureFile.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <filesystem>
#include <map>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

/*
Loading and lifetime of the resources instances use: reads the discs (Globals::Discs), runs the importers, keeps the
user counts and frees what no instance uses. The Graphics pools themselves only store.
*/
namespace Loading
{
    using namespace Graphics;

    /*
    The set for the key: the one already loaded, or imported now from the SADX disc (importGvm names it by the path).
    created tells the caller whether it has to undo the load if a later step fails. The set is not counted as used here.
    */
    static PoolId findOrLoadTextureSet(const SATextureSetKey& rKey_, bool& rCreated_)
    {
        auto found = textureSets.nameToId.find(rKey_.path);
        if(found != textureSets.nameToId.end())
        {
            rCreated_ = false;
            return found->second;
        }
        rCreated_ = true;
        try
        {
            Ninja::importGvm(rKey_.path, Globals::Discs::sadx.readFile(rKey_.path));
        }
        catch(...)
        {
            found = textureSets.nameToId.find(rKey_.path);
            if(found != textureSets.nameToId.end()) textureSets.remove(found->second);
            throw;
        }
        return textureSets.nameToId.find(rKey_.path)->second;
    }

    /* The relocated .rel module at path rel_ on the SADX disc: the cached one, or read and relocated now (no user yet). */
    static std::unordered_map<std::string, RelModule>::iterator findOrLoadRelModule(const char* rel_)
    {
        auto module = relModules.find(rel_);
        if(module == relModules.end())
        {
            module = relModules.emplace(rel_, RelModule{GameCube::relocateRel(Globals::Discs::sadx.readFile(rel_)), 0}).first;
        }
        return module;
    }

    /* Gives back one user of a .rel module; it is freed when no mesh or motion imported from it is left. */
    static void releaseRelModule(const std::string& rRel_)
    {
        auto module = relModules.find(rRel_);
        if(module != relModules.end() && --module->second.users == 0)
        {
            relModules.erase(module);
        }
    }

    /* Frees a .rel module no mesh or motion imported from it is using (users 0), e.g. one loaded only to read a land table. */
    static void eraseRelModuleIfUnused(const char* rel_)
    {
        auto module = relModules.find(rel_);
        if(module != relModules.end() && module->second.users == 0)
        {
            relModules.erase(module);
        }
    }

    /*
    The SAMesh for the key (named "<rel>@0x<offset>"): the one already imported, or imported now from the SADX disc.
    The relocated .rel is kept in relModules (one user per mesh or motion imported from it), so only the first import of
    a module reads and relocates it. If the import fails, the partial mesh and an unused module are removed again.
    */
    static PoolId findOrImportSAMesh(const SAModelKey& rKey_, bool& rCreated_)
    {
        std::string name = std::format("{}@0x{:X}", rKey_.rel, rKey_.rootOffset);
        auto found = saMeshes.nameToId.find(name);
        if(found != saMeshes.nameToId.end())
        {
            rCreated_ = false;
            return found->second;
        }
        rCreated_ = true;

        auto module = findOrLoadRelModule(rKey_.rel);
        try
        {
            PoolId meshId = Ninja::importSAModel(name.c_str(), module->second.bytes, rKey_.rootOffset, true);
            saMeshes[meshId].rel = rKey_.rel;
            module->second.users++;
            return meshId;
        }
        catch(...)
        {
            found = saMeshes.nameToId.find(name);
            if(found != saMeshes.nameToId.end()) saMeshes.remove(found->second);
            if(module->second.users == 0) relModules.erase(module);
            throw;
        }
    }

    /* Removes an SAMesh and gives back its user of its .rel module. */
    static void removeSAMesh(PoolId saMeshId_)
    {
        std::string rel = saMeshes[saMeshId_].rel;
        saMeshes.remove(saMeshId_);
        releaseRelModule(rel);
    }

    /*
    The SAMotion for the key (named "<rel>@0x<offset>"): the one already imported, or imported now, from the relModules
    cache like the meshes. Not counted as used here.
    */
    static PoolId findOrImportSAMotion(const SAMotionKey& rKey_)
    {
        std::string name = std::format("{}@0x{:X}", rKey_.rel, rKey_.offset);
        auto found = saMotions.nameToId.find(name);
        if(found != saMotions.nameToId.end())
        {
            return found->second;
        }

        auto module = findOrLoadRelModule(rKey_.rel);
        try
        {
            PoolId motionId = saMotions.add(name.c_str(), Ninja::readSAMotion(module->second.bytes, rKey_.offset, rKey_.nodeCount, true));
            saMotions[motionId].rel = rKey_.rel;
            module->second.users++;
            return motionId;
        }
        catch(...)
        {
            if(module->second.users == 0) relModules.erase(module);
            throw;
        }
    }

    /* Gives back one user of the motion; at 0 it is removed and its .rel module user given back. */
    void releaseSAMotion(PoolId saMotionId_)
    {
        if(--saMotions[saMotionId_].users != 0) return;
        std::string rel = saMotions[saMotionId_].rel;
        saMotions.remove(saMotionId_);
        releaseRelModule(rel);
    }

    /*
    Makes the SA instance play the key's motion from its first frame, looping (SAInstance::update). The key's node
    count has to be the instance's model's. The instance holds one user of the motion; the previous one is given back.
    */
    void playSAMotion(PoolId saInstanceId_, const SAMotionKey& rKey_)
    {
        SAInstance& rInstance = saInstances[saInstanceId_];
        size_t nodeCount = saMeshes[rInstance.saMeshId].nodes.size();
        if(rKey_.nodeCount != nodeCount)
        {
            throw std::runtime_error(std::format("SAMotion: {}@0x{:X} animates {} nodes, but the model has {}.", rKey_.rel, rKey_.offset, rKey_.nodeCount, nodeCount));
        }

        PoolId motionId = findOrImportSAMotion(rKey_);
        saMotions[motionId].users++;
        PoolId previous = rInstance.currentSAAnimation;
        rInstance.currentSAAnimation = motionId;
        rInstance.animationFrame = 0.0f;
        if(previous != UninitializedPoolId) releaseSAMotion(previous);
    }

    /*
    Gives back one user of the mesh. At 0 the mesh is removed, together with the bindings that sets keep for it
    (a binding refers to its mesh by id).
    */
    void releaseSAMesh(PoolId saMeshId_)
    {
        if(--saMeshes[saMeshId_].users != 0) return;

        for(uint32_t i = 0; i < textureSets.objects.size(); i++)
        {
            if(!textureSets.alive[i]) continue;
            std::vector<std::pair<PoolId, PoolId>>& rBindings = textureSets.objects[i].saBindings;
            for(const auto& rBinding : rBindings)
            {
                if(rBinding.first == saMeshId_) textureBindings.remove(rBinding.second);
            }
            std::erase_if(rBindings, [saMeshId_](const std::pair<PoolId, PoolId>& rBinding_) { return rBinding_.first == saMeshId_; });
        }
        removeSAMesh(saMeshId_);
    }

    void releaseTextureSet(PoolId textureSetId_)
    {
        if(--textureSets[textureSetId_].users == 0)
        {
            textureSets.remove(textureSetId_);
        }
    }

    /*
    The set's TextureBinding for a mesh: the existing one, or a new one that resolves every submesh's textureIndex
    through rOrdered_ (the set's file order, or a land table's texlist resolved against the set). An index past the end
    of rOrdered_ throws; a submesh without a texture (SANoTexture) gets UninitializedPoolId. A mesh is bound once per set,
    whatever rOrdered_ was the first time.
    */
    static PoolId findOrBindSATextures(PoolId textureSetId_, PoolId saMeshId_, const char* textureSetKey_, const std::vector<PoolId>& rOrdered_)
    {
        TextureSet& rSet = textureSets[textureSetId_];
        for(const auto& rBinding : rSet.saBindings)
        {
            if(rBinding.first == saMeshId_) return rBinding.second;
        }

        TextureBinding binding;
        for(const SANode& rNode : saMeshes[saMeshId_].nodes)
        {
            std::vector<PoolId>& rIds = binding.submeshTextureIds.emplace_back();
            for(const SASubmesh& rSubmesh : rNode.submeshes)
            {
                if(rSubmesh.textureIndex == SANoTexture)
                {
                    rIds.push_back(UninitializedPoolId);
                    continue;
                }
                if(rSubmesh.textureIndex >= rOrdered_.size())
                {
                    throw std::runtime_error(std::format("TextureBinding: submesh uses texture {} but set {} has {} textures.", rSubmesh.textureIndex, textureSetKey_, rOrdered_.size()));
                }
                rIds.push_back(rOrdered_[rSubmesh.textureIndex]);
            }
        }
        PoolId bindingId = textureBindings.add(std::move(binding));
        rSet.saBindings.emplace_back(saMeshId_, bindingId);
        return bindingId;
    }

    /*
    Makes an SA instance draw another mesh (e.g. Sonic's curled model), keeping its shader and TextureSet: the mesh is found or imported and bound to the instance's set (the set's file order), the instance
    takes one user of it and gives back the old mesh's. The motion it plays animates the old mesh's nodes, so it is stopped (play another with playSAMotion); the pose is dropped. The same mesh is a no-op.
    If the import or the binding fails, what this call imported is removed again and the instance is unchanged.
    */
    void changeSAMesh(PoolId saInstanceId_, const SAModelKey& rMesh_)
    {
        SAInstance& rInstance = saInstances[saInstanceId_];
        bool meshCreated = false;
        PoolId meshId = findOrImportSAMesh(rMesh_, meshCreated);
        if(meshId == rInstance.saMeshId) return;

        PoolId bindingId = UninitializedPoolId;
        if(rInstance.textureSetId != UninitializedPoolId)
        {
            try
            {
                bindingId = findOrBindSATextures(rInstance.textureSetId, meshId, "(the instance's set)", textureSets[rInstance.textureSetId].ordered);
            }
            catch(...)
            {
                if(meshCreated) removeSAMesh(meshId);
                throw;
            }
        }
        saMeshes[meshId].users++;
        PoolId previousMesh = rInstance.saMeshId;
        PoolId previousMotion = rInstance.currentSAAnimation;
        rInstance.saMeshId = meshId;
        rInstance.textureBindingId = bindingId;
        rInstance.currentSAAnimation = UninitializedPoolId;
        rInstance.animationFrame = 0.0f;
        rInstance.nodePose.clear();
        rInstance.poseDirty = false;
        if(previousMotion != UninitializedPoolId) releaseSAMotion(previousMotion);
        releaseSAMesh(previousMesh);
    }

    /*
    Makes an SA instance draw its mesh with another TextureSet (a .gvm of the disc, loaded now if needed; nullptr = untextured), keeping the mesh and its motion: the mesh is bound to the new set in the set's file order, so the set must hold every texture
    id the mesh's materials use (an index past its end throws). The instance takes one user of the new set and gives back the old one's. The same set is a no-op. If the load or the binding fails, what this call loaded is removed again and the instance is unchanged.
    */
    void changeSATextureSet(PoolId saInstanceId_, const SATextureSetKey* pTextures_)
    {
        SAInstance& rInstance = saInstances[saInstanceId_];
        PoolId setId = UninitializedPoolId;
        PoolId bindingId = UninitializedPoolId;
        bool setCreated = false;
        try
        {
            if(pTextures_ != nullptr)
            {
                setId = findOrLoadTextureSet(*pTextures_, setCreated);
                if(setId == rInstance.textureSetId) return;
                bindingId = findOrBindSATextures(setId, rInstance.saMeshId, pTextures_->path, textureSets[setId].ordered);
            }
        }
        catch(...)
        {
            if(setCreated && setId != UninitializedPoolId) textureSets.remove(setId);
            throw;
        }
        if(setId != UninitializedPoolId) textureSets[setId].users++;
        PoolId previousSet = rInstance.textureSetId;
        rInstance.textureSetId = setId;
        rInstance.textureBindingId = bindingId;
        if(previousSet != UninitializedPoolId) releaseTextureSet(previousSet);
    }

    /*
    The archive at the key's path on the Shadow disc: the one already in the pool, or read and parsed now.
    created tells the caller whether it has to undo the load if a later step fails. It is not counted as used here.
    */
    static PoolId findOrLoadONE(const STH2005::ArchiveKey& rKey_, bool& rCreated_)
    {
        auto found = oneArchives.nameToId.find(rKey_.path);
        if(found != oneArchives.nameToId.end())
        {
            rCreated_ = false;
            return found->second;
        }
        rCreated_ = true;
        return oneArchives.add(rKey_.path, STH2005::openONE(Globals::Discs::shadow.readFile(rKey_.path)));
    }

    void releaseONE(PoolId archiveId_)
    {
        if(--oneArchives[archiveId_].users == 0)
        {
            oneArchives.remove(archiveId_);
        }
    }

    /*
    Everything the instance needs is found or loaded from its keys, then it holds one user of the mesh and of the
    texture set (if pTextures_ is not nullptr) until destroyed. If a step fails, what this call loaded is removed again.
    pOrder_ is the Texture pool id of each texture id of the mesh; nullptr means the set's file order.
    */
    static PoolId createSAInstance(PoolId shaderId_, const SAModelKey& rMesh_, const SATextureSetKey* pTextures_, const std::vector<PoolId>* pOrder_)
    {
        bool meshCreated = false;
        bool setCreated = false;
        PoolId meshId = findOrImportSAMesh(rMesh_, meshCreated);
        PoolId setId = UninitializedPoolId;
        PoolId bindingId = UninitializedPoolId;
        try
        {
            if(pTextures_ != nullptr)
            {
                setId = findOrLoadTextureSet(*pTextures_, setCreated);
                bindingId = findOrBindSATextures(setId, meshId, pTextures_->path, pOrder_ ? *pOrder_ : textureSets[setId].ordered);
            }
        }
        catch(...)
        {
            if(setCreated && setId != UninitializedPoolId) textureSets.remove(setId);
            if(meshCreated) removeSAMesh(meshId);
            throw;
        }
        saMeshes[meshId].users++;
        if(setId != UninitializedPoolId) textureSets[setId].users++;
        return saInstances.add(SAInstance{meshId, shaderId_, setId, bindingId, UninitializedPoolId, 0.0f});
    }

    PoolId addSAInstance(PoolId shaderId_, const SAModelKey& rMesh_, const SATextureSetKey* pTextures_)
    {
        return createSAInstance(shaderId_, rMesh_, pTextures_, nullptr);
    }

    /*
    Surface flag bit of a land table piece that marks it as drawn (hypothesis, unverified: recalled from SA Tools'
    SurfaceFlags; observed: 316 of the 480 pieces of Emerald Coast's first land table have exactly this bit, the others
    carry solid / water bits). Pieces without it are collision only and are not imported.
    */
    constexpr uint32_t LandTableVisible = 0x80000000;

    /*
    True if every submesh of the mesh uses a texture whose texlist name is in rSkipped_ (a mesh without submeshes is not
    skipped). Checked after the import because the submeshes' texture ids are only known then; the instance of a skipped
    piece is removed again, which removes the mesh too.
    */
    static bool isSkippedPiece(const SAMesh& rMesh_, const std::vector<std::string>& rTextureNames_, const std::vector<std::string>& rSkipped_)
    {
        bool any = false;
        for(const SANode& rNode : rMesh_.nodes)
        {
            for(const SASubmesh& rSubmesh : rNode.submeshes)
            {
                any = true;
                if(rSubmesh.textureIndex == SANoTexture || rSubmesh.textureIndex >= rTextureNames_.size()) return false;
                if(std::find(rSkipped_.begin(), rSkipped_.end(), rTextureNames_[rSubmesh.textureIndex]) == rSkipped_.end()) return false;
            }
        }
        return any;
    }

    /*
    A level from a land table: every visible piece becomes an SAInstance (its model is an SAMesh imported from the same
    .rel), textured through the land table's texlist: a texture id of a piece is a name of the texlist, looked up in the
    TextureSet of rTextures_ (a name the set doesn't have throws). If a step fails, what this call loaded is removed again.
    */
    PoolId addSALevelInstance(PoolId shaderId_, const SALandTableKey& rLandTable_, const SATextureSetKey& rTextures_, const std::vector<std::string>& rSkippedTextures_)
    {
        Ninja::SALandTable landTable;
        bool setCreated = false;
        PoolId setId = UninitializedPoolId;
        try
        {
            landTable = Ninja::readSALandTable(findOrLoadRelModule(rLandTable_.rel)->second.bytes, rLandTable_.offset);
            setId = findOrLoadTextureSet(rTextures_, setCreated);
        }
        catch(...)
        {
            eraseRelModuleIfUnused(rLandTable_.rel);
            throw;
        }
        SALevelInstance level;
        try
        {
            std::vector<PoolId> order;
            const TextureSet& rSet = textureSets[setId];
            for(const std::string& rName : landTable.textureNames)
            {
                auto found = std::find(rSet.names.begin(), rSet.names.end(), rName);
                if(found == rSet.names.end())
                {
                    throw std::runtime_error(std::format("SALevelInstance: texlist texture {} of the land table at 0x{:X} is not in {}.", rName, rLandTable_.offset, rTextures_.path));
                }
                order.push_back(rSet.ordered[found - rSet.names.begin()]);
            }

            for(const Ninja::SALandTableCol& rCol : landTable.cols)
            {
                if(!(rCol.flags & LandTableVisible)) continue;
                PoolId instanceId = createSAInstance(shaderId_, SAModelKey{rLandTable_.rel, rCol.modelOffset}, &rTextures_, &order);
                if(isSkippedPiece(saMeshes[saInstances[instanceId].saMeshId], landTable.textureNames, rSkippedTextures_))
                {
                    saInstances.remove(instanceId);
                    continue;
                }
                level.pieces.push_back(SALevelPiece{instanceId, rCol.center, rCol.radius});
            }
        }
        catch(...)
        {
            level.destroy();
            auto found = textureSets.nameToId.find(rTextures_.path);
            if(setCreated && found != textureSets.nameToId.end() && textureSets[found->second].users == 0) textureSets.remove(found->second);
            eraseRelModuleIfUnused(rLandTable_.rel);
            throw;
        }
        eraseRelModuleIfUnused(rLandTable_.rel);
        return saLevelInstances.add(std::move(level));
    }

    /*
    A Shadow stage as a level: the texture archive and its .TXD entry are found or loaded, then every section archive is
    read from the Shadow disc (not kept: only its .RG1 worlds are decoded) and each sector of each world becomes a piece,
    its submeshes' texture names being looked up in the dictionary (a name it doesn't have throws). The instance holds one
    user of the texture archive until destroyed. If a step fails, what this call loaded is removed again.
    */
    PoolId addONELevelInstance(PoolId shaderId_, const STH2005::LevelKey& rLevel_, const std::vector<std::string>& rSkippedEntries_)
    {
        bool archiveCreated = false;
        PoolId archiveId = findOrLoadONE(rLevel_.textures.archive, archiveCreated);
        oneArchives[archiveId].users++;
        ONELevelInstance level{shaderId_, archiveId, {}};
        try
        {
            uint32_t dictionaryIndex = oneArchives[archiveId].useTextureDictionary(rLevel_.textures.entry);
            for(const STH2005::ArchiveKey& rSection : rLevel_.sections)
            {
                STH2005::ONE section = STH2005::openONE(Globals::Discs::shadow.readFile(rSection.path));
                for(const STH2005::ONEEntry& rEntry : section.entries)
                {
                    if(rEntry.name.size() < 4 || !std::string_view(rEntry.name).ends_with(".RG1")) continue;
                    bool skipped = false;
                    for(const std::string& rPart : rSkippedEntries_)
                    {
                        if(rEntry.name.find(rPart) != std::string::npos) skipped = true;
                    }
                    if(skipped) continue;
                    Graphics::RenderWareWorld world = Renderware::readWorld(section.readFile(rEntry.name));

                    const Renderware::TxdTextures& rDictionary = oneArchives[archiveId].textureDictionaries[dictionaryIndex];

                    for(Graphics::RenderWareWorldSector& rSector : world.sectors)
                    {
                        ONELevelPiece piece;
                        piece.center = (rSector.boxMin + rSector.boxMax) * 0.5f;
                        piece.radius = glm::distance(rSector.boxMin, rSector.boxMax) * 0.5f;
                        for(Graphics::RenderWareSubmesh& rSubmesh : rSector.submeshes)
                        {
                            PoolId textureId = UninitializedPoolId;
                            if(!rSubmesh.textureName.empty())
                            {
                                auto texture = rDictionary.byName.find(rSubmesh.textureName);
                                if(texture == rDictionary.byName.end())
                                {
                                    throw std::runtime_error(std::format("ONELevelInstance: texture {} of {} in {} is not in {}.", rSubmesh.textureName, rEntry.name, rSection.path, rLevel_.textures.entry));
                                }
                                textureId = texture->second;
                            }
                            piece.submeshes.push_back(ONELevelSubmesh{std::move(rSubmesh.mesh), textureId});
                        }
                        level.pieces.push_back(std::move(piece));
                    }
                }
            }
        }
        catch(...)
        {
            level.destroy();
            throw;
        }
        return oneLevelInstances.add(std::move(level));
    }

    /*
    The instance of a glTF level, read from its folder (GLTFKeys.h): every <levelName>_*.chunk.model.json of Models/ (written by the editor's import, Loading/GLTFImport.h) is one chunk, each of its submeshes one GLTFLevelSubmesh with its bounding
    sphere; the PNGs the chunks name (also in Models/) are added to Graphics::textures as "<levelName> <file name>" (uploaded now, removed by GLTFLevelInstance::destroy). The GPU buffers of a submesh are made by the streaming
    when it comes near. Reads files only (no scan of the external assets), so it works in a build without EDITOR. Throws if a file can't be read or there is no chunk, after removing what was made.
    */
    PoolId addGLTFLevelInstance(PoolId shaderId_, const GLTFLevelKey& rLevel_)
    {
        namespace fs = std::filesystem;
        fs::path models = fs::path(rLevel_.folder()) / "Models";
        std::vector<fs::path> chunkFiles;
        const std::string prefix = rLevel_.levelName + "_";
        if(fs::is_directory(models))
        {
            for(const fs::directory_entry& rEntry : fs::directory_iterator(models))
            {
                std::string name = rEntry.path().filename().string();
                if(rEntry.is_regular_file() && name.starts_with(prefix) && name.ends_with(".chunk.model.json")) chunkFiles.push_back(rEntry.path());
            }
        }
        std::sort(chunkFiles.begin(), chunkFiles.end());
        if(chunkFiles.empty()) throw std::runtime_error(std::format("glTF level '{}': no chunk files ({}_*.chunk.model.json) in {}; import a glTF model first.", rLevel_.levelName, rLevel_.levelName, models.string()));

        GLTFLevelInstance level;
        level.shaderId = shaderId_;
        try
        {
            std::map<std::string, PoolId> textureOfName;
            for(const fs::path& rFile : chunkFiles)
            {
                External::ModelData model = External::readModel(rFile.string());
                if(model.layout.key() != VertexLayout::standard().key()) throw std::runtime_error(std::format("{}: the vertex layout is not the standard one.", rFile.string()));
                for(External::ModelSubmesh& rSource : model.submeshes)
                {
                    if(rSource.vertexCount == 0) continue;
                    GLTFLevelSubmesh submesh{};
                    submesh.textureId = UninitializedPoolId;
                    if(!rSource.texture.empty())
                    {
                        auto found = textureOfName.find(rSource.texture);
                        if(found == textureOfName.end())
                        {
                            PoolId id = textures.add(std::format("{} {}", rLevel_.levelName, rSource.texture).c_str(), External::readTexture((models / (rSource.texture + ".png")).string()));
                            level.textureIds.push_back(id);
                            textures[id].create();
                            found = textureOfName.emplace(rSource.texture, id).first;
                        }
                        submesh.textureId = found->second;
                    }
                    submesh.mesh.verticies.bytes = std::move(rSource.vertices);
                    submesh.mesh.indicies = std::move(rSource.indices);
                    const Graphics::Vertex* pVertices = submesh.mesh.verticies.begin();
                    size_t count = submesh.mesh.verticies.size();
                    glm::vec3 minimum = pVertices[0].position, maximum = pVertices[0].position;
                    for(size_t i = 0; i < count; i++)
                    {
                        minimum = glm::min(minimum, pVertices[i].position);
                        maximum = glm::max(maximum, pVertices[i].position);
                    }
                    submesh.center = (minimum + maximum) * 0.5f;
                    submesh.radius = glm::distance(minimum, maximum) * 0.5f;
                    level.submeshes.push_back(std::move(submesh));
                }
            }
        }
        catch(...)
        {
            level.destroy();
            throw;
        }
        return gltfLevelInstances.add(std::move(level));
    }

    /* The collision of a glTF level: the triangles of the collision files the key names (Collision/<name>.collision.json, CollisionFile.h) in one Collision, every piece solid (flags 0 is not listed in the SADXLevel remap, whose fallback is Solid). */
    PoolId addGLTFCollision(const GLTFLevelKey& rLevel_)
    {
        Physics::Collision collision;
        for(const std::string& rName : rLevel_.collisions) collision.mesh.append(readGLTFCollisionFile(rLevel_, rName));
        collision.computeBounds();
        Globals::Game::baBehaviors.resolve(collision, Globals::Game::baBehaviors.remapSADXLevel);
        return Globals::Physics::collisions.add(std::move(collision));
    }

    /* Adds one collision file to a glTF level's live collision: the triangles are appended, the bounds are made again (the collision keeps its position) and the new pieces' flags are given their behaviours. The caller rebuilds the quad tree. */
    void appendGLTFCollision(PoolId collisionId_, const GLTFLevelKey& rLevel_, const std::string& rName_)
    {
        Physics::Collision& rCollision = Globals::Physics::collisions[collisionId_];
        rCollision.mesh.append(readGLTFCollisionFile(rLevel_, rName_));
        glm::vec3 position = rCollision.position;
        rCollision.position = glm::vec3(0.0f);
        rCollision.computeBounds();
        rCollision.setPosition(position);
        Globals::Game::baBehaviors.resolve(rCollision, Globals::Game::baBehaviors.remapSADXLevel);
    }

    /* One collision file of a glTF level. */
    Physics::CollisionMesh readGLTFCollisionFile(const GLTFLevelKey& rLevel_, const std::string& rName_)
    {
        return External::readCollision(rLevel_.folder() + "/Collision/" + rName_ + ".collision.json");
    }

    /*
    The collision of an SADX land table: every COL's model is imported as geometry only (appendSACollision), including the
    COLs without LandTableVisible (hypothesis: those are the collision-only pieces), each as a piece with its surface flags.
    Nothing is uploaded to the GPU and no mesh or instance is created; the .rel module is freed again unless a mesh or motion is using it. The caller owns the id.
    */
    PoolId addSACollision(const SALandTableKey& rLandTable_)
    {
        const std::vector<uint8_t>& rBytes = findOrLoadRelModule(rLandTable_.rel)->second.bytes;
        Physics::Collision collision;
        try
        {
            Ninja::SALandTable landTable = Ninja::readSALandTable(rBytes, rLandTable_.offset);
            for(const Ninja::SALandTableCol& rCol : landTable.cols)
            {
                Ninja::appendSACollision(collision.mesh, rBytes, rCol.modelOffset, rCol.flags, rCol.center, rCol.radius, true);
            }
        }
        catch(...)
        {
            eraseRelModuleIfUnused(rLandTable_.rel);
            throw;
        }
        eraseRelModuleIfUnused(rLandTable_.rel);
        collision.computeBounds();
        Globals::Game::baBehaviors.resolve(collision, Globals::Game::baBehaviors.remapSADXLevel);
        return Globals::Physics::collisions.add(std::move(collision));
    }

    /*
    A Collision made of the shapes of an SADX collision table (readSACollisionShapes), with no mesh. The relocated .rel is freed again
    unless a mesh or motion is using it. The caller owns the id.
    */
    /* Same for several tables (the collisions of one stage object): the shapes of all of them, in order, in one Collision. */
    PoolId addSAShapes(const std::vector<SACollisionShapesKey>& rShapes_)
    {
        Physics::Collision collision;
        for(const SACollisionShapesKey& rKey : rShapes_)
        {
            try
            {
                std::vector<Physics::CollisionShape> shapes = Ninja::readSACollisionShapes(findOrLoadRelModule(rKey.rel)->second.bytes, rKey.offset, rKey.count);
                collision.shapes.insert(collision.shapes.end(), shapes.begin(), shapes.end());
                collision.name += std::format("{}{}@0x{:X}", collision.name.empty() ? "" : ", ", rKey.rel, rKey.offset);
            }
            catch(...)
            {
                eraseRelModuleIfUnused(rKey.rel);
                throw;
            }
            eraseRelModuleIfUnused(rKey.rel);
        }
        Globals::Game::baBehaviors.resolve(collision, Globals::Game::baBehaviors.remapSADXShape);
        return Globals::Physics::collisions.add(std::move(collision));
    }

    PoolId addSAShapes(const SACollisionShapesKey& rShapes_)
    {
        Physics::Collision collision;
        try
        {
            collision.shapes = Ninja::readSACollisionShapes(findOrLoadRelModule(rShapes_.rel)->second.bytes, rShapes_.offset, rShapes_.count);
            collision.name = std::format("{}@0x{:X}", rShapes_.rel, rShapes_.offset);
        }
        catch(...)
        {
            eraseRelModuleIfUnused(rShapes_.rel);
            throw;
        }
        eraseRelModuleIfUnused(rShapes_.rel);
        Globals::Game::baBehaviors.resolve(collision, Globals::Game::baBehaviors.remapSADXShape);
        return Globals::Physics::collisions.add(std::move(collision));
    }

    /*
    A Collision made of the shapes of a Shadow .CCL entry (readCCL), with no mesh. The archive is read from the disc and not pooled
    (like addONECollision's section archives). The caller owns the id.
    */
    PoolId addONEShapes(const STH2005::CollisionKey& rShapes_)
    {
        STH2005::ONE archive = STH2005::openONE(Globals::Discs::shadow.readFile(rShapes_.archive.path));
        Physics::Collision collision;
        collision.shapes = STH2005::readCCL(archive.readFile(rShapes_.entry));
        collision.name = rShapes_.entry;
        Globals::Game::baBehaviors.resolve(collision, Globals::Game::baBehaviors.remapShadowShape);
        return Globals::Physics::collisions.add(std::move(collision));
    }

    /*
    Same for several .CCL entries (the collisions of one stage object): the shapes of all of them, in order, in one Collision.
    */
    PoolId addONEShapes(const std::vector<STH2005::CollisionKey>& rShapes_)
    {
        Physics::Collision collision;
        for(const STH2005::CollisionKey& rKey : rShapes_)
        {
            STH2005::ONE archive = STH2005::openONE(Globals::Discs::shadow.readFile(rKey.archive.path));
            std::vector<Physics::CollisionShape> shapes = STH2005::readCCL(archive.readFile(rKey.entry));
            collision.shapes.insert(collision.shapes.end(), shapes.begin(), shapes.end());
            collision.name += std::format("{}{}", collision.name.empty() ? "" : ", ", rKey.entry);
        }
        Globals::Game::baBehaviors.resolve(collision, Globals::Game::baBehaviors.remapShadowShape);
        return Globals::Physics::collisions.add(std::move(collision));
    }

    /*
    The collision of a Shadow stage: every .BSP entry whose name contains "COLI" (hypothesis: the collision worlds, see
    readCollisionWorld) in each section archive of the key, appended into one mesh, rotated by the renderer's RenderWareYawDegrees. The key's textures are not used.
    */
    PoolId addONECollision(const STH2005::LevelKey& rLevel_)
    {
        /* Same yaw the renderer gives RenderWare data (RenderWareYawDegrees, a hypothesis), so the collision lines up with the drawn level and entities. */
        glm::mat4 yaw = glm::rotate(glm::mat4(1.0f), glm::radians(RenderWareYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
        Physics::Collision collision;
        for(const STH2005::ArchiveKey& rSection : rLevel_.sections)
        {
            STH2005::ONE section = STH2005::openONE(Globals::Discs::shadow.readFile(rSection.path));
            for(const STH2005::ONEEntry& rEntry : section.entries)
            {
                if(!std::string_view(rEntry.name).ends_with(".BSP") || rEntry.name.find("COLI") == std::string::npos) continue;
                Physics::CollisionMesh world = Renderware::readCollisionWorld(section.readFile(rEntry.name));

                for(glm::vec3& rVertex : world.vertices) rVertex = glm::vec3(yaw * glm::vec4(rVertex, 1.0f));
                for(Physics::CollisionPiece& rPiece : world.pieces) rPiece.center = glm::vec3(yaw * glm::vec4(rPiece.center, 1.0f));
                collision.mesh.append(world);
            }
        }
        collision.computeBounds();
        Globals::Game::baBehaviors.resolve(collision, Globals::Game::baBehaviors.remapShadowLevel);
        return Globals::Physics::collisions.add(std::move(collision));
    }

    /*
    Same for a Shadow model: the archive, the mesh (built from the key's recipe) and the .TXD entry are found or
    loaded, then the instance holds one user of the archive until destroyed. pTextures_ nullptr = untextured; its archive can be another than the mesh's (see below).
    */
    PoolId addONEInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_, const STH2005::TextureKey* pTextures_)
    {
        bool created = false;
        PoolId archiveId = findOrLoadONE(rMesh_.archive, created);
        uint32_t meshIndex;
        uint32_t dictionaryIndex = STH2005::NoIndex;
        uint32_t bindingIndex = STH2005::NoIndex;
        try
        {
            STH2005::ONE& rArchive = oneArchives[archiveId];
            meshIndex = rArchive.useMesh(rMesh_);
            if(pTextures_ != nullptr)
            {
                /*
                A .TXD of another archive than the mesh's is decoded into the mesh's archive, named "<archive>|<entry>" (stage objects can need a texture only another stage's archive has,
                see EntityKey; whether the game lets a model use another archive's textures is unknown).
                */
                if(std::string_view(pTextures_->archive.path) == rMesh_.archive.path)
                {
                    dictionaryIndex = rArchive.useTextureDictionary(pTextures_->entry);
                }
                else
                {
                    std::string name = std::format("{}|{}", pTextures_->archive.path, pTextures_->entry);
                    auto known = rArchive.textureDictionaryByName.find(name);
                    if(known != rArchive.textureDictionaryByName.end())
                    {
                        dictionaryIndex = known->second;
                    }
                    else
                    {
                        STH2005::ONE source = STH2005::openONE(Globals::Discs::shadow.readFile(pTextures_->archive.path));
                        dictionaryIndex = rArchive.addTextureDictionary(name, source.readFile(pTextures_->entry));
                    }
                }
                bindingIndex = rArchive.useBinding(meshIndex, dictionaryIndex);
            }
        }
        catch(...)
        {
            if(created) oneArchives.remove(archiveId);
            throw;
        }
        oneArchives[archiveId].users++;
        return oneInstances.add(ONEInstance{archiveId, shaderId_, meshIndex, dictionaryIndex, bindingIndex});
    }
}
