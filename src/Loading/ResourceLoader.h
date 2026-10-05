#pragma once
#include "PoolId.h"
#include "SAKeys.h"
#include "STH2005Keys.h"
#include <vector>

namespace Loading
{
    PoolId addSAInstance(PoolId shaderId_, const SAModelKey& rMesh_, const SATextureSetKey* pTextures_);
    PoolId addSALevelInstance(PoolId shaderId_, const SALandTableKey& rLandTable_, const SATextureSetKey& rTextures_);
    PoolId addONELevelInstance(PoolId shaderId_, const STH2005::LevelKey& rLevel_);
    PoolId addONEInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_, const STH2005::TextureKey* pTextures_);
    PoolId addSACollision(const SALandTableKey& rLandTable_);
    PoolId addSAShapes(const SACollisionShapesKey& rShapes_);
    PoolId addSAShapes(const std::vector<SACollisionShapesKey>& rShapes_);
    PoolId addONEShapes(const STH2005::CollisionKey& rShapes_);
    PoolId addONEShapes(const std::vector<STH2005::CollisionKey>& rShapes_);
    PoolId addONECollision(const STH2005::LevelKey& rLevel_);
    void releaseSAMesh(PoolId saMeshId_);
    void releaseTextureSet(PoolId textureSetId_);
    void releaseONE(PoolId archiveId_);
    void playSAMotion(PoolId saInstanceId_, const SAMotionKey& rKey_);
    void changeSAMesh(PoolId saInstanceId_, const SAModelKey& rMesh_);
    void releaseSAMotion(PoolId saMotionId_);
}
