#pragma once
#include <vector>
#include "PoolId.h"
#include "STH2005ONE.h"
#include "STH2005Keys.h"

namespace Importers
{
/*
Builds one RenderWareMesh from several parts of a .ONE archive and adds it to that archive's meshes under name_
(returns its index in STH2005ONE::meshes).
parts_[0] is the main model (e.g. SHADOW_BODY.DFF + SH.BON) and needs a .BON. A later part with a .BON
(e.g. SHADOW_HAND_L.DFF + LTE.BON) is attached to the main model's bone that has the same name as the part's root bone;
one without (e.g. SHADOW_EYE_L.DFF) has its atomic's frame attached to the main model's bone named by bone.
*/
uint32_t importSTH2005Model(const char* name_, STH2005ONE& rArchive_, const std::vector<STH2005ModelPart>& rParts_);
}
