#version 450 core

layout (location = 0) in vec3 position;
layout (location = 2) in vec2 uv;
layout (location = 3) in vec4 color;
layout (location = 4) in uvec4 boneIndices;
layout (location = 5) in vec4 boneWeights;

out vec2 vertexUv;
out vec4 vertexColor;

layout(std140, binding = 0) uniform CameraData {
    mat4 view;
    mat4 projection;
};

/*
Skin matrices of the atomic being drawn (bone world matrix * inverse bind matrix, see ONEInstance::draw).
A skinned atomic's vertices are in its skin's bind space and are placed only by their weighted bones; an unskinned one
is placed by model, like in the SA shader.
*/
layout(std430, binding = 1) readonly buffer SkinBones {
    mat4 bones[];
};

uniform mat4 model;
layout(location = 2) uniform bool skinned;

/* Added to every vertex's UV: the scroll of an effect's UV animation (Graphics::EffectBalls); zero for everything else. */
layout(location = 3) uniform vec2 uvOffset = vec2(0.0);

void main() {
    vec4 local = vec4(position, 1.0);
    vec4 world;
    if(skinned)
    {
        world = vec4(0.0);
        for(int i = 0; i < 4; i++)
        {
            world += boneWeights[i] * (bones[boneIndices[i]] * local);
        }
    }
    else
    {
        world = model * local;
    }
    gl_Position = projection * view * world;
    vertexUv = uv + uvOffset;
    vertexColor = color;
}
