#include "scene.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-util.h>

struct scene *
scene_create(void)
{
	struct scene *s = calloc(1, sizeof *s);
	if (!s) {
		fprintf(stderr, "calloc: %s\n", strerror(errno));
		return NULL;
	}

	return s;
}

void
scene_destroy(struct scene *s)
{
	free(s);
}

void
scene_get_vertex_index_sizes(struct scene *s, size_t *vert, size_t *index)
{
	if (!s->root) {
		*vert = 0;
		*index = 0;
		return;
	}

	/* 4 corners of x+y */
	*vert = s->root->decendent_views * 8;
	/* 2 triangles */
	*index = s->root->decendent_views * 6;
}

static void
write_node(struct scene_node *n, float *vert, uint16_t *index,
	   size_t *vert_i, size_t *index_i, float x, float y);

static void
write_layer(struct scene_layer *l, float *vert, uint16_t *index,
	    size_t *vert_i, size_t *index_i, float x, float y)
{
	struct scene_node *n;
	wl_list_for_each(n, &l->children, link) {
		write_node(n, vert, index, vert_i, index_i, x, y);
	}
}

static void
write_view(struct scene_view *v, float *vert, uint16_t *index,
	   size_t *vert_i, size_t *index_i, float x, float y)
{
	// Top left
	vert[*vert_i + 0] = x;
	vert[*vert_i + 1] = y;
	// Top right
	vert[*vert_i + 2] = x + v->width;
	vert[*vert_i + 3] = y;
	// Bottom right
	vert[*vert_i + 4] = x + v->width;
	vert[*vert_i + 5] = y + v->height;
	// Bottom left
	vert[*vert_i + 6] = x;
	vert[*vert_i + 7] = y + v->height;

	index[*index_i + 0] = *vert_i / 2 + 0;
	index[*index_i + 1] = *vert_i / 2 + 1;
	index[*index_i + 2] = *vert_i / 2 + 2;
	index[*index_i + 3] = *vert_i / 2 + 0;
	index[*index_i + 4] = *vert_i / 2 + 2;
	index[*index_i + 5] = *vert_i / 2 + 3;

	*vert_i += 8;
	*index_i += 6;
}

static void
write_node(struct scene_node *n, float *vert, uint16_t *index,
	   size_t *vert_i, size_t *index_i, float x, float y)
{
	x += n->x;
	y += n->y;

	switch (n->type) {
	case SCENE_NODE_LAYER:
		write_layer((struct scene_layer *)n, vert, index,
			    vert_i, index_i, x, y);
		break;
	case SCENE_NODE_VIEW:
		write_view((struct scene_view *)n, vert, index,
			    vert_i, index_i, x, y);
		break;
	}
}

void
scene_get_vertex_index_data(struct scene *s, float *vert, uint16_t *index)
{
	size_t vert_i = 0, index_i = 0;
	size_t vert_len, index_len;
	scene_get_vertex_index_sizes(s, &vert_len, &index_len);

	if (!s->root)
		return;

	write_node(s->root, vert, index, &vert_i, &index_i, 0.0f, 0.0f);

	assert(vert_i == vert_len && index_i == index_len);

	printf("===\n");

	float (*vert_data)[8] = (void *)vert;
	for (size_t i = 0; i < vert_len / 8; ++i) {
		for (int j = 0; j < 4; ++j)
			printf("(%.1f, %.1f), ", vert_data[i][j * 2],
			       vert_data[i][j * 2 + 1]);
		printf("\n");
	}

	printf("===\n");

	uint16_t (*index_data)[6] = (void *)index;
	for (size_t i = 0; i < index_len / 6; ++i) {
		for (int j = 0; j < 6; ++j)
			printf("%hu, ", index_data[i][j]);
		printf("\n");
	}

	printf("===\n");
}

struct scene_layer *
scene_layer_create(void)
{
	struct scene_layer *l = calloc(1, sizeof *l);
	if (!l) {
		fprintf(stderr, "calloc: %s\n", strerror(errno));
		return NULL;
	}

	wl_list_init(&l->base.link);
	l->base.type = SCENE_NODE_LAYER;
	l->base.decendent_views = 0;
	wl_list_init(&l->children);

	return l;
}

struct scene_view *
scene_view_create(int width, int height)
{
	struct scene_view *v = calloc(1, sizeof *v);
	if (!v) {
		fprintf(stderr, "calloc: %s\n", strerror(errno));
		return NULL;
	}

	wl_list_init(&v->base.link);
	v->base.type = SCENE_NODE_VIEW;
	v->base.decendent_views = 1;
	v->width = width;
	v->height = height;

	return v;
}

static void
dump_node(struct scene_node *n, int depth);

static void
dump_layer(struct scene_layer *l, int depth)
{
	for (int i = 0; i < depth; ++i)
		printf("  ");
	printf("layer, pos %d,%d, dec: %d {\n", l->base.x, l->base.y,
		l->base.decendent_views);

	struct scene_node *n;
	wl_list_for_each(n, &l->children, link) {
		dump_node(n, depth + 1);
	}

	for (int i = 0; i < depth; ++i)
		printf("  ");
	printf("}\n"); 
}

static void
dump_view(struct scene_view *v, int depth)
{
	for (int i = 0; i < depth; ++i)
		printf("  ");
	printf("view, pos %d,%d, dim %dx%d\n",
		v->base.x, v->base.y,
		v->width, v->height);
}

static void
dump_node(struct scene_node *n, int depth)
{
	switch (n->type) {
	case SCENE_NODE_LAYER:
		dump_layer((struct scene_layer *)n, depth);
		break;
	case SCENE_NODE_VIEW:
		dump_view((struct scene_view *)n, depth);
		break;
	}
}

void
scene_dump(struct scene *s)
{
	if (!s->root)
		return;
	dump_node(s->root, 0);
}
