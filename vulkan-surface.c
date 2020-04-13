/* SPDX-License-Identifier: MIT */

#include "vulkan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include "wayland.h"

static int
create_image_view(struct vulkan *vk,
		  struct vulkan_image *image)
{
	VkResult res;
	const VkImageViewCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image->image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_B8G8R8A8_UNORM,
		.components = {
			.r = VK_COMPONENT_SWIZZLE_IDENTITY,
			.g = VK_COMPONENT_SWIZZLE_IDENTITY,
			.b = VK_COMPONENT_SWIZZLE_IDENTITY,
			.a = VK_COMPONENT_SWIZZLE_IDENTITY,
		},
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	res = vkCreateImageView(vk->logical_device, &info, NULL,
				&image->image_view);
	if (res < 0) {
		fprintf(stderr, "vkCreateImageView: 0x%x\n",
			res);
		return -1;
	}

	return 0;
}

static int
create_framebuffer(struct vulkan *vk,
		   struct vulkan_image *image,
		   uint32_t width, uint32_t height)
{
	VkResult res;
	const VkFramebufferCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass = vk->renderpass.renderpass,
		.attachmentCount = 1,
		.pAttachments = &image->image_view,
		.width = width,
		.height = height,
		.layers = 1,
	};

	res = vkCreateFramebuffer(vk->logical_device,
				  &info,
				  NULL,
				  &image->framebuffer);
	if (res < 0) {
		fprintf(stderr, "vkCreateFramebuffer: 0x%x\n",
			res);
		return -1;
	}

	return 0;
}

static int
create_fence(struct vulkan *vk,
	     struct vulkan_image *image)
{
	VkResult res;
	static const VkFenceCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};

	res = vkCreateFence(vk->logical_device, &info, NULL, &image->fence);
	if (res < 0) {
		fprintf(stderr, "vkCreateFence: 0x%x\n", res);
		return -1;
	}

	return 0;
}

static int
alloc_command_buffers(struct vulkan *vk,
		      VkCommandBuffer *bufs,
		      uint32_t n)
{
	VkResult res;
	const VkCommandBufferAllocateInfo info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = vk->gfx_queue->command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = n,
	};

	res = vkAllocateCommandBuffers(vk->logical_device, &info, bufs);
	if (res < 0) {
		fprintf(stderr, "vkAllocateCommandBuffers: 0x%x\n",
			res);
		return -1;
	}

	return 0;
}

static int
get_swapchain_images(struct vulkan *vk,
		     struct vulkan_surface *surf,
		     uint32_t width, uint32_t height)
{
	uint32_t num_images;
	vkGetSwapchainImagesKHR(vk->logical_device,
				surf->swapchain,
				&num_images,
				NULL);

	VkImage images[num_images];

	vkGetSwapchainImagesKHR(vk->logical_device,
				surf->swapchain,
				&num_images,
				images);

	surf->images = realloc(surf->images, num_images * sizeof *surf->images);
	if (!surf->images)
		return -1;

	for (uint32_t i = 0; i < num_images; ++i) {
		struct vulkan_image *img = &surf->images[i];

		img->image = images[i];

		if (create_image_view(vk, img) < 0)
			return -1;

		if (create_framebuffer(vk, img, width, height) < 0)
			return -1;

		img->undefined = true;
	}

	if (num_images > surf->num_images) {
		uint32_t new = num_images - surf->num_images;
		VkCommandBuffer buffers[new];

		if (alloc_command_buffers(vk, buffers, new) < 0)
			return -1;

		for (uint32_t i = surf->num_images; i < num_images; ++i) {
			struct vulkan_image *img = &surf->images[i];

			if (create_fence(vk, img) < 0)
				return -1;

			img->command_buffer = buffers[i];
		}
	}

	surf->num_images = num_images;

	return 0;
}

static int
create_swapchain(struct vulkan *vk,
		 struct vulkan_surface *surf,
		 uint32_t width, uint32_t height)
{
	VkSwapchainKHR old = surf->swapchain;
	VkResult res;
	const VkSwapchainCreateInfoKHR info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surf->surface,
		.minImageCount = surf->min_images,
		.imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
		.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
		.imageExtent.width = width,
		.imageExtent.height = height,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
		.clipped = VK_FALSE,
		.oldSwapchain = old,
	};

	res = vkCreateSwapchainKHR(vk->logical_device,
				   &info, NULL, &surf->swapchain);
	if (res < 0) {
		fprintf(stderr, "vkCreateSwapchainKHR: 0x%x\n",
			res);
		return -1;
	}

	if (old != VK_NULL_HANDLE)
		vkDestroySwapchainKHR(vk->logical_device, old, NULL);

	return 0;
}

