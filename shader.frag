/* SPDX-License-Identifier: MIT */

#version 450

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform block {
	vec4 color;
};

void main() {
	out_color = color;
}
