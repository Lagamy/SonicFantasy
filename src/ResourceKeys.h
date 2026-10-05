#pragma once
#include "SAKeys.h"
#include "STH2005Keys.h"
#include "SAShapeKeys.h"
#include "STH2005ShapeKeys.h"
#include "STH2005EntityKeys.h"
#include "SAEntityKeys.h"

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
Sonic's other motions in ModelsMiles.rel, all 73 nodes like his model (external, SA Tools GameConfig/GC_SADX/ModelsMiles.ini, section names in quotes). Which one the game plays for which
speed and move is the table at _Main.rel 0x3544E0 (16 bytes per animation id, a pointer to the motion first; external, Ghidra pseudocode of sadxDecomp): id 0 Stand, 9 Walk, 10 Speedwalk, 11 Jog, 12 Run,
13 Sprint, 0x12 Fall, 0x13 Landing, 0x11 Begin Falling From Spring Launch, 0x14 Brake, 0x17 Damage, 0x1E Turn Around. "Spring Launch" (0xE, 74 nodes) is the jump, below, with its own model.
*/
/*
Sonic's other models and the motions made for them (external, the table at _Main.rel 0x3544E0 pairs each animation id with a model and a motion; SA Tools ModelsMiles.ini): the jump (id 0xE, "Spring Launch",
74 nodes) is drawn with the curled model, the spin ball (id 0x20, "Spin (Jumpball)", 1 node) with the jumpball model; all the others use SonicMesh.
*/
inline const SAModelKey SonicCurledMesh{"ModelsMiles.rel", 0x17671C};
inline const SAModelKey SonicJumpballMesh{"ModelsMiles.rel", 0x17F900};
inline const SAMotionKey SonicJumpMotion{"ModelsMiles.rel", 0x1DA47C, 74};
inline const SAMotionKey SonicBallMotion{"ModelsMiles.rel", 0x1F8BB8, 1};
inline const SAMotionKey SonicStand{"ModelsMiles.rel", 0x1C3F30, 73};
inline const SAMotionKey SonicWalk{"ModelsMiles.rel", 0x1CCEE8, 73};
inline const SAMotionKey SonicSpeedwalk{"ModelsMiles.rel", 0x1D07EC, 73};
inline const SAMotionKey SonicJog{"ModelsMiles.rel", 0x1D4020, 73};
inline const SAMotionKey SonicRun{"ModelsMiles.rel", 0x1D72B4, 73};
inline const SAMotionKey SonicSprint{"ModelsMiles.rel", 0x1E8AD8, 73};
inline const SAMotionKey SonicBrake{"ModelsMiles.rel", 0x1D9F58, 73};
inline const SAMotionKey SonicTurnAround{"ModelsMiles.rel", 0x1F7190, 73};
inline const SAMotionKey SonicSpringFall{"ModelsMiles.rel", 0x1DFCD4, 73};
inline const SAMotionKey SonicFall{"ModelsMiles.rel", 0x1E3218, 73};
inline const SAMotionKey SonicLanding{"ModelsMiles.rel", 0x1E5F24, 73};
inline const SAMotionKey SonicDamage{"ModelsMiles.rel", 0x1FB99C, 73};

/*
SADX object collision tables (SACollisionShapesKey, one per table the game's collision init is given by a literal address, from the Ghidra
pseudocode of sadxDecomp): generated into SAShapeKeys.h (included above) by tools/genSAShapeKeys.py, named after module and address since the objects are
unnamed. Entities without a table are given hand-made shapes instead.
*/

/*
Emerald Coast, first land table of _Stg01.rel (external, SA Tools GameConfig/GC_SADX/_Stg01.ini, landtable0100). The names
of its texlist are all in BEACH01.GVM (observed: 103 of 103); the other two land tables of the module (0x1BA554, 0x1BC174) match
beach02.GVM (90 of 90) and BEACH03.GVM (89 of 89).
m8_shadows is a baked shadow decal (8x8, translucent in the file); its pieces hold nothing else (observed: 6 pieces here, 1 in each
of the other two tables), and it is skipped since shadows are to be real time.
Its SET file is Set0100S.bin (Sonic's, the stage's first act; the letter picks the character, hypothesis) and SAEntityTable_EmeraldCoast (generated, SAEntityKeys.h) makes
Level::setSAEntities create the objects it places.
*/