static void
cleanup_old_swapchain(struct vulkan *vk,
		      struct vulkan_surface *surf)
{
	if (surf->swapchain == VK_NULL_HANDLE)
		return;

	for (uint32_t i = 0; i < surf->num_images; ++i) {
		struct vulkan_image *img = &surf->images[i];

		vkWaitForFences(vk->logical_device, 1, &img->fence,
				VK_TRUE, UINT64_MAX);

		vkDestroyFramebuffer(vk->logical_device, img->framebuffer, NULL);
		vkDestroyImageView(vk->logical_device, img->image_view, NULL);
		img->image = VK_NULL_HANDLE;
		img->image_view = VK_NULL_HANDLE;
		img->framebuffer = VK_NULL_HANDLE;

		/* The command buffer and the fence for it are preserved */
	}
}

static int
vulkan_surface_resize_swapchain(struct vulkan *vk,
				struct vulkan_surface *surf,
				uint32_t width, uint32_t height)
{
	cleanup_old_swapchain(vk, surf);

	if (create_swapchain(vk, surf, width, height) < 0)
		return -1;

	if (get_swapchain_images(vk, surf, width, height) < 0)
		return -1;

	return 0;
}

void
vulkan_surface_resize(struct vulkan_surface *surf, uint32_t w, uint32_t h)
{
	surf->needs_realloc = true;
	surf->width = w;
	surf->height = h;
}

extern struct wl_array glyphs, indices;

static void
create_buffer(struct vulkan *vk, uint64_t size, VkBufferUsageFlags usage,
	      VkBuffer *buf, VkMemoryRequirements *reqs)
{
	VkResult res;
	const VkBufferCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	res = vkCreateBuffer(vk->logical_device, &info, NULL, buf);
	if (res < 0) {
		fprintf(stderr, "vkCreateBuffer: 0x%x\n",
			res);
		abort();
	}

	vkGetBufferMemoryRequirements(vk->logical_device, *buf, reqs);
}

static void
create_buffers(struct vulkan *vk, VkBuffer *vert, VkBuffer *index,
	       VkDeviceMemory *mem)
{
	VkResult res;
	VkMemoryRequirements vert_req;
	VkMemoryRequirements index_req;
	uint32_t bits;
	uint64_t align;
	uint64_t mem_size;
	uint64_t index_offset;
	VkPhysicalDeviceMemoryProperties props;
	static const uint32_t flags =
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	uint32_t i;
	void *v_data;
	uint8_t *data;

	create_buffer(vk, glyphs.size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		      vert, &vert_req);
	create_buffer(vk, indices.size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		      index, &index_req);

	printf("Memory Requirements: size: %lu, align: %lu, bits: 0x%x\n",
		vert_req.size, vert_req.alignment, vert_req.memoryTypeBits);
	printf("Memory Requirements: size: %lu, align: %lu, bits: 0x%x\n",
		index_req.size, index_req.alignment, index_req.memoryTypeBits);

	bits = vert_req.memoryTypeBits & index_req.memoryTypeBits;

	mem_size = vert_req.size;

	align = mem_size % index_req.alignment;
	if (align != 0) {
		mem_size += index_req.alignment - align;
	}

	index_offset = mem_size;
	mem_size += index_req.size;

	printf("Total size: %lu, Index offset: %lu\n", mem_size, index_offset);

	vkGetPhysicalDeviceMemoryProperties(vk->physical_device, &props);

	for (i = 0; i < props.memoryTypeCount; ++i) {
		if (!(bits & (1 << i)))
			continue;
		if (!(props.memoryTypes[i].propertyFlags & flags))
			continue;

		break;
	}

	if (i == props.memoryTypeCount) {
		fprintf(stderr, "No suitable memory type found\n");
		abort();
	}

	const VkMemoryAllocateInfo info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_size,
		.memoryTypeIndex = i,
	};

	res = vkAllocateMemory(vk->logical_device, &info, NULL, mem);
	if (res < 0) {
		fprintf(stderr, "vkAllocateMemory: 0x%x\n", res);
		abort();
	}

	res = vkBindBufferMemory(vk->logical_device, *vert, *mem, 0);
	if (res < 0) {
		fprintf(stderr, "vkBindBufferMemory: 0x%x\n", res);
		abort();
	}

	res = vkBindBufferMemory(vk->logical_device, *index, *mem, index_offset);
	if (res < 0) {
		fprintf(stderr, "vkBindBufferMemory: 0x%x\n", res);
		abort();
	}

	res = vkMapMemory(vk->logical_device, *mem, 0, mem_size, 0, &v_data);
	if (res < 0) {
		fprintf(stderr, "vkMapMemory: 0x%x\n", res);
		abort();
	}

	data = v_data;

	memcpy(&data[0], glyphs.data, glyphs.size);
	memcpy(&data[index_offset], indices.data, indices.size);

	vkUnmapMemory(vk->logical_device, *mem);
}

