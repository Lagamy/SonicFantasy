#include "ResourceLoader.h"
#include "GPU_Globals.h"
#include "Globals.h"
#include "Rel.h"
#include "Gvm.h"
#include "SAMdl.h"
#include "SAMotion.h"
#include "STH2005ONE.h"
#include <algorithm>
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
            Importers::importGvm(rKey_.path, Globals::Discs::sadx.readFile(rKey_.path));
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
            module = relModules.emplace(rel_, RelModule{Importers::relocateRel(Globals::Discs::sadx.readFile(rel_)), 0}).first;
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
            PoolId meshId = Importers::importSAModel(name.c_str(), module->second.bytes, rKey_.rootOffset, true);
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
            PoolId motionId = saMotions.add(name.c_str(), Importers::readSAMotion(module->second.bytes, rKey_.offset, rKey_.nodeCount, true));
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
    through the set's file order. An index past the end of the set throws; a submesh without a texture (SANoTexture)
    gets UninitializedPoolId.
    */
    static PoolId findOrBindSATextures(PoolId textureSetId_, PoolId saMeshId_, const char* textureSetKey_)
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
                if(rSubmesh.textureIndex >= rSet.ordered.size())
                {
                    throw std::runtime_error(std::format("TextureBinding: submesh uses texture {} but set {} has {} textures.", rSubmesh.textureIndex, textureSetKey_, rSet.ordered.size()));
                }
                rIds.push_back(rSet.ordered[rSubmesh.textureIndex]);
            }
        }
        PoolId bindingId = textureBindings.add(std::move(binding));
        rSet.saBindings.emplace_back(saMeshId_, bindingId);
        return bindingId;
    }

    /*
    The archive at the key's path on the Shadow disc: the one already in the pool, or read and parsed now.
    created tells the caller whether it has to undo the load if a later step fails. It is not counted as used here.
    */
    static PoolId findOrLoadSTH2005ONE(const STH2005ArchiveKey& rKey_, bool& rCreated_)
    {
        auto found = sth2005Archives.nameToId.find(rKey_.path);
        if(found != sth2005Archives.nameToId.end())
        {
            rCreated_ = false;
            return found->second;
        }
        rCreated_ = true;
        return sth2005Archives.add(rKey_.path, Importers::openSTH2005ONE(Globals::Discs::shadow.readFile(rKey_.path)));
    }

    void releaseSTH2005ONE(PoolId archiveId_)
    {
        if(--sth2005Archives[archiveId_].users == 0)
        {
            sth2005Archives.remove(archiveId_);
        }
    }

    /*
    Everything the instance needs is found or loaded from its keys, then it holds one user of the mesh and of the
    texture set (if pTextures_ is not nullptr) until destroyed. If a step fails, what this call loaded is removed again.
    */
    PoolId addSAInstance(PoolId shaderId_, const SAModelKey& rMesh_, const SATextureSetKey* pTextures_)
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
                bindingId = findOrBindSATextures(setId, meshId, pTextures_->path);
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

    /*
    Same for a Shadow model: the archive, the mesh (built from the key's recipe) and the .TXD entry are found or
    loaded, then the instance holds one user of the archive until destroyed. pTextures_ nullptr = untextured, and its
    archive has to be the mesh's (whether the game can mix archives is unknown).
    */
    PoolId addONEInstance(PoolId shaderId_, const STH2005ModelKey& rMesh_, const STH2005TextureKey* pTextures_)
    {
        if(pTextures_ != nullptr && std::string_view(pTextures_->archive.path) != rMesh_.archive.path)
        {
            throw std::runtime_error(std::format("ONEInstance: textures from {} can't be used on a mesh of {}.", pTextures_->archive.path, rMesh_.archive.path));
        }

        bool created = false;
        PoolId archiveId = findOrLoadSTH2005ONE(rMesh_.archive, created);
        uint32_t meshIndex;
        uint32_t dictionaryIndex = Importers::STH2005NoIndex;
        uint32_t bindingIndex = Importers::STH2005NoIndex;
        try
        {
            Importers::STH2005ONE& rArchive = sth2005Archives[archiveId];
            meshIndex = rArchive.useMesh(rMesh_);
            if(pTextures_ != nullptr)
            {
                dictionaryIndex = rArchive.useTextureDictionary(pTextures_->entry);
                bindingIndex = rArchive.useBinding(meshIndex, dictionaryIndex);
            }
        }
        catch(...)
        {
            if(created) sth2005Archives.remove(archiveId);
            throw;
        }
        sth2005Archives[archiveId].users++;
        return oneInstances.add(ONEInstance{archiveId, shaderId_, meshIndex, dictionaryIndex, bindingIndex});
    }
}
