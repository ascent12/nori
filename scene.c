/* SPDX-License-Identifier: MIT */

#include "scene.h"

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
	printf("layer, pos %d,%d {\n", l->base.x, l->base.y);

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
