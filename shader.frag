/* SPDX-License-Identifier: MIT */

#version 450
/*
 * The vulkan spec says the minimum for maxPerStageDescriptorSampledImages
 * is 16, but I think it's MUCH higher on real implementations.
 */
layout(constant_id = 0) const int MAX_TEXTURES = 16;

layout(location = 0) in vec2 tex_coord;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 1) uniform sampler s;
layout(set = 1, binding = 0) uniform texture2D tex[MAX_TEXTURES];
layout(push_constant) uniform block {
	int tex_id;
};

void main() {
	out_color = texture(sampler2D(tex[tex_id], s), tex_coord);
}
