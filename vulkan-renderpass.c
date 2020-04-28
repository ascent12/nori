/* SPDX-License-Identifier: MIT */

#include "vulkan.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>
#include <wayland-client-core.h>

#include "shader.vert.h"
#include "shader.frag.h"

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))

static int
create_renderpass(struct vulkan *vk,
		  struct vulkan_renderpass *rp)
{
	VkResult res;
	static const VkAttachmentDescription attachment = {
		.flags = 0,
		.format = VK_FORMAT_B8G8R8A8_UNORM,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	};

	static const VkAttachmentReference attach_ref = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	static const VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount = 0,
		.pInputAttachments = NULL,
		.colorAttachmentCount = 1,
		.pColorAttachments = &attach_ref,
		.pResolveAttachments = NULL,
		.pDepthStencilAttachment = NULL,
		.preserveAttachmentCount = 0,
		.pPreserveAttachments = NULL,
	};

	static const VkSubpassDependency dep = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstAccessMask =
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	};

	static const VkRenderPassCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &attachment,
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 1,
		.pDependencies = &dep,
	};

	res = vkCreateRenderPass(vk->logical_device, &info, NULL,
				 &rp->renderpass);
	if (res < 0) {
		fprintf(stderr, "vkCreateRenderPass: 0x%x\n",
			res);
		return -1;
	}

	return 0;
}

static int
create_sampler(struct vulkan *vk, struct vulkan_renderpass *rp)
{
	VkResult res;
	static const VkSamplerCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.flags = 0,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.mipLodBias = 0.0f,
		.anisotropyEnable = VK_FALSE,
		.compareEnable = VK_FALSE,
		.minLod = 0.0f,
		.maxLod = 0.0f,
		.unnormalizedCoordinates = VK_FALSE,
	};

	res = vkCreateSampler(vk->logical_device, &info, NULL, &rp->sampler);
	if (res < 0) {
		fprintf(stderr, "vkCreateSampler: 0x%x\n", res);
		return -1;
	}

	return 0;
}

static int
create_pipeline_layout(struct vulkan *vk, struct vulkan_renderpass *rp)
{
	VkResult res;
	static const VkDescriptorBindingFlags desc_flags[] = {
		0,
		0,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
	};
	const VkDescriptorSetLayoutBinding desc_bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = &rp->sampler,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		},
		{
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.descriptorCount = vk->max_textures,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		},
	};

	static_assert(ARRAY_LEN(desc_flags) == ARRAY_LEN(desc_bindings));

	static const VkDescriptorSetLayoutBindingFlagsCreateInfo binding_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.bindingCount = ARRAY_LEN(desc_flags),
		.pBindingFlags = desc_flags,
	};
	const VkDescriptorSetLayoutCreateInfo desc_layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &binding_info,
		.bindingCount = ARRAY_LEN(desc_bindings),
		.pBindings = desc_bindings,
	};

	res = vkCreateDescriptorSetLayout(vk->logical_device,
					  &desc_layout_info,
					  NULL, &rp->desc_layout);
	if (res < 0) {
		fprintf(stderr, "vkCreateDescriptorSetLayout: 0x%x\n", res);
		return -1;
	}

	static const VkPushConstantRange range = {
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(int32_t),
	};
	const VkPipelineLayoutCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &rp->desc_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &range,
	};

	res = vkCreatePipelineLayout(vk->logical_device,
				     &info, NULL,
				     &rp->pipeline_layout);
	if (res < 0) {
		fprintf(stderr, "vkCreatePipelineLayout: 0x%x\n", res);
		return -1;
	}

	return 0;
}

static int
compile_shaders(struct vulkan *vk,
		VkShaderModule *vert, VkShaderModule *frag)
{
	VkResult res;
	static const VkShaderModuleCreateInfo vert_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = sizeof vert_shader,
		.pCode = vert_shader,
	};
	static const VkShaderModuleCreateInfo frag_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = sizeof frag_shader,
		.pCode = frag_shader,
	};

	res = vkCreateShaderModule(vk->logical_device, &vert_info, NULL, vert);
	if (res < 0) {
		fprintf(stderr, "vkCreateShaderModule (vert): 0x%x\n",
			res);
		return -1;
	}

	res = vkCreateShaderModule(vk->logical_device, &frag_info, NULL, frag);
	if (res < 0) {
		fprintf(stderr, "vkCreateShaderModule (frag): 0x%x\n",
			res);
		return -1;
	}

	return 0;
}

