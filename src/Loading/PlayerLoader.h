#pragma once
#include "SAKeys.h"
#include "STH2005Keys.h"
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

/*
The keys of the two players, read from files instead of being compiled in: where Sonic's model, texture and motions are on the SADX disc (Assets/External/Players/Sonic.json) and Shadow's parts, texture and motions
in character/shadow.one (Assets/External/Players/Shadow.json). The animation names the scripts use ("walk", "jumpC" ...) are the files' keys. The files are described in docs/ENGINE_USAGE.md.
*/
namespace Loading
{
    /* A motion of Sonic's by script name and the model it is drawn with (the body's, or the curled / jumpball one: pMesh points into SonicKeys::models). */
    struct SonicAnimation
    {
        const SAMotionKey* pMotion;
        const SAModelKey* pMesh;
    };

    struct SonicKeys
    {
        SATextureSetKey textures;
        std::map<std::string, SAModelKey> models;
        SAModelKey mesh;
        SAMotionKey idle;
        std::unordered_map<std::string, SonicAnimation> animations;
    };

    /* A motion of Shadow's by script name; loop false = it plays once and holds its last frame. */
    struct ShadowAnimation
    {
        const STH2005::MotionKey* pMotion;
        bool loop;
    };

    struct ShadowKeys
    {
        STH2005::ModelKey mesh;
        STH2005::TextureKey textures;
        /* Played when Shadow is made: the body's idle, then the two hands'. */
        std::vector<STH2005::MotionKey> idle;
        std::unordered_map<std::string, ShadowAnimation> animations;
    };

    /*
    Reads both files (Assets/External/Players/Sonic.json and Shadow.json). Once, before any player is made. Throws std::runtime_error if a file is missing or wrong, naming it.
    */
    void loadPlayerKeys();

    /* The keys; they live for the whole run. Throw std::logic_error before loadPlayerKeys. */
    const SonicKeys& sonicKeys();
    const ShadowKeys& shadowKeys();
}
