/* SPDX-License-Identifier: MIT */

#include "vulkan.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include "wayland.h"
#include "scene.h"

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
create_descriptor_pool(struct vulkan *vk,
		       struct vulkan_surface *surf)
{
	VkResult res;
	static const VkDescriptorPoolSize pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 4,
	};
	static const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 4,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};

	res = vkCreateDescriptorPool(vk->logical_device, &pool_info,
				     NULL, &surf->desc_pool);
	if (res < 0) {
		fprintf(stderr, "vkCreateDesciptorPool: 0x%x\n", res);
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

		vkDestroyFramebuffer(vk->logical_device, img->framebuffer, NULL);
		vkDestroyImageView(vk->logical_device, img->image_view, NULL);
		img->image = VK_NULL_HANDLE;
		img->image_view = VK_NULL_HANDLE;
		img->framebuffer = VK_NULL_HANDLE;
	}
}

static int
vulkan_surface_resize_swapchain(struct vulkan *vk,
				struct vulkan_surface *surf,
				uint32_t width, uint32_t height)
{
	vkQueueWaitIdle(vk->gfx_queue->queue);

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
create_buffers(struct vulkan *vk, struct scene *scene, size_t *index_size,
	       VkBuffer *vert, VkBuffer *index, VkBuffer *uniform,
	       VkDeviceMemory *mem)
{
	size_t vert_size;
	VkResult res;
	VkMemoryRequirements vert_req;
	VkMemoryRequirements index_req;
	VkMemoryRequirements uniform_req;
	uint32_t bits;
	uint64_t align;
	uint64_t mem_size;
	uint64_t index_offset;
	uint64_t uniform_offset;
	VkPhysicalDeviceMemoryProperties props;
	static const uint32_t flags =
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	uint32_t i;
	void *v_data;
	uint8_t *data;

	scene_get_vertex_index_sizes(scene, &vert_size, index_size);

	create_buffer(vk, vert_size * sizeof(float),
		      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		      vert, &vert_req);
	create_buffer(vk, *index_size * sizeof(uint16_t),
		      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		      index, &index_req);
	create_buffer(vk, sizeof(float[3][4]),
		      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		      uniform, &uniform_req);

	printf("Vert Memory Requirements: size: %lu, align: %lu, bits: 0x%x\n",
		vert_req.size, vert_req.alignment, vert_req.memoryTypeBits);
	printf("Index Memory Requirements: size: %lu, align: %lu, bits: 0x%x\n",
		index_req.size, index_req.alignment, index_req.memoryTypeBits);
	printf("Uniform Memory Requirements: size: %lu, align: %lu, bits: 0x%x\n",
		uniform_req.size, uniform_req.alignment, uniform_req.memoryTypeBits);

	bits = vert_req.memoryTypeBits & index_req.memoryTypeBits &
		uniform_req.memoryTypeBits;

	mem_size = vert_req.size;

	align = mem_size % index_req.alignment;
	if (align != 0)
		mem_size += index_req.alignment - align;

	index_offset = mem_size;
	mem_size += index_req.size;

	align = mem_size % uniform_req.alignment;
	if (align != 0)
		mem_size += uniform_req.alignment - align;

	uniform_offset = mem_size;
	mem_size += uniform_req.size;

	printf("Total size: %lu, Index offset: %lu, Uniform offset: %lu\n",
	       mem_size, index_offset, uniform_offset);

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

	const VkBindBufferMemoryInfo bind_info[3] = {
		{
			.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
			.buffer = *vert,
			.memory = *mem,
			.memoryOffset = 0,
		},
		{
			.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
			.buffer = *index,
			.memory = *mem,
			.memoryOffset = index_offset,
		},
		{
			.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
			.buffer = *uniform,
			.memory = *mem,
			.memoryOffset = uniform_offset,
		},
	};

	res = vkBindBufferMemory2(vk->logical_device, 3, bind_info);
	if (res < 0) {
		fprintf(stderr, "vkBindBufferMemory2: 0x%x\n", res);
		abort();
	}

	res = vkMapMemory(vk->logical_device, *mem, 0, mem_size, 0, &v_data);
	if (res < 0) {
		fprintf(stderr, "vkMapMemory: 0x%x\n", res);
		abort();
	}

	data = v_data;

	scene_get_vertex_index_data(scene, (float *)data,
				    (uint16_t *)(data + index_offset));

	static const float mat[3][4] = {
		{ 2.0f / 200.0f, 0.0f, 0.0f, NAN },
		{ 0.0f, 2.0f / 200.0f, 0.0f, NAN },
		{ -1.0f, -1.0f, 1.0f, NAN },
	};

	memcpy(&data[uniform_offset], mat, sizeof mat);

	vkUnmapMemory(vk->logical_device, *mem);
}

static struct vulkan_frame *
vulkan_surface_prepare_frame(struct vulkan_surface *surf)
{
	struct vulkan *vk = surf->vk;
	struct vulkan_frame *f = NULL, *iter;
	VkResult res;

	wl_list_for_each(iter, &surf->frame_res, link) {
		res = vkGetFenceStatus(vk->logical_device, iter->fence);
		if (res == VK_SUCCESS) {
			f = iter;
			break;
		}
	}

	if (f) {
		vkResetFences(vk->logical_device, 1, &f->fence);

		vkDestroyBuffer(vk->logical_device, f->vertex_buf, NULL);
		vkDestroyBuffer(vk->logical_device, f->index_buf, NULL);
		vkDestroyBuffer(vk->logical_device, f->uniform_buf, NULL);
		vkFreeMemory(vk->logical_device, f->memory, NULL);

		f->vertex_buf = VK_NULL_HANDLE;
		f->index_buf = VK_NULL_HANDLE;
		f->uniform_buf = VK_NULL_HANDLE;
		f->memory = VK_NULL_HANDLE;

		/* Reinsert at end of queue */
		wl_list_remove(&f->link);
		wl_list_insert(surf->frame_res.prev, &f->link);

		return f;
	} else {
		f = calloc(1, sizeof *f);
		if (!f)
			return NULL;

		const VkCommandBufferAllocateInfo cmd_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = vk->gfx_queue->command_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};

		res = vkAllocateCommandBuffers(vk->logical_device, &cmd_info,
					       &f->command_buffer);
		if (res < 0) {
			fprintf(stderr, "vkAllocateCommandBuffers: 0x%x\n", res);
			free(f);
			return NULL;
		}

		static const VkFenceCreateInfo fence_info = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = 0,
		};

		res = vkCreateFence(vk->logical_device, &fence_info, NULL,
				    &f->fence);
		if (res < 0) {
			fprintf(stderr, "vkCreateFence: 0x%x\n", res);
			return NULL;
		}

		const VkDescriptorSetAllocateInfo ds_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = surf->desc_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &vk->renderpass.ds_layout,
		};

		res = vkAllocateDescriptorSets(vk->logical_device, &ds_info,
			&f->desc);
		if (res < 0) {
			fprintf(stderr, "vkAllocateDescriptorSets: 0x%x\n", res);
			return NULL;
		}
	}

	wl_list_insert(surf->frame_res.prev, &f->link);

	return f;
}

