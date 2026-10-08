#version 450 core

in vec2 vertexUv;
in vec4 vertexColor;

layout(binding = 0) uniform sampler2D baseTexture;
layout(location = 1) uniform bool hasTexture;

out vec4 FragColor;

/*
For levels read from glTF files (GLTFLevelInstance). Unlike the SA and STH2005 shaders, the vertex color is not converted: glTF's COLOR_0 and baseColorFactor are already linear, and the texture is uploaded as sRGB
so sampling it gives linear values too; the sRGB framebuffer encodes the product. No lighting, the color is the material's.
*/
void main()
{
	vec4 color = vertexColor;
	if(hasTexture)
	{
		color *= texture(baseTexture, vertexUv);
	}
	/* A fully transparent texel is dropped, so it doesn't write depth and hide what is behind it. */
	if(color.a < 0.01) discard;
	FragColor = color;
}
