#include "vulkan.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))

/*
 * Vulkan implementations are supposed to order the memory types based on what
 * they think is the most efficient, which this takes advantage of.
 */

/*
 * Used by the "staging" type.
 * Guranteed that at least one of these exits. This may be uncached, which
 * doesn't matter, since we aren't reading out of it, and could even be
 * preferable, because the driver could do write combining.
 */
static const uint32_t staging_reqs = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

/*
 * Used by both "uniform" and "texture" types.
 * Whatever is the most efficient; we don't care.
 */
static const uint32_t vram_reqs = 0;

/* Used by both "vertex" and "index" types */
static const uint32_t stream_reqs[] = {
	/*
	 * Best if no extra copying steps needs to happen.
	 * AMD has some special "streaming" type that satisfies this.
	 */
	VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
	/*
	 * Prefer the GPU can just read from the CPU instead of transferring,
	 * since we're only using it once.
	 */
	VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
	/*
	 * Just take what we can get; we'll have to transfer from a staging
	 * buffer.
	 */
	0,
};

/* Select the best memory type based on how we indend to use it. */
static uint32_t
get_type_index(const VkPhysicalDeviceMemoryProperties *props,
	       const VkMemoryRequirements *mem,
	       size_t num_reqs, const uint32_t reqs[static num_reqs])
{
	for (size_t i = 0; i < num_reqs; ++i) {
		for (uint32_t j = 0; j < props->memoryTypeCount; ++j) {
			uint32_t flags = props->memoryTypes[j].propertyFlags;

			/* Not compatible with buffer/image */
			if (!(mem->memoryTypeBits & (1 << j)))
				continue;

			/* Not compatible with our requirements */
			if ((flags & reqs[i]) != reqs[i])
				continue;

			return j;
		}
	}

	abort();
}

static int
get_buf_type(struct vulkan *vk, const VkPhysicalDeviceMemoryProperties *props,
	     size_t num_reqs, const uint32_t reqs[static num_reqs],
	     uint32_t usage, uint32_t *index)
{
	VkResult res;
	VkBuffer dummy_buf;
	VkMemoryRequirements req;
	/* See vulkan_mm_setup_types comment */
	VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.flags = 0,
		.size = 1,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	res = vkCreateBuffer(vk->logical_device, &buffer_info, NULL, &dummy_buf);
	if (res < 0)
		return -1;

	vkGetBufferMemoryRequirements(vk->logical_device, dummy_buf, &req);
	*index = get_type_index(props, &req, num_reqs, reqs);
	vkDestroyBuffer(vk->logical_device, dummy_buf, NULL);

	return 0;
}

int
vulkan_mm_setup_types(struct vulkan *vk)
{
	VkResult res;
	VkPhysicalDeviceMemoryProperties props;
	VkMemoryRequirements req;
	VkImage dummy_img;

	/*
	 * From the Vulkan spec:
	 * 
	 * - The memoryTypeBits member is identical for all VkBuffer objects
	 *   created with the same value for the flags and usage members in the
	 *   VkBufferCreateInfo structure [...] passed to vkCreateBuffer.
	 * 
	 * - For images created with a color format, the memoryTypeBits member
	 *   is idential for all VkImage objects created with the same
	 *   combination of values for the tiling member [...] in the
	 *   VkImageCreate structure passed to vkCreateImage.
	 *
	 * with the redacted parts being features we don't use/change.
	 *
	 * Basically, that means we can create these dummy buffers and figure
	 * out and set up memory heaps ahead of time, rather than after each
	 * allocation, because we know exactly what kinds of allocations we're
	 * doing.
	 *
	 * As such, most of the fields in these info structures are not
	 * important, except for the ones mentioned above. We just need
	 * something valid.
	 */
	VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.flags = 0,
		.imageType = VK_IMAGE_TYPE_2D,
		/* Format guaranteed to be supported */
		.format = VK_FORMAT_R8_UNORM,
		.extent.width = 1,
		.extent.height = 1,
		.extent.depth = 1,
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	vkGetPhysicalDeviceMemoryProperties(vk->physical_device, &props);

	if (get_buf_type(vk, &props, 1, &staging_reqs,
			 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			 &vk->staging_type) < 0)
		return -1;

	if (get_buf_type(vk, &props, ARRAY_LEN(stream_reqs), stream_reqs,
			 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			 &vk->uniform_type) < 0)
		return -1;

	if (get_buf_type(vk, &props, ARRAY_LEN(stream_reqs), stream_reqs,
			 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			 &vk->vertex_type) < 0)
		return -1;

	res = vkCreateImage(vk->logical_device, &image_info, NULL, &dummy_img);
	if (res < 0)
		goto err;
	vkGetImageMemoryRequirements(vk->logical_device, dummy_img, &req);
	vk->texture_type = get_type_index(&props, &req, 1, &vram_reqs);
	vkDestroyImage(vk->logical_device, dummy_img, NULL);

	printf("- Staging type: %u\n", vk->staging_type);
	printf("- Texture type: %u\n", vk->texture_type);
	printf("- Uniform type: %u\n", vk->uniform_type);
	printf("- Vertex type: %u\n", vk->vertex_type);

	return 0;

err:
	return -1;
}

