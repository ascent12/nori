/* SPDX-License-Identifier: MIT */

#include "vulkan.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>
#include <wayland-client-core.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))

static struct vulkan_queue *
create_queue(struct vulkan *vk, uint32_t index)
{
	VkResult res;
	const VkCommandPoolCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = index,
	};

	struct vulkan_queue *q = calloc(1, sizeof *q);
	if (!q)
		return NULL;

	vkGetDeviceQueue(vk->logical_device, index, 0, &q->queue);

	res = vkCreateCommandPool(vk->logical_device, &info, NULL, &q->command_pool);
	if (res < 0) {
		fprintf(stderr, "vkCreateCommandPool: 0x%x\n",
			res);
		goto fail;
	}

	q->index = index;
	return q;

fail:
	free(q);
	return NULL;
}

static int
create_logical_device(struct vulkan *vk, uint32_t gfx, uint32_t xfer)
{
	VkResult res;
	/* TODO: check for these properly */
	static const char *exts[] = {
		"VK_KHR_swapchain",
	};
	static const float queue_pri = 0.0;

	const VkDeviceQueueCreateInfo queues[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = gfx,
			.queueCount = 1,
			.pQueuePriorities = &queue_pri,
		},
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = xfer,
			.queueCount = 1,
			.pQueuePriorities = &queue_pri,
		},
	};
	uint32_t num_queues = gfx == xfer ? 1 : 2;

	VkPhysicalDeviceVulkan12Features vk12_f = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.descriptorBindingPartiallyBound = VK_TRUE,
		/* Guranteed for Vulkan 1.2 graphics implementations */
		.imagelessFramebuffer = VK_TRUE,
	};
	VkPhysicalDeviceFeatures2 f = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &vk12_f,
		/*
		 * Allows us to use uniforms to index texture arrays
		 * in our shaders. AFAIK this is supported on all "real"
		 * Vulkan implementations.
		 */
		.features.shaderSampledImageArrayDynamicIndexing = VK_TRUE,
	};
	const VkDeviceCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &f,
		.queueCreateInfoCount = num_queues,
		.pQueueCreateInfos = queues,
		.enabledExtensionCount = ARRAY_LEN(exts),
		.ppEnabledExtensionNames = exts,
		.pEnabledFeatures = NULL,
	};

	res = vkCreateDevice(vk->physical_device, &info, NULL, &vk->logical_device);
	if (res < 0) {
		fprintf(stderr, "vkCreateDevice: 0x%x\n", res);
		return -1;
	}

	vk->gfx_queue = create_queue(vk, gfx);
	if (!vk->gfx_queue)
		return -1;

	if (gfx != xfer) {
		vk->xfer_queue = create_queue(vk, xfer);
		if (!vk->xfer_queue)
			return -1;
	} else {
		vk->xfer_queue = vk->gfx_queue;
	}

	return 0;
}

static int
physical_device_find_queues(VkPhysicalDevice phy, struct wl_display *wl,
			    uint32_t *gfx, uint32_t *xfer)
{
	/* Must have at least 1 graphics queue with Wayland support */
	uint32_t num_qf;
	vkGetPhysicalDeviceQueueFamilyProperties(phy, &num_qf, NULL);

	VkQueueFamilyProperties props[num_qf];
	vkGetPhysicalDeviceQueueFamilyProperties(phy, &num_qf, props);

	uint32_t compute_index;
	bool gfx_found = false;
	bool xfer_found = false;
	bool compute_found = false;

	for (uint32_t i = 0; i < num_qf; ++i) {
		uint32_t flags = props[i].queueFlags;
		if (flags & VK_QUEUE_GRAPHICS_BIT) {
			if (vkGetPhysicalDeviceWaylandPresentationSupportKHR(phy, i, wl)) {
				gfx_found = true;
				*gfx = i;
			}
		} else if (flags & VK_QUEUE_COMPUTE_BIT) {
			compute_found = true;
			compute_index = i;
		} else if (flags & VK_QUEUE_TRANSFER_BIT) {
			xfer_found = true;
			*xfer = i;
		}
	}

	if (!gfx_found)
		return -1;

	if (!xfer_found) {
		if (compute_found)
			*xfer = compute_index;
		else
			*xfer = *gfx;
	}

	return 0;
}

