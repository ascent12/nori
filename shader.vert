/* SPDX-License-Identifier: MIT */

#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 tex_coord_in;
layout(location = 0) out vec2 tex_coord_out;

layout(set = 0, binding = 0) uniform block {
	mat3 mat;
};

void main() {
	tex_coord_out = tex_coord_in;

	vec3 pos = mat * vec3(position, 1.0);
	gl_Position = vec4(pos.xy, 0.0, 1.0);
}