int
vulkan_surface_repaint(struct vulkan_surface *surf, struct scene *scene)
{
	struct vulkan *vk = surf->vk;
	VkResult res;
	uint32_t i;
	size_t index_size;
	struct vulkan_image *img;
	struct vulkan_frame *frame;

	if (surf->needs_realloc) {
		if (vulkan_surface_resize_swapchain(vk, surf,
						    surf->width,
						    surf->height) < 0)
			return -1;
		surf->needs_realloc = false;
	}

	res = vkAcquireNextImageKHR(vk->logical_device,
				    surf->swapchain,
				    (uint64_t)-1,
				    surf->acquire,
				    VK_NULL_HANDLE,
				    &i);
	if (res < 0) {
		fprintf(stderr, "vkAcquireNextImage2KHR: 0x%x\n",
			res);
		return 1;
	}
	if (res == VK_SUBOPTIMAL_KHR)
		surf->needs_realloc = true;

	img = &surf->images[i];
	frame = vulkan_surface_prepare_frame(surf);

	create_buffers(vk, scene, &index_size,
		       &frame->vertex_buf, &frame->index_buf,
		       &frame->uniform_buf, &frame->memory);

	const VkDescriptorBufferInfo buf_info = {
		.buffer = frame->uniform_buf,
		.offset = 0,
		.range = sizeof(float[3][4]),
	};
	const VkWriteDescriptorSet ds_write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = frame->desc,
		.dstBinding = 0,
		.dstArrayElement = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.pBufferInfo = &buf_info,
	};
	vkUpdateDescriptorSets(vk->logical_device, 1, &ds_write, 0, NULL);

	static const VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	res = vkBeginCommandBuffer(frame->command_buffer, &begin);
	if (res < 0) {
		fprintf(stderr, "vkBeginCommandBuffer: 0x%x\n",
			res);
		return -1;
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
		vkCmdPipelineBarrier(frame->command_buffer,
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
		.renderArea.extent.width = surf->width,
		.renderArea.extent.height = surf->height,
		.clearValueCount = 0,
		.pClearValues = NULL,
	};

	vkCmdBeginRenderPass(frame->command_buffer, &rp_info,
			     VK_SUBPASS_CONTENTS_INLINE);

	const VkViewport viewport = {
		.x = 0,
		.y = 0,
		.width = surf->width,
		.height = surf->height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	vkCmdSetViewport(frame->command_buffer, 0, 1, &viewport);

	const VkRect2D scissor = {
		.offset.x = 0,
		.offset.y = 0,
		.extent.width = surf->width,
		.extent.height = surf->height,
	};
	vkCmdSetScissor(frame->command_buffer, 0, 1, &scissor);

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
	vkCmdClearAttachments(frame->command_buffer,
			      1, &clear, 1, &clear_rect);

	vkCmdBindPipeline(frame->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			  vk->renderpass.pipeline);

	static const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(frame->command_buffer, 0, 1,
			       &frame->vertex_buf, &offset);

	vkCmdBindIndexBuffer(frame->command_buffer, frame->index_buf,
			     0, VK_INDEX_TYPE_UINT16);

	vkCmdBindDescriptorSets(frame->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				vk->renderpass.pipeline_layout, 0, 1, &frame->desc,
				0, NULL);

	float color[4] = { 0.8f, 0.0f, 0.0f, 0.8f };

	vkCmdPushConstants(frame->command_buffer, vk->renderpass.pipeline_layout,
			   VK_SHADER_STAGE_FRAGMENT_BIT, 0,
			   sizeof color, color);

	vkCmdDrawIndexed(frame->command_buffer, index_size, 1, 0, 0, 0);

	vkCmdEndRenderPass(frame->command_buffer);

	res = vkEndCommandBuffer(frame->command_buffer);
	if (res < 0) {
		fprintf(stderr, "vkEndCommandBuffer: 0x%x\n", res);
		return -1;
	}

	static const VkPipelineStageFlags wait =
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	const VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &surf->acquire,
		.pWaitDstStageMask = &wait,
		.commandBufferCount = 1,
		.pCommandBuffers = &frame->command_buffer,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &surf->done,
	};

	res = vkQueueSubmit(vk->gfx_queue->queue, 1, &submit_info,
			    frame->fence);
	if (res < 0) {
		fprintf(stderr, "vkQueueSubmit: 0x%x\n", res);
		return -1;
	}

	const VkPresentInfoKHR present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &surf->done,
		.swapchainCount = 1,
		.pSwapchains = &surf->swapchain,
		.pImageIndices = &i,
		.pResults = NULL,
	};

	res = vkQueuePresentKHR(vk->gfx_queue->queue, &present_info);
	if (res < 0) {
		fprintf(stderr, "vkQueuePresentKHR: 0x%x\n", res);
		return -1;
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

	if (create_descriptor_pool(vk, surf) < 0)
		return -1;

	surf->vk = vk;
	surf->needs_realloc = true;
	wl_list_init(&surf->frame_res);

	return 0;
}
