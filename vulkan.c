/* SPDX-License-Identifier: MIT */

#include "vulkan.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

	const VkDeviceCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = num_queues,
		.pQueueCreateInfos = queues,
		.enabledExtensionCount = ARRAY_LEN(exts),
		.ppEnabledExtensionNames = exts,
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

	uint32_t i;
	for (i = 0; i < num_phy; ++i)
		if (physical_device_find_queues(phy[i], wl, gfx, xfer) == 0)
			break;

	if (i == num_phy) {
		printf("VK: No suitable device found\n");
		return -1;
	}

	vk->physical_device = phy[i];
	return 0;
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
		.apiVersion = VK_API_VERSION_1_1,
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

	return 0;
}
