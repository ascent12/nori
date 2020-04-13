/* SPDX-License-Identifier: MIT */

#include "scene.h"

#include <assert.h>
#include <stddef.h>
#include <wayland-util.h>

static void
node_disconnect(struct scene_node *n)
{
	wl_list_remove(&n->link);
	wl_list_init(&n->link);
	n->parent = NULL;
}

static void
node_set_root(struct scene *s, struct scene_node *n)
{
	node_disconnect(n);
	s->root = n;
}

static void
node_push(struct scene_layer *parent, struct scene_node *n)
{
	node_disconnect(n);
	n->parent = parent;
	wl_list_insert(parent->children.prev, &n->link);
}

static void
node_above(struct scene_node *rel, struct scene_node *n)
{
	assert(rel->parent);

	node_disconnect(n);
	n->parent = rel->parent;
	wl_list_insert(&rel->link, &n->link);
}

static void
node_below(struct scene_node *rel, struct scene_node *n)
{
	assert(rel->parent);

	node_disconnect(n);
	n->parent = rel->parent;
	wl_list_insert(rel->link.prev, &n->link);
}

static void
node_set_pos(struct scene_node *n, int x, int y)
{
	n->x = x;
	n->y = y;
}

void
scene_disconnect_view(struct scene_view *v)
{
	node_disconnect(&v->base);
}

void
scene_disconnect_layer(struct scene_layer *l)
{
	node_disconnect(&l->base);
}

void
scene_set_root_view(struct scene *s, struct scene_view *v)
{
	node_set_root(s, &v->base);
}

void
scene_set_root_layer(struct scene *s, struct scene_layer *l)
{
	node_set_root(s, &l->base);
}

void
scene_push_view(struct scene_layer *parent, struct scene_view *v)
{
	node_push(parent, &v->base);
}

void
scene_push_layer(struct scene_layer *parent, struct scene_layer *l)
{
	node_push(parent, &l->base);
}

void
scene_view_above_view(struct scene_view *rel, struct scene_view *v)
{
	node_above(&rel->base, &v->base);
}

void
scene_view_above_layer(struct scene_layer *rel, struct scene_view *v)
{
	node_above(&rel->base, &v->base);
}

void
scene_layer_above_view(struct scene_view *rel, struct scene_layer *l)
{
	node_above(&rel->base, &l->base);
}

void
scene_layer_above_layer(struct scene_layer *rel, struct scene_layer *l)
{
	node_above(&rel->base, &l->base);
}

void
scene_view_below_view(struct scene_view *rel, struct scene_view *v)
{
	node_below(&rel->base, &v->base);
}

void
scene_view_below_layer(struct scene_layer *rel, struct scene_view *v)
{
	node_below(&rel->base, &v->base);
}

void
scene_layer_below_view(struct scene_view *rel, struct scene_layer *l)
{
	node_below(&rel->base, &l->base);
}

void
scene_layer_below_layer(struct scene_layer *rel, struct scene_layer *l)
{
	node_below(&rel->base, &l->base);
}

void
scene_set_pos_view(struct scene_view *v, int x, int y)
{
	node_set_pos(&v->base, x, y);
}

void
scene_set_pos_layer(struct scene_layer *l, int x, int y)
{
	node_set_pos(&l->base, x, y);
}
