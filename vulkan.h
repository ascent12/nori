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

	VkDescriptorSetLayout ds_layouts[2];
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

	uint32_t max_textures;

	struct vulkan_renderpass renderpass;
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

	VkDeviceMemory memory;
	VkBuffer vertex_buf;
	VkBuffer index_buf;
	VkBuffer uniform_buf;

	VkFence fence;

	VkDescriptorSet desc[2];
};

struct vulkan_texture {
	VkDeviceMemory memory;
	VkImage image;
	VkImageView view;
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

	VkDescriptorPool desc_pools[2];

	VkSemaphore acquire;
	VkSemaphore done;

	VkSampler sampler;
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

#endif
