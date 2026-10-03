#pragma once
#include "PoolId.h"
#include "SAKeys.h"
#include "STH2005Keys.h"

namespace Loading
{
    PoolId addSAInstance(PoolId shaderId_, const SAModelKey& rMesh_, const SATextureSetKey* pTextures_);
    PoolId addONEInstance(PoolId shaderId_, const STH2005::ModelKey& rMesh_, const STH2005::TextureKey* pTextures_);
    void releaseSAMesh(PoolId saMeshId_);
    void releaseTextureSet(PoolId textureSetId_);
    void releaseONE(PoolId archiveId_);
    void playSAMotion(PoolId saInstanceId_, const SAMotionKey& rKey_);
    void releaseSAMotion(PoolId saMotionId_);
}