static int
create_pipeline(struct vulkan *vk,
		struct vulkan_renderpass *rp,
		VkShaderModule *vert, VkShaderModule *frag)
{
	VkResult res;
	static const VkSpecializationMapEntry max_tex = {
		.constantID = 0,
		.offset = 0,
		.size = sizeof(vk->max_textures),
	};
	const VkSpecializationInfo frag_spec = {
		.mapEntryCount = 1,
		.pMapEntries = &max_tex,
		.dataSize = sizeof(vk->max_textures),
		.pData = &vk->max_textures,
	};
	const VkPipelineShaderStageCreateInfo shader_info[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = *vert,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = *frag,
			.pName = "main",
			.pSpecializationInfo = &frag_spec,
		},
	};

	static const VkVertexInputBindingDescription vi_bind = {
		.binding = 0,
		.stride = sizeof(float[4]),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};
	static const VkVertexInputAttributeDescription vi_attr[] = {
		{
			.binding = 0,
			.location = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = 0,
		},
		{
			.binding = 0,
			.location = 1,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = sizeof(float[2]),
		},
	};
	static const VkPipelineVertexInputStateCreateInfo vi_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vi_bind,
		.vertexAttributeDescriptionCount = ARRAY_LEN(vi_attr),
		.pVertexAttributeDescriptions = vi_attr,
	};

	static const VkPipelineInputAssemblyStateCreateInfo asm_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE,
	};

	static const VkPipelineViewportStateCreateInfo vp_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	static const VkPipelineRasterizationStateCreateInfo rast_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
		.lineWidth = 1.0f,
	};

	static const VkPipelineMultisampleStateCreateInfo ms_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		.sampleShadingEnable = VK_FALSE,
		.minSampleShading = 0.0f,
		.pSampleMask = NULL,
		.alphaToCoverageEnable = VK_FALSE,
		.alphaToOneEnable = VK_FALSE,
	};

	static const VkPipelineColorBlendAttachmentState cb_attachment = {
		.blendEnable = VK_TRUE,
		.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT |
			VK_COLOR_COMPONENT_A_BIT,
	};
	static const VkPipelineColorBlendStateCreateInfo cb_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		//.logicOp = VK_LOGIC_OP_CLEAR,
		.attachmentCount = 1,
		.pAttachments = &cb_attachment,
		//.blendConstants = { 1.0, 1.0, 1.0, 1.0 },
	};

	static const VkDynamicState dyn[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};
	static const VkPipelineDynamicStateCreateInfo dyn_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = ARRAY_LEN(dyn),
		.pDynamicStates = dyn,
	};

	const VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = ARRAY_LEN(shader_info),
		.pStages = shader_info,
		.pVertexInputState = &vi_info,
		.pInputAssemblyState = &asm_info,
		.pTessellationState = NULL,
		.pViewportState = &vp_info,
		.pRasterizationState = &rast_info,
		.pMultisampleState = &ms_info,
		.pDepthStencilState = NULL,
		.pColorBlendState = &cb_info,
		.pDynamicState = &dyn_info,
		.layout = rp->pipeline_layout,
		.renderPass = rp->renderpass,
		.subpass = 0,
		.basePipelineHandle = VK_NULL_HANDLE,
		.basePipelineIndex = -1,
	};

	res = vkCreateGraphicsPipelines(vk->logical_device, NULL,
					1, &pipeline_info,
					NULL, &rp->pipeline);
	if (res < 0) {
		fprintf(stderr, "vkCreateGraphicsPipelines: 0x%x\n",
			res);
		return -1;
	}

	return 0;
}

int
vulkan_init_renderpass(struct vulkan *vk,
		       struct vulkan_renderpass *rp)
{
	VkShaderModule vert, frag;

	if (create_renderpass(vk, rp) < 0)
		return -1;

	if (create_sampler(vk, rp) < 0)
		return -1;

	if (create_pipeline_layout(vk, rp) < 0)
		return -1;

	if (compile_shaders(vk, &vert, &frag) < 0)
		return -1;

	if (create_pipeline(vk, rp, &vert, &frag) < 0)
		return -1;

	vkDestroyShaderModule(vk->logical_device, vert, NULL);
	vkDestroyShaderModule(vk->logical_device, frag, NULL);

	return 0;
}
