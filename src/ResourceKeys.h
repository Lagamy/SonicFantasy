#pragma once
#include "SAKeys.h"
#include "STH2005Keys.h"

/*
Where everything the game loads lives: disc paths, entry names and model recipes.
An instance is created with these keys (Entity::setSAGPUInstance, Entity::setONEGPUInstance); whatever is not
loaded yet is loaded then, and it is freed when its last instance is destroyed.
*/

/* SADX. Sonic's root NJS_OBJECT in ModelsMiles.rel (external, SA Tools GameConfig/GC_SADX/ModelsMiles.ini, section [Sonic]). */
inline const SAModelKey SonicMesh{"ModelsMiles.rel", 0x16ED5C};
inline const SATextureSetKey SonicTextures{"sonic.gvm"};
/* "Sonic Alternate Stand", 73 nodes (external, SA Tools GameConfig/GC_SADX/ModelsMiles.ini; observed: 16 frames, position and rotation keys, linear). */
inline const SAMotionKey SonicIdle{"ModelsMiles.rel", 0x2118B4, 73};

/*
Shadow the Hedgehog. The parts are named so that ShadowMesh is built from them and Entity::switchMeshPart can swap one
for another (a part without a .BON of its own needs the bone to attach to).
*/
inline const STH2005ArchiveKey ShadowArchive{"character/shadow.one"};
inline const STH2005ModelPart ShadowBodyPart{"SHADOW_BODY.DFF", "SH.BON"};
inline const STH2005ModelPart ShadowHandLPart{"SHADOW_HAND_L.DFF", "LTE.BON"};
inline const STH2005ModelPart ShadowHandRPart{"SHADOW_HAND_R.DFF", "RTE.BON"};
inline const STH2005ModelPart ShadowEyeLPart{"SHADOW_EYE_L.DFF", nullptr, "sh_j_head"};
inline const STH2005ModelPart ShadowEyeRPart{"SHADOW_EYE_R.DFF", nullptr, "sh_j_head"};
inline const STH2005ModelPart ShadowMabuta01Part{"SHADOW_MABUTA01.DFF", nullptr, "sh_j_head"};
inline const STH2005ModelKey ShadowMesh{ShadowArchive, "SHADOW_BODY.DFF", {
	ShadowBodyPart, ShadowHandLPart, ShadowHandRPart, ShadowEyeLPart, ShadowEyeRPart, ShadowMabuta01Part
}};
inline const STH2005TextureKey ShadowTextures{ShadowArchive, "SHADOW.TXD"};
/* "sh_idle" of SHADOW.MTP for the SH.BON skeleton (observed: 100 frames). */
inline const STH2005MotionKey ShadowIdle{ShadowArchive, "SHADOW.MTP", "sh_idle", "SH.BON"};
/* The hands' fingers, for LTE.BON / RTE.BON (observed: 100 frames, the first node is the wrist sh_j_l1te / sh_j_r1te). */
inline const STH2005MotionKey ShadowIdleLeftHand{ShadowArchive, "SHADOW.MTP", "lte_idle", "LTE.BON", true};
inline const STH2005MotionKey ShadowIdleRightHand{ShadowArchive, "SHADOW.MTP", "rte_idle", "RTE.BON", true};
