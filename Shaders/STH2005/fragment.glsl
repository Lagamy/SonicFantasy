#version 450 core

in vec2 vertexUv;
in vec4 vertexColor;

layout(binding = 0) uniform sampler2D baseTexture;
layout(location = 1) uniform bool hasTexture;

/* Multiplies the vertex color and alpha: an effect's tint and fade (Graphics::EffectBalls); white and opaque (no change) for everything else. */
layout(location = 4) uniform vec4 effectTint = vec4(1.0);

out vec4 FragColor;

/*
The texture is uploaded as sRGB, so sampling it gives linear values, while the vertex color (a DFF material color baked
into the vertices, white for SA models) is a plain 0-1 value. Converting it to linear before multiplying and letting the sRGB framebuffer encode the result
gives about the same displayed color as multiplying the raw values, which is what the original does (hypothesis: the
GameCube blends in that space).
*/
vec3 srgbToLinear(vec3 c)
{
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void main()
{
	vec4 color = vec4(srgbToLinear(vertexColor.rgb * effectTint.rgb), vertexColor.a * effectTint.a);
	if(hasTexture)
	{
		color *= texture(baseTexture, vertexUv);
	}
	/* A fully transparent texel is dropped, so it doesn't write depth and hide what is behind it. */
	if(color.a < 0.01) discard;
	FragColor = color;
}
