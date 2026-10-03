#pragma once
#include <cstdint>

/*
A Ninja Basic model of a GameCube .rel module on the SADX disc: the module's path and the file offset of the model's
root NJS_OBJECT (see importSAModel). Always big-endian. Imported the first time an instance needs it.
*/
struct SAModelKey
{
	const char* rel;
	uint32_t rootOffset;
};

/* A .gvm texture archive on the SADX disc, by its path. Imported the first time an instance needs it. */
struct SATextureSetKey
{
	const char* path;
};

/*
A Ninja motion (NJS_MOTION) of a GameCube .rel module on the SADX disc: the module's path, the motion's file offset and
the number of model nodes it animates (the file doesn't store it; SA Tools lists it). Imported the first time an
instance plays it; nodeCount must match the instance's model.
*/
struct SAMotionKey
{
	const char* rel;
	uint32_t offset;
	uint32_t nodeCount;
};