/*
Emerald Coast's floating bridge, in the pose the game draws it from afar (external, the machine code of _Stg01.rel 0x17BBC / 0x17D4C and the task creator 0x18218, see docs/EMERALD_COAST_OBJECTS.md): the stage's
own code creates it (no SET record) at (2803, -1, 365) (the constants at _Stg01.rel 0x203B0 to 0x203B8) along +x: the Forward dock at that point, then, from 75 along and 9.5 up, 25 piers 40 apart (the first 20
further) with a link between each two (24), and the Backward dock 95 after the last pier (1150 from the start). The collisions are the shape tables the docks and piers give the collision init (0x24A4D0, 0x24A590,
0x24A650); the meshes the game also collides with ("Floating Dock Collision", "Floating Pier Collision") are not used. Not reproduced: the waves and the player's weight that move the piers and tilt them (see the docs).
*/
inline const SAEntityKey EmeraldCoastBridgeDockForward{"Floating Dock (Forward)", {"_Stg01.rel", 0x1C8408}, {"obj_beach.GVM"}, {}, {{"_Stg01.rel", 0x24A4D0, 4}}};
inline const SAEntityKey EmeraldCoastBridgeDockBackward{"Floating Dock (Backward)", {"_Stg01.rel", 0x1CACEC}, {"obj_beach.GVM"}, {}, {{"_Stg01.rel", 0x24A590, 4}}};
inline const SAEntityKey EmeraldCoastBridgePier{"Floating Pier", {"_Stg01.rel", 0x1CB9C0}, {"obj_beach.GVM"}, {}, {{"_Stg01.rel", 0x24A650, 2}}};
inline const SAEntityKey EmeraldCoastBridgeLinks{"Floating Pier Links", {"_Stg01.rel", 0x1CBB40}, {"obj_beach.GVM"}};
inline const std::vector<SAStageEntityKey> EmeraldCoastStageEntities = []
{
	constexpr float X = 2803.0f;
	constexpr float Y = -1.0f;
	constexpr float Z = 365.0f;
	std::vector<SAStageEntityKey> entities;
	entities.push_back({&EmeraldCoastBridgeDockForward, {X, Y, Z}});
	for(int i = 0; i < 25; i++) entities.push_back({&EmeraldCoastBridgePier, {X + 95.0f + 40.0f * i, Y + 9.5f, Z}});
	for(int i = 0; i < 24; i++) entities.push_back({&EmeraldCoastBridgeLinks, {X + 115.0f + 40.0f * i, Y + 9.5f, Z}});
	entities.push_back({&EmeraldCoastBridgeDockBackward, {X + 1150.0f, Y + 9.5f, Z}});
	return entities;
}();

inline const SALandTableKey EmeraldCoastLandTable{"_Stg01.rel", 0x1B8D68, {"m8_shadows"}, {"Set0100S.bin"}, SAEntityTable_EmeraldCoast, &SAObjectTypeNames_EmeraldCoast, EmeraldCoastStageEntities};
inline const SATextureSetKey EmeraldCoastTextures{"BEACH01.GVM"};

/*
Shadow the Hedgehog. The parts are named so that ShadowMesh is built from them and Entity::switchMeshPart can swap one
for another (a part without a .BON of its own needs the bone to attach to).
*/
inline const STH2005::ArchiveKey ShadowArchive{"character/shadow.one"};
inline const STH2005::ModelPart ShadowBodyPart{"SHADOW_BODY.DFF", "SH.BON"};
inline const STH2005::ModelPart ShadowHandLPart{"SHADOW_HAND_L.DFF", "LTE.BON"};
inline const STH2005::ModelPart ShadowHandRPart{"SHADOW_HAND_R.DFF", "RTE.BON"};
inline const STH2005::ModelPart ShadowEyeLPart{"SHADOW_EYE_L.DFF", nullptr, "sh_j_head"};
inline const STH2005::ModelPart ShadowEyeRPart{"SHADOW_EYE_R.DFF", nullptr, "sh_j_head"};
inline const STH2005::ModelPart ShadowMabuta01Part{"SHADOW_MABUTA01.DFF", nullptr, "sh_j_head"};
inline const STH2005::ModelKey ShadowMesh{ShadowArchive, "SHADOW_BODY.DFF", {
	ShadowBodyPart, ShadowHandLPart, ShadowHandRPart, ShadowEyeLPart, ShadowEyeRPart, ShadowMabuta01Part
}};
inline const STH2005::TextureKey ShadowTextures{ShadowArchive, "SHADOW.TXD"};
/* "sh_idle" of SHADOW.MTP for the SH.BON skeleton (observed: 100 frames). */
inline const STH2005::MotionKey ShadowIdle{ShadowArchive, "SHADOW.MTP", "sh_idle", "SH.BON"};
/* The hands' fingers, for LTE.BON / RTE.BON (observed: 100 frames, the first node is the wrist sh_j_l1te / sh_j_r1te). */
inline const STH2005::MotionKey ShadowIdleLeftHand{ShadowArchive, "SHADOW.MTP", "lte_idle", "LTE.BON", true};
inline const STH2005::MotionKey ShadowIdleRightHand{ShadowArchive, "SHADOW.MTP", "rte_idle", "RTE.BON", true};
/*
The motions the player script (Scripts/shadow/player.lua) picks, for SH.BON. Which motion the game plays in which state is not decoded: the names of SHADOW.MTP's table
(158 entries) are the only evidence (hypothesis): sh_walk, sh_run, sh_jump_a, sh_jump_b, sh_jump_c (a jump: observed on the disc, each one's last pose is the next one's first, and sh_jump_c turns the root by -6.98 radians, a full somersault curled up; then sh_jump_idle, whose first pose is close to sh_jump_c's last), sh_break_a (a brake), sh_dam_m_a (damage).
*/
inline const STH2005::MotionKey ShadowWalk{ShadowArchive, "SHADOW.MTP", "sh_walk", "SH.BON"};
inline const STH2005::MotionKey ShadowRun{ShadowArchive, "SHADOW.MTP", "sh_run", "SH.BON"};
inline const STH2005::MotionKey ShadowJump{ShadowArchive, "SHADOW.MTP", "sh_jump_a", "SH.BON"};
inline const STH2005::MotionKey ShadowJumpB{ShadowArchive, "SHADOW.MTP", "sh_jump_b", "SH.BON"};
inline const STH2005::MotionKey ShadowJumpC{ShadowArchive, "SHADOW.MTP", "sh_jump_c", "SH.BON"};
inline const STH2005::MotionKey ShadowJumpFA{ShadowArchive, "SHADOW.MTP", "sh_jump_fa", "SH.BON"};
inline const STH2005::MotionKey ShadowJumpFB{ShadowArchive, "SHADOW.MTP", "sh_jump_fb", "SH.BON"};
inline const STH2005::MotionKey ShadowJumpD{ShadowArchive, "SHADOW.MTP", "sh_jump_d", "SH.BON"};
inline const STH2005::MotionKey ShadowJumpE{ShadowArchive, "SHADOW.MTP", "sh_jump_e", "SH.BON"};
/* The jump dash's motion (observed in the live log: animation index 6, motion number 83 = sh_fw_jump, range 0 to 15, looping while the dash lasts). */
inline const STH2005::MotionKey ShadowFwJump{ShadowArchive, "SHADOW.MTP", "sh_fw_jump", "SH.BON"};
inline const STH2005::MotionKey ShadowFall{ShadowArchive, "SHADOW.MTP", "sh_jump_idle", "SH.BON"};
inline const STH2005::MotionKey ShadowBrake{ShadowArchive, "SHADOW.MTP", "sh_break_a", "SH.BON"};
inline const STH2005::MotionKey ShadowDamage{ShadowArchive, "SHADOW.MTP", "sh_dam_m_a", "SH.BON"};
inline const STH2005::MotionKey ShadowSlowSkate{ShadowArchive, "SHADOW.MTP", "sh_slow_skate", "SH.BON"};
inline const STH2005::MotionKey ShadowHighSkate{ShadowArchive, "SHADOW.MTP", "sh_high_skate", "SH.BON"};