int
vulkan_surface_repaint(struct vulkan_surface *vk_surface)
{
	struct vulkan *vk = vk_surface->vk;
	static VkBuffer vert_buf, index_buf;
	static VkDeviceMemory mem;
	static bool do_once = false;
	VkResult res;
	uint32_t i;

	/* Hack until vulkan memory is sorted out */
	if (!do_once) {
		create_buffers(vk, &vert_buf, &index_buf, &mem);
		do_once = true;
	}

	if (vk_surface->needs_realloc) {
		if (vulkan_surface_resize_swapchain(vk, vk_surface,
						    vk_surface->width,
						    vk_surface->height) < 0)
			return -1;
		vk_surface->needs_realloc = false;
	}

	res = vkAcquireNextImageKHR(vk->logical_device,
				    vk_surface->swapchain,
				    (uint64_t)-1,
				    vk_surface->acquire,
				    VK_NULL_HANDLE,
				    &i);
	if (res < 0) {
		fprintf(stderr, "vkAcquireNextImage2KHR: 0x%x\n",
			res);
		return 1;
	}
	if (res == VK_SUBOPTIMAL_KHR)
		vk_surface->needs_realloc = true;

	struct vulkan_image *img = &vk_surface->images[i];

	vkWaitForFences(vk->logical_device, 1, &img->fence,
			VK_TRUE, UINT64_MAX);
	vkResetFences(vk->logical_device, 1, &img->fence);

	static const VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	res = vkBeginCommandBuffer(img->command_buffer, &begin);
	if (res < 0) {
		fprintf(stderr, "vkBeginCommandBuffer: 0x%x\n",
			res);
		return 1;
	}

	/*
	 * Transition from UNDEFINED layout to PRESENT_SRC,
	 * which our renderpass expects.
	 */
	if (img->undefined) {
		const VkImageMemoryBarrier barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = img->image,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};
		vkCmdPipelineBarrier(img->command_buffer,
				     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				     0,
				     0, NULL,
				     0, NULL,
				     1, &barrier);

		img->undefined = false;
	}

	const VkRenderPassBeginInfo rp_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = vk->renderpass.renderpass,
		.framebuffer = img->framebuffer,
		.renderArea.offset.x = 0,
		.renderArea.offset.y = 0,
		.renderArea.extent.width = vk_surface->width,
		.renderArea.extent.height = vk_surface->height,
		.clearValueCount = 0,
		.pClearValues = NULL,
	};

	vkCmdBeginRenderPass(img->command_buffer, &rp_info,
			     VK_SUBPASS_CONTENTS_INLINE);

	const VkViewport viewport = {
		.x = 0,
		.y = 0,
		.width = vk_surface->width,
		.height = vk_surface->height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	vkCmdSetViewport(img->command_buffer, 0, 1, &viewport);

	const VkRect2D scissor = {
		.offset.x = 0,
		.offset.y = 0,
		.extent.width = vk_surface->width,
		.extent.height = vk_surface->height,
	};
	vkCmdSetScissor(img->command_buffer, 0, 1, &scissor);

	static const VkClearAttachment clear = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.colorAttachment = 0,
		.clearValue.color.float32 = { 0.8f, 0.8f, 0.8f, 0.8f },
	};
	const VkClearRect clear_rect = {
		.rect = scissor,
		.baseArrayLayer = 0,
		.layerCount = 1,
	};
	vkCmdClearAttachments(img->command_buffer,
			      1, &clear, 1, &clear_rect);

	vkCmdBindPipeline(img->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			  vk->renderpass.pipeline);

	static const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(img->command_buffer, 0, 1, &vert_buf, &offset);

	vkCmdBindIndexBuffer(img->command_buffer, index_buf,
			     0, VK_INDEX_TYPE_UINT16);

	// Padded out to fit mat3 row alignment