static int
select_physical_device(struct vulkan *vk, struct wl_display *wl,
		       uint32_t *gfx, uint32_t *xfer)
{
	uint32_t num_phy;
	vkEnumeratePhysicalDevices(vk->instance, &num_phy, NULL);
	printf("VK: %u Physical device(s)\n", num_phy);

	VkPhysicalDevice phy[num_phy];
	vkEnumeratePhysicalDevices(vk->instance, &num_phy, phy);

	for (uint32_t i = 0; i < num_phy; ++i) {
		VkPhysicalDeviceProperties props;
		VkPhysicalDeviceVulkan12Features vk12_f = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		};
		VkPhysicalDeviceFeatures2 f = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &vk12_f,
		};

		vkGetPhysicalDeviceProperties(phy[i], &props);
		vkGetPhysicalDeviceFeatures2(phy[i], &f);

		if (props.apiVersion < VK_API_VERSION_1_2)
			continue;

		if (!f.features.shaderSampledImageArrayDynamicIndexing)
			continue;
		if (!vk12_f.descriptorBindingPartiallyBound)
			continue;

		if (physical_device_find_queues(phy[i], wl, gfx, xfer) < 0)
			continue;

		vk->physical_device = phy[i];
		vk->max_textures = props.limits.maxPerStageDescriptorSampledImages;

		/*
		 * Lets keep it somewhat sensible, but still significantly
		 * higher than we'll realistically need
		 */
		if (vk->max_textures > 1024)
			vk->max_textures = 1024;

		return 0;
	}

	printf("VK: No suitable device found\n");
	return -1;
}

static VkBool32
vk_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT sev,
		  VkDebugUtilsMessageTypeFlagsEXT types,
		  const VkDebugUtilsMessengerCallbackDataEXT *data,
		  void *user)
{
	printf("VK debug message:\n");
	printf("  Message Id: \"%s\"\n", data->pMessageIdName);
	printf("  Message: \"%s\"\n", data->pMessage);
	return VK_FALSE;
}

static int
create_debug_messenger(struct vulkan *vk)
{
	static const VkDebugUtilsMessengerCreateInfoEXT info = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity =
			//VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
			//VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType =
			VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = vk_debug_callback,
		.pUserData = NULL,
	};

	VkResult res;
	PFN_vkCreateDebugUtilsMessengerEXT create_debug_msg;

	create_debug_msg = (PFN_vkCreateDebugUtilsMessengerEXT)
		vkGetInstanceProcAddr(vk->instance, "vkCreateDebugUtilsMessengerEXT");

	res = create_debug_msg(vk->instance, &info, NULL, &vk->debug_messenger);
	if (res < 0) {
		fprintf(stderr, "vkCreateDebugUtilsMessengerEXT: 0x%x\n", res);
		return -1;
	}

	return 0;
}

static int
create_instance(struct vulkan *vk)
{
	VkResult res;
	/* TODO: check for these properly */
	static const char *vk_layers[] = {
		"VK_LAYER_KHRONOS_validation",
	};
	static const char *vk_exts[] = {
		"VK_EXT_debug_utils",
		"VK_KHR_surface",
		"VK_KHR_wayland_surface",
	};
	static const VkApplicationInfo app = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.apiVersion = VK_API_VERSION_1_2,
	};

	static const VkInstanceCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.enabledLayerCount = ARRAY_LEN(vk_layers),
		.pApplicationInfo = &app,
		.ppEnabledLayerNames = vk_layers,
		.enabledExtensionCount = ARRAY_LEN(vk_exts),
		.ppEnabledExtensionNames = vk_exts,
	};

	res = vkCreateInstance(&info, NULL, &vk->instance);
	if (res < 0) {
		fprintf(stderr, "vkCreateInstance: 0x%x\n", res);
		return -1;
	}

	return 0;
}