static int
create_buffer(struct vulkan *vk, struct vulkan_buffer *b,
	      size_t size, VkBufferUsageFlags usage)
{
	VkResult res;
	const VkBufferCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	res = vkCreateBuffer(vk->logical_device, &info, NULL, &b->buffer);
	if (res < 0) {
		fprintf(stderr, "vkCreateBuffer: 0x%x\n", res);
		return -1;
	}

	return 0;
}

static int
bind_buffer(struct vulkan *vk, struct vulkan_buffer *b)
{
	VkResult res;
	const VkBindBufferMemoryInfo info = {
		.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
		.buffer = b->buffer,
		.memory = b->mem->memory,
		.memoryOffset = b->offset,
	};

	res = vkBindBufferMemory2(vk->logical_device, 1, &info);
	if (res < 0) {
		fprintf(stderr, "vkBindBufferMemory2: 0x%x\n", res);
		return -1;
	}

	return 0;
}

enum {
	ALLOC_NO_MAP = 1 << 0,
};

static struct vulkan_memory *
allocate_memory(struct vulkan *vk, const VkMemoryRequirements2 *req,
		int index, unsigned opts)
{
	VkResult res;
	struct vulkan_memory *m;
	VkPhysicalDeviceMemoryProperties props;
	uint32_t flags;
	const VkMemoryAllocateInfo info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = req->memoryRequirements.size,
		.memoryTypeIndex = index,
	};

	m = calloc(1, sizeof *m);
	if (!m)
		return NULL;

	wl_list_init(&m->link);
	m->ref = 1;
	m->size = req->memoryRequirements.size;

	res = vkAllocateMemory(vk->logical_device, &info, NULL, &m->memory);
	if (res < 0) {
		fprintf(stderr, "vkAllocateMemory: 0x%x\n", res);
		goto err_free;
	}

	/* Always map memory if it's mappable */

	vkGetPhysicalDeviceMemoryProperties(vk->physical_device, &props);
	flags = props.memoryTypes[index].propertyFlags;

	if (!(opts & ALLOC_NO_MAP) && (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
		res = vkMapMemory(vk->logical_device, m->memory, 0, m->size, 0,
				  &m->data);
		if (res < 0) {
			fprintf(stderr, "vkMapMemory: 0x%x\n", res);
			goto err_mem;
		}
	}

	return m;

err_mem:
	vkFreeMemory(vk->logical_device, m->memory, NULL);
err_free:
	free(m);
	return NULL;
}

static void
free_memory(struct vulkan *vk, struct vulkan_memory *m)
{
	vkUnmapMemory(vk->logical_device, m->memory);
	vkFreeMemory(vk->logical_device, m->memory, NULL);
	free(m);
}

