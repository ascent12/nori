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

size_t
scene_get_vertex_size(struct scene *s)
{
	if (!s->root)
		return 0;

	/* 6 verticies with 4 floats each */
	return s->root->decendent_views * 6 * 4;
}

static void
write_node(struct scene_node *n, float *vert, size_t *i, float x, float y);

static void
write_layer(struct scene_layer *l, float *vert, size_t *i, float x, float y)
{
	struct scene_node *n;
	wl_list_for_each(n, &l->children, link) {
		write_node(n, vert, i, x, y);
	}
}

static void
emit_vertex(float *vert, size_t *i,
	    float x, float y, float tex_x, float tex_y)
{
	vert[*i + 0] = x;
	vert[*i + 1] = y;
	vert[*i + 2] = tex_x;
	vert[*i + 3] = tex_y;

	*i += 4;
}

static void
write_view(struct scene_view *v, float *vert, size_t *i, float x, float y)
{
	/* Top left */
	emit_vertex(vert, i, x, y, 0.0f, 0.0f);
	/* Top right */
	emit_vertex(vert, i, x + v->width, y, 1.0f, 0.0f);
	/* Bottom right */
	emit_vertex(vert, i, x + v->width, y + v->height, 1.0f, 1.0f);

	/* Bottom right */
	emit_vertex(vert, i, x + v->width, y + v->height, 1.0f, 1.0f);
	/* Bottom left */
	emit_vertex(vert, i, x, y + v->height, 0.0f, 1.0f);
	/* Top left */
	emit_vertex(vert, i, x, y, 0.0f, 0.0f);
}

static void
write_node(struct scene_node *n, float *vert, size_t *i, float x, float y)
{
	x += n->x;
	y += n->y;

	switch (n->type) {
	case SCENE_NODE_LAYER:
		write_layer((struct scene_layer *)n, vert, i, x, y);
		break;
	case SCENE_NODE_VIEW:
		write_view((struct scene_view *)n, vert, i, x, y);
		break;
	}
}

void
scene_get_vertex_data(struct scene *s, float *vert)
{
	size_t i = 0;
	size_t len = scene_get_vertex_size(s);

	if (!s->root)
		return;

	write_node(s->root, vert, &i, 0.0f, 0.0f);

	assert(i == len);

	printf("===\n");

	float (*vert_data)[8] = (void *)vert;
	for (size_t i = 0; i < len / 8; ++i) {
		for (int j = 0; j < 4; ++j)
			printf("(%.1f, %.1f), ", vert_data[i][j * 2],
			       vert_data[i][j * 2 + 1]);
		printf("\n");
	}

	printf("===\n");
}

static void
for_each_node(struct scene_node *n, scene_iter_fn fn, void *data)
{
	struct scene_layer *l;
	struct scene_node *iter;
	
	switch (n->type) {
	case SCENE_NODE_LAYER:
		l = (struct scene_layer *)n;
		wl_list_for_each(iter, &l->children, link)
			for_each_node(iter, fn, data);
		break;
	case SCENE_NODE_VIEW:
		fn((struct scene_view *)n, data);
		break;
	}
}

void
scene_for_each(struct scene *s, scene_iter_fn fn, void *data)
{
	if (!s->root)
		return;

	for_each_node(s->root, fn, data);
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
