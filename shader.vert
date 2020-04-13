/* SPDX-License-Identifier: MIT */

#version 450

layout(location = 0) in vec2 position;

layout(push_constant) uniform block {
	mat3 mat;
};

void main() {
	vec3 pos = mat * vec3(position, 1.0);
	gl_Position = vec4(pos.xy, 0.0, 1.0);
}
