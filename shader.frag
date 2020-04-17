/* SPDX-License-Identifier: MIT */

#version 450

layout(location = 0) in vec2 tex_coord;

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform block {
	vec4 color;
};

layout(set = 0, binding = 1) uniform texture2D tex;
layout(set = 0, binding = 2) uniform sampler s;

void main() {
	out_color = texture(sampler2D(tex, s), tex_coord);
}