int
vulkan_create(struct vulkan *vk, struct wl_display *wl)
{
	if (create_instance(vk) < 0)
		return -1;

	if (create_debug_messenger(vk) < 0)
		return -1;

	uint32_t gfx, xfer;
	if (select_physical_device(vk, wl, &gfx, &xfer) < 0)
		return -1;

	printf("VK: Graphics queue: %u, Transfer queue: %u\n", gfx, xfer);

	if (create_logical_device(vk, gfx, xfer) < 0)
		return -1;

	if (vulkan_mm_setup_types(vk) < 0)
		return -1;

	return 0;
}

struct vulkan_texture *
vulkan_texture_create(struct vulkan *vk, int width, int height, int stride,
		      void *pixels)
{
	VkResult res;
	struct vulkan_texture *t;
	struct vulkan_buffer staging;
	uint8_t (*in_data)[stride] = pixels;
	uint8_t (*data)[width];
	VkCommandBuffer cmd;
	static const VkComponentMapping mapping = {
		.r = VK_COMPONENT_SWIZZLE_ZERO,
		.g = VK_COMPONENT_SWIZZLE_ZERO,
		.b = VK_COMPONENT_SWIZZLE_ZERO,
		.a = VK_COMPONENT_SWIZZLE_R,
	};

	t = vulkan_mm_alloc_texture(vk, VK_FORMAT_R8_UNORM,
				    width, height, &mapping);
	if (!t)
		return NULL;


	vulkan_mm_alloc_staging_buffer(vk, &staging, width * height);
	data = staging.mem->data;

	for (int i = 0; i < height; ++i)
		memcpy(data[i], in_data[i], sizeof data[i]);

	const VkCommandBufferAllocateInfo cmd_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = vk->gfx_queue->command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	res = vkAllocateCommandBuffers(vk->logical_device, &cmd_info, &cmd);
	if (res < 0) {
		fprintf(stderr, "vkAllocateCommandBuffers: 0x%x\n", res);
		abort();
	}
	
	static const VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	res = vkBeginCommandBuffer(cmd, &begin);
	if (res < 0) {
		fprintf(stderr, "vkBeginCommandBuffer: 0x%x\n", res);
		abort();
	}

	const VkImageMemoryBarrier barrier_info1 = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = t->image,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	vkCmdPipelineBarrier(cmd,
			     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			     VK_PIPELINE_STAGE_TRANSFER_BIT,
			     0, 0, NULL, 0, NULL, 1, &barrier_info1);

	const VkBufferImageCopy region = {
		.bufferOffset = 0,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
		.imageOffset = { .x = 0, .y = 0, .z = 0 },
		.imageExtent = {
			.width = width,
			.height = height,
			.depth = 1,
		},
	};

	vkCmdCopyBufferToImage(cmd, staging.buffer, t->image,
			       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			       1, &region);

	const VkImageMemoryBarrier barrier_info2 = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = t->image,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	vkCmdPipelineBarrier(cmd,
			     VK_PIPELINE_STAGE_TRANSFER_BIT,
			     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			     0, 0, NULL, 0, NULL, 1, &barrier_info2);

	res = vkEndCommandBuffer(cmd);
	if (res < 0) {
		fprintf(stderr, "vkEndCommandBuffer: 0x%x\n", res);
		abort();
	}

	const VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd,
	};
	res = vkQueueSubmit(vk->gfx_queue->queue, 1, &submit_info,
			    VK_NULL_HANDLE);
	if (res < 0) {
		fprintf(stderr, "vkQueueSubmit: 0x%x\n", res);
		abort();
	}

	vkQueueWaitIdle(vk->gfx_queue->queue);

	vkFreeCommandBuffers(vk->logical_device, vk->gfx_queue->command_pool,
			     1, &cmd);

	vulkan_mm_free_buffer(vk, &staging);

	return t;
}
