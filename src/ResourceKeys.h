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

/*
The players' keys (Sonic's model, texture and motions on the SADX disc; Shadow's parts, texture and motions in character/shadow.one) are not here: they are read from Assets/External/Players/Sonic.json and
Shadow.json (Loading::loadPlayerKeys, PlayerLoader.h).
*/

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
Its level file is Levels/EmeraldCoast/EmeraldCoast.json. Its SET file is Set0100S.bin (Sonic's, the stage's first act; the letter picks the character, hypothesis) and SAEntityTable_EmeraldCoast (generated, SAEntityKeys.h) makes
Level::setSAEntities create the objects it places.
*/

/*
Emerald Coast's floating bridge: the four things the stage's own code creates it from (no SET record). Only the disc offsets are here: the models of _Stg01.rel (the Forward dock, the Backward dock, a pier, the links between piers) and the shape tables the docks
and piers give the collision init (0x24A4D0, 0x24A590, 0x24A650); the meshes the game also collides with ("Floating Dock Collision", "Floating Pier Collision") are not used. Where the 51 entities stand is the level file's "source" (Levels/EmeraldCoast/EmeraldCoast.json,
see docs/EMERALD_COAST_OBJECTS.md); they are registered as prefabs by the stage-created list of LevelLoader.cpp. Not reproduced: the waves and the player's weight that move the piers and tilt them (see the docs).
*/
inline const SAEntityKey EmeraldCoastBridgeDockForward{"Floating Dock (Forward)", {"_Stg01.rel", 0x1C8408}, {"obj_beach.GVM"}, {}, {{"_Stg01.rel", 0x24A4D0, 4}}};
inline const SAEntityKey EmeraldCoastBridgeDockBackward{"Floating Dock (Backward)", {"_Stg01.rel", 0x1CACEC}, {"obj_beach.GVM"}, {}, {{"_Stg01.rel", 0x24A590, 4}}};
inline const SAEntityKey EmeraldCoastBridgePier{"Floating Pier", {"_Stg01.rel", 0x1CB9C0}, {"obj_beach.GVM"}, {}, {{"_Stg01.rel", 0x24A650, 2}}};
inline const SAEntityKey EmeraldCoastBridgeLinks{"Floating Pier Links", {"_Stg01.rel", 0x1CBB40}, {"obj_beach.GVM"}};


/*
Shadow's jump effect balls (docs/STH2005_PLAYER_REFERENCE.md, "Jump_ballA / Jump_ballB decoded"): the DFFs of common/CommonEffect.one (a "One Ver 0.50" archive, STH2005::openONE), each a static model of one atomic with its UV
animation in the .UVA of the same name, and the archive's one texture dictionary.
*/
inline const STH2005::ArchiveKey CommonEffectArchive{"common/CommonEffect.one"};
inline const STH2005::ModelKey EffectBallAMesh{CommonEffectArchive, "EFFCHRBALLA.DFF", {{"EFFCHRBALLA.DFF", nullptr, nullptr}}};
inline const STH2005::ModelKey EffectBallBMesh{CommonEffectArchive, "EFFCHRBALLB.DFF", {{"EFFCHRBALLB.DFF", nullptr, nullptr}}};
inline const STH2005::TextureKey CommonEffectTextures{CommonEffectArchive, "COMMONEFFECT.TXD"};

/*
Shadow stages as levels (observed on the disc): stgNNNN/stgNNNN_01.one ... _NN.one hold the stage's worlds (.RG1, visual) and
collision (.BSP), and stgNNNN_texCM.one has one .TXD with every texture those worlds use. Which stage of the game each id is
is not known (the ResourceMapByCategory.txt grouping assumes no names). stg0000 has one section (Levels/Stg0000/Stg0000.json, no entity table), stg0100 has 23.
stg0100 skips its "_D_" and "_G_" worlds (entry names like STG0100_D_23.RG1): in every section that has them they are 72 to 240
triangles with bounding spheres of radius 6800 to 15700, against about 3700 for the largest of the other worlds, and look like
sky domes (hypothesis; what the letters stand for is unknown).
Its level file is Levels/Stg0100/Stg0100.json. Its placement files (all four; whether the nrm, hrd and ds1 ones are alternatives of different missions, hypothesis from their flags, is not handled: every record is placed) and EntityTable_stg0100
(generated, STH2005EntityKeys.h) make Level::setONEEntities create the objects the stage places.
*/
