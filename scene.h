/* SPDX-License-Identifier: MIT */

#ifndef NORI_SCENE_H
#define NORI_SCENE_H

#include <wayland-util.h>

struct scene_layer;

enum scene_node_type {
	SCENE_NODE_LAYER,
	SCENE_NODE_VIEW,
};

struct scene_node {
	struct wl_list link;
	enum scene_node_type type;

	struct scene_layer *parent;
	/* Always == 1 for views, representing itself */
	int decendent_views;

	int x;
	int y;
};

struct scene_layer {
	struct scene_node base;

	struct wl_list children; /* scene_node.link */
};

struct scene_view {
	struct scene_node base;

	/* TODO: Replace these with some texture type */
	int width;
	int height;
};

struct scene {
	struct scene_node *root;
};

struct scene *
scene_create(void);

void
scene_destroy(struct scene *s);

void
scene_get_vertex_index_sizes(struct scene *s, size_t *vert, size_t *index);
void
scene_get_vertex_index_data(struct scene *s, float *vert, uint16_t *index);

struct scene_layer *
scene_layer_create(void);

struct scene_view *
scene_view_create(int width, int height);

void
scene_dump(struct scene *s);

/* Scene operations */

void
scene_disconnect_view(struct scene_view *v);
void
scene_disconnect_layer(struct scene_layer *l);

void
scene_set_root_view(struct scene *s, struct scene_view *v);
void
scene_set_root_layer(struct scene *s, struct scene_layer *l);

void
scene_push_view(struct scene_layer *parent, struct scene_view *v);
void
scene_push_layer(struct scene_layer *parent, struct scene_layer *l);

void
scene_view_above_view(struct scene_view *rel, struct scene_view *v);
void
scene_view_above_layer(struct scene_layer *rel, struct scene_view *v);

void
scene_layer_above_view(struct scene_view *rel, struct scene_layer *l);
void
scene_layer_above_layer(struct scene_layer *rel, struct scene_layer *l);

void
scene_view_below_view(struct scene_view *rel, struct scene_view *v);
void
scene_view_below_layer(struct scene_layer *rel, struct scene_view *v);

void
scene_layer_below_view(struct scene_view *rel, struct scene_layer *l);
void
scene_layer_below_layer(struct scene_layer *rel, struct scene_layer *l);

void
scene_set_pos_view(struct scene_view *v, int x, int y);
void
scene_set_pos_layer(struct scene_layer *l, int x, int y);

#define scene_disconnect(n) _Generic((n), \
	struct scene_view *: scene_disconnect_view(n), \
	struct scene_layer *: scene_disconnect_layer(n))

#define scene_set_root(s, n) _Generic((n), \
	struct scene_view *: scene_set_root_view, \
	struct scene_layer *: scene_set_root_layer)((s), (n))

#define scene_push(parent, n) _Generic((n), \
	struct scene_view *: scene_push_view, \
	struct scene_layer *: scene_push_layer)((parent), (n))

#define scene_view_above(n, v) _Generic((n), \
	struct scene_view *: scene_view_above_view, \
	struct scene_layer *: scene_view_above_layer)((n), (v))

#define scene_layer_above(n, l) _Generic((n), \
	struct scene_view *: scene_layer_above_view, \
	struct scene_layer *: scene_layer_above_layer)((n), (l))

#define scene_above(rel, n) _Generic((n), \
	struct scene_view *: scene_view_above, \
	struct scene_layer *: scene_layer_above)((rel), (n))

#define scene_view_below(n, v) _Generic((n), \
	struct scene_view *: scene_view_below_view, \
	struct scene_layer *: scene_view_below_layer)((n), (v))

#define scene_layer_below(n, l) _Generic((n), \
	struct scene_view *: scene_layer_below_view, \
	struct scene_layer *: scene_layer_below_layer)((n), (l))

#define scene_below(rel, n) _Generic((n), \
	struct scene_view *: scene_view_below, \
	struct scene_layer *: scene_layer_below)((rel), (n))

#define scene_set_pos(n, x, y) _Generic((n), \
	struct scene_view *: scene_set_pos_view, \
	struct scene_layer *: scene_set_pos_layer)((n), (x), (y))

#endif
