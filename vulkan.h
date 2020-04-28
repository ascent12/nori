/* SPDX-License-Identifier: MIT */

#ifndef NORI_VULKAN_H
#define NORI_VULKAN_H

#include <stdbool.h>

#include <vulkan/vulkan.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

struct wayland_surface;
struct scene;

struct vulkan_queue {
	uint32_t index;
	VkQueue queue;
	VkCommandPool command_pool;
};

struct vulkan_renderpass {
	VkRenderPass renderpass;

	VkSampler sampler;

	VkDescriptorSetLayout desc_layout;
	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;
};

struct vulkan {
	VkInstance instance;

	VkDebugUtilsMessengerEXT debug_messenger;

	VkPhysicalDevice physical_device;
	VkDevice logical_device;

	/* These may or may not be the same */
	struct vulkan_queue *gfx_queue;
	struct vulkan_queue *xfer_queue;

	/*
	 * Memory types we use for various purposes.
	 * These may alias.
	 *
	 * staging_type:
	 *   CPU-accessable for transferring data to the device.
	 *
	 * texture_type:
	 *   Should be in fastest device memory.
	 *
	 * uniform_type, vertex_type:
	 *   Ideally CPU-accessable, but may not be.
	 */
	uint32_t staging_type;
	uint32_t texture_type;
	uint32_t uniform_type;
	uint32_t vertex_type;

	uint32_t max_textures;

	struct vulkan_renderpass renderpass;
};

struct vulkan_memory {
	struct wl_list link;
	size_t ref;

	VkDeviceMemory memory;
	uint64_t size;
	void *data;

	bool dedicated;
};

struct vulkan_buffer {
	VkBuffer buffer;
	struct vulkan_memory *mem;
	uint64_t offset;
	uint64_t size;
};

struct vulkan_texture {
	VkImage image;
	VkImageView view;
	struct vulkan_memory *mem;
};

struct vulkan_image {
	VkImage image;
	VkImageView image_view;
	VkFramebuffer framebuffer;

	bool undefined;
};

/* Per-frame resources */
struct vulkan_frame {
	struct wl_list link;

	VkCommandBuffer command_buffer;

	struct vulkan_buffer uniform;
	struct vulkan_buffer vertex;

	VkFence fence;

	VkDescriptorSet desc;
};

struct vulkan_surface {
	struct vulkan *vk;

	VkSurfaceKHR surface;
	VkSwapchainKHR swapchain;

	bool needs_realloc;

	int32_t width;
	int32_t height;

	uint32_t min_images;
	uint32_t num_images;
	struct vulkan_image *images;

	VkDescriptorPool desc_pool;

	VkSemaphore acquire;
	VkSemaphore done;

	struct vulkan_texture *texture;

	struct wl_list frame_res;
};

int
vulkan_create(struct vulkan *vk, struct wl_display *wl);

int
vulkan_surface_init(struct vulkan_surface *surf,
		    struct vulkan *vk,
		    struct wayland_surface *wl_surf);

void
vulkan_surface_resize(struct vulkan_surface *surf, uint32_t w, uint32_t h);

int
vulkan_surface_repaint(struct vulkan_surface *vk_surface, struct scene *scene);

int
vulkan_init_renderpass(struct vulkan *vk,
		       struct vulkan_renderpass *rp);

struct vulkan_texture *
vulkan_texture_create(struct vulkan *vk, int width, int height, int stride,
		      void *data);

int
vulkan_mm_setup_types(struct vulkan *vk);

int
vulkan_mm_alloc_staging_buffer(struct vulkan *vk, struct vulkan_buffer *b,
			       size_t size);
int
vulkan_mm_alloc_uniform_buffer(struct vulkan *vk, struct vulkan_buffer *b,
			       size_t size);
int
vulkan_mm_alloc_vertex_buffer(struct vulkan *vk, struct vulkan_buffer *b,
			      size_t size);

struct vulkan_texture *
vulkan_mm_alloc_texture(struct vulkan *vk, VkFormat format,
			int width, int height, const VkComponentMapping *mapping);

void
vulkan_mm_free_buffer(struct vulkan *vk, struct vulkan_buffer *b);

#endif