/*
Shadow stages as levels (observed on the disc): stgNNNN/stgNNNN_01.one ... _NN.one hold the stage's worlds (.RG1, visual) and
collision (.BSP), and stgNNNN_texCM.one has one .TXD with every texture those worlds use. Which stage of the game each id is
is not known (the ResourceMapByCategory.txt grouping assumes no names). stg0000 has one section, stg0100 has 23.
stg0100 skips its "_D_" and "_G_" worlds (entry names like STG0100_D_23.RG1): in every section that has them they are 72 to 240
triangles with bounding spheres of radius 6800 to 15700, against about 3700 for the largest of the other worlds, and look like
sky domes (hypothesis; what the letters stand for is unknown).
Its placement files (all four; whether the nrm, hrd and ds1 ones are alternatives of different missions, hypothesis from their flags, is not handled: every record is placed) and EntityTable_stg0100
(generated, STH2005EntityKeys.h) make Level::setONEEntities create the objects the stage places.
*/
inline const STH2005::LevelKey Stg0000Level{
	{
	{"stg0000/stg0000_01.one"}
	},
	{{"stg0000/stg0000_texCM.one"}, "STG0000_TEXCM.TXD"}
};
inline const STH2005::LevelKey Stg0100Level{
	{
	{"stg0100/stg0100_01.one"},
	{"stg0100/stg0100_02.one"},
	{"stg0100/stg0100_03.one"},
	{"stg0100/stg0100_04.one"},
	{"stg0100/stg0100_05.one"},
	{"stg0100/stg0100_06.one"},
	{"stg0100/stg0100_07.one"},
	{"stg0100/stg0100_08.one"},
	{"stg0100/stg0100_09.one"},
	{"stg0100/stg0100_10.one"},
	{"stg0100/stg0100_11.one"},
	{"stg0100/stg0100_12.one"},
	{"stg0100/stg0100_13.one"},
	{"stg0100/stg0100_14.one"},
	{"stg0100/stg0100_15.one"},
	{"stg0100/stg0100_16.one"},
	{"stg0100/stg0100_17.one"},
	{"stg0100/stg0100_18.one"},
	{"stg0100/stg0100_19.one"},
	{"stg0100/stg0100_20.one"},
	{"stg0100/stg0100_21.one"},
	{"stg0100/stg0100_22.one"},
	{"stg0100/stg0100_23.one"}
	},
	{{"stg0100/stg0100_texCM.one"}, "STG0100_TEXCM.TXD"},
	{"_D_", "_G_"},
	{"stg0100/stg0100_cmn.dat", "stg0100/stg0100_nrm.dat", "stg0100/stg0100_hrd.dat", "stg0100/stg0100_ds1.dat"},
	EntityTable_stg0100,
	&ObjectTypeNames
};
