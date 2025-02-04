#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
	mat4 model;
	mat4 view;
	mat4 projection;
	vec3 look;
	vec3 position;
	vec3 up;
    vec3 u;
    vec3 v;
    vec3 w;
    vec3 camconf;
    vec3 sizes;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in uint inTexInd;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint fragTexInd;

void main() {
    gl_Position = ubo.projection * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragColor = inColor;
	fragTexCoord = inTexCoord;
	fragTexInd = inTexInd;
}
