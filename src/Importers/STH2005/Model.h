#pragma once
#include <vector>
#include "PoolId.h"
#include "ONE.h"
#include "STH2005Keys.h"

namespace STH2005
{
/*
Builds one RenderWareMesh from several parts of a .ONE archive and adds it to that archive's meshes under name_
(returns its index in ONE::meshes).
parts_[0] is the main model (e.g. SHADOW_BODY.DFF + SH.BON). Without a .BON it is a static model, kept as the DFF has it, and it is then the only part. A later part with a .BON
(e.g. SHADOW_HAND_L.DFF + LTE.BON) is attached to the main model's bone that has the same name as the part's root bone;
one without (e.g. SHADOW_EYE_L.DFF) has its atomic's frame attached to the main model's bone named by bone.
*/
uint32_t importModel(const char* name_, ONE& rArchive_, const std::vector<ModelPart>& rParts_);
}
