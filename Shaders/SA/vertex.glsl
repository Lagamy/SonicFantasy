#version 450 core

layout (location = 0) in vec3 position;
layout (location = 2) in vec2 uv;
layout (location = 3) in vec4 color;

out vec2 vertexUv;
out vec4 vertexColor;

layout(std140, binding = 0) uniform CameraData {
    mat4 view;
    mat4 projection;
};

uniform mat4 model;

void main() {
    gl_Position = projection * view * model * vec4(position, 1.0);
    vertexUv = uv;
    vertexColor = color;
}