#if 0
	float mat[12] = {
		2.0f / (pen_26_6 >> 6), 0.0f, 0.0f, NAN,
		0.0f, 2.0f / (line_height >> 6), 0.0f, NAN,
		-1.0f, -1.0f, 1.0f, NAN,
	};
#else
	float mat[12] = {
		2.0f / 100.0f, 0.0f, 0.0f, NAN,
		0.0f, 2.0f / 20.0f, 0.0f, NAN,
		-1.0f, -1.0f, 1.0f, NAN,
	};
#endif

	vkCmdPushConstants(img->command_buffer, vk->renderpass.pipeline_layout,
			   VK_SHADER_STAGE_VERTEX_BIT, 0,
			   sizeof mat, mat);

	vkCmdDrawIndexed(img->command_buffer, indices.size / sizeof(uint16_t), 1, 0, 0, 0);

	vkCmdEndRenderPass(img->command_buffer);

	res = vkEndCommandBuffer(img->command_buffer);
	if (res < 0) {
		fprintf(stderr, "vkEndCommandBuffer: 0x%x\n",
			res);
		return 1;
	}

	static const VkPipelineStageFlags wait =
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	const VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &vk_surface->acquire,
		.pWaitDstStageMask = &wait,
		.commandBufferCount = 1,
		.pCommandBuffers = &img->command_buffer,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &vk_surface->done,
	};

	res = vkQueueSubmit(vk->gfx_queue->queue, 1, &submit_info,
			    img->fence);
	if (res < 0) {
		fprintf(stderr, "vkQueueSubmit: 0x%x\n",
			res);
		return 1;
	}

	const VkPresentInfoKHR present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &vk_surface->done,
		.swapchainCount = 1,
		.pSwapchains = &vk_surface->swapchain,
		.pImageIndices = &i,
		.pResults = NULL,
	};

	res = vkQueuePresentKHR(vk->gfx_queue->queue, &present_info);
	if (res < 0) {
		fprintf(stderr, "vkQueuePresentKHR: 0x%x\n",
			res);
		return 1;
	}

	return 0;
}

static int create_semaphores(struct vulkan *vk,
			     struct vulkan_surface *surf)
{
	VkResult res;
	static const VkSemaphoreCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};

	res = vkCreateSemaphore(vk->logical_device, &info, NULL,
				&surf->acquire);
	if (res < 0) {
		fprintf(stderr, "vkCreateSemaphore: 0x%x\n",
			res);
		return -1;
	}

	res = vkCreateSemaphore(vk->logical_device, &info, NULL,
				&surf->done);
	if (res < 0) {
		fprintf(stderr, "vkCreateSemaphore: 0x%x\n",
			res);
		return -1;
	}

	return 0;
}

int
vulkan_surface_init(struct vulkan_surface *surf,
		    struct vulkan *vk,
		    struct wayland_surface *wl_surf)
{
	VkResult res;
	VkBool32 supported;
	const VkWaylandSurfaceCreateInfoKHR info = {
		.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
		.display = wl_surf->wl->display,
		.surface = wl_surf->surf,
	};
	VkSurfaceCapabilitiesKHR caps;

	res = vkCreateWaylandSurfaceKHR(vk->instance, &info, NULL, &surf->surface);
	if (res < 0) {
		fprintf(stderr, "vkCreateWaylandSurfaceKHR: 0x%x\n", res);
		return -1;
	}

	/*
	 * This is entirely a formality on Wayland. It conceptually doesn't
	 * make sense for a Wayland surface, and it will literally always
	 * return true in Mesa. However, the validiation layers will yell at us
	 * if we don't do it.
	 *
	 * If this code ever grows X11 support, then this actually checks
	 * something.
	 */

	vkGetPhysicalDeviceSurfaceSupportKHR(vk->physical_device,
					     vk->gfx_queue->index,
					     surf->surface,
					     &supported);

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk->physical_device,
						  surf->surface,
						  &caps);

	surf->min_images = caps.minImageCount;

	if (create_semaphores(vk, surf) < 0)
		return -1;

	surf->vk = vk;
	surf->needs_realloc = true;

	return 0;
}