static int
alloc_buffer(struct vulkan *vk, struct vulkan_buffer *b, size_t size,
	     int index, VkBufferUsageFlags usage)
{
	VkMemoryRequirements2 req = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
	};

	if (create_buffer(vk, b, size, usage) < 0)
		return -1;

	const VkBufferMemoryRequirementsInfo2 info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
		.buffer = b->buffer,
	};

	vkGetBufferMemoryRequirements2(vk->logical_device, &info, &req);

	b->mem = allocate_memory(vk, &req, index, 0);
	if (!b->mem)
		goto err_buf;

	b->size = size;
	b->offset = 0;

	if (bind_buffer(vk, b) < 0)
		goto err_mem;

	return 0;

err_mem:
	free_memory(vk, b->mem);
err_buf:
	vkDestroyBuffer(vk->logical_device, b->buffer, NULL);
	b->buffer = VK_NULL_HANDLE;
	return -1;
}

int
vulkan_mm_alloc_staging_buffer(struct vulkan *vk, struct vulkan_buffer *b,
			       size_t size)
{
	return alloc_buffer(vk, b, size, vk->staging_type,
			    VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
}

int
vulkan_mm_alloc_uniform_buffer(struct vulkan *vk, struct vulkan_buffer *b,
			       size_t size)
{
	return alloc_buffer(vk, b, size, vk->uniform_type,
			    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
}

int
vulkan_mm_alloc_vertex_buffer(struct vulkan *vk, struct vulkan_buffer *b,
			      size_t size)
{
	return alloc_buffer(vk, b, size, vk->vertex_type,
			    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

struct vulkan_texture *
vulkan_mm_alloc_texture(struct vulkan *vk, VkFormat format,
			int width, int height, const VkComponentMapping *mapping)
{
	VkResult res;
	struct vulkan_texture *t;
	VkMemoryRequirements2 req = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
	};

	const VkImageCreateInfo img_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent.width = width,
		.extent.height = height,
		.extent.depth = 1,
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
			 VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	t = calloc(1, sizeof *t);
	if (!t)
		return NULL;
	
	res = vkCreateImage(vk->logical_device, &img_info, NULL, &t->image);
	if (res < 0) {
		fprintf(stderr, "vkCreateImage: 0x%x\n", res);
		goto err_free;
	}

	const VkImageMemoryRequirementsInfo2 req_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
		.image = t->image,
	};

	vkGetImageMemoryRequirements2(vk->logical_device, &req_info, &req);

	/*
	 * We don't map textures, because we can't usefully write data to it
	 * unless it's VK_IMAGE_TILING_LINEAR. But that has its own weirdness
	 * with transitioning to VK_IMAGE_TILING_OPTIMAL, so we just always
	 * go through a staging buffer and perform a transfer command.
	 */
	t->mem = allocate_memory(vk, &req, vk->texture_type, ALLOC_NO_MAP);
	if (!t->mem)
		goto err_img;

	const VkBindImageMemoryInfo bind_info = {
		.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
		.image = t->image,
		.memory = t->mem->memory,
		.memoryOffset = 0,
	};

	res = vkBindImageMemory2(vk->logical_device, 1, &bind_info);
	if (res < 0) {
		fprintf(stderr, "vkBindMemoryInfo: 0x%x\n", res);
		goto err_mem;
	}

	const VkImageViewCreateInfo view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = t->image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = format,
		.components = *mapping,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	res = vkCreateImageView(vk->logical_device, &view_info, NULL, &t->view);
	if (res < 0) {
		fprintf(stderr, "vkCreateImageView: 0x%x\n", res);
		goto err_mem;
	}


	return t;

err_mem:
	free_memory(vk, t->mem);
err_img:
	vkDestroyImage(vk->logical_device, t->image, NULL);
err_free:
	free(t);
	return NULL;
}

void
vulkan_mm_free_buffer(struct vulkan *vk, struct vulkan_buffer *b)
{
	vkDestroyBuffer(vk->logical_device, b->buffer, NULL);
	free_memory(vk, b->mem);

	b->buffer = VK_NULL_HANDLE;
	b->mem = NULL;
	b->offset = 0;
	b->size = 0;
}
