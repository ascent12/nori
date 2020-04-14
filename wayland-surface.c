/* SPDX-License-Identifier: MIT */

#include "wayland.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include "xdg-shell-protocol.h"
#include "timespec-util.h"

#include "vulkan.h"

static void
wayland_surface_repaint(struct wayland_surface *surf)
{
	/*
	 * No fancy frame prediction without this.
	 * Just target the current time.
	 */
	if (!surf->wl->presentation) {
		clock_gettime(CLOCK_MONOTONIC, &surf->predicted_time);

	} else {
		clock_gettime(surf->wl->clock_id, &surf->predicted_time);
		printf("Current time:   %ld:%09ld\n",
			surf->predicted_time.tv_sec,
			surf->predicted_time.tv_nsec);
		timespec_add_nsec(&surf->predicted_time,
				  &surf->predicted_time, surf->latency_ns);
		printf("Predicted time: %ld:%09ld\n",
			surf->predicted_time.tv_sec,
			surf->predicted_time.tv_nsec);
	}

	surf->repaint(surf, surf->repaint_priv);
}

static void
frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
	struct wayland_surface *surf = data;
	wl_callback_destroy(surf->frame);
	surf->frame = NULL;

	wayland_surface_repaint(surf);
}

static const struct wl_callback_listener frame_listener = {
	.done = frame_done,
};

static void
feedback_destroy(struct feedback *fb)
{
	wl_list_remove(&fb->link);
	free(fb);
}

static void
feedback_sync_output(void *data, struct wp_presentation_feedback *f,
		     struct wl_output *output)
{
	/* Don't care */
}
	
static void
feedback_presented(void *data, struct wp_presentation_feedback *f,
		   uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec,
		   uint32_t refresh, uint32_t seq_hi, uint32_t seq_lo,
		   uint32_t flags)
{
	struct feedback *fb = data;
	struct wayland_surface *surf = fb->surf;
	struct timespec presented;

	timespec_from_proto(&presented, tv_sec_hi, tv_sec_lo, tv_nsec);
	surf->latency_ns = timespec_sub_to_nsec(&presented, &fb->committed);
	surf->refresh_ns = refresh;

	printf("Took %ld ms\n", surf->latency_ns / 1000000);

	feedback_destroy(fb);
}

static void 
feedback_discarded(void *data, struct wp_presentation_feedback *f)
{
	struct feedback *fb = data;
	printf("discarded\n");
	feedback_destroy(fb);
}

static const struct wp_presentation_feedback_listener feedback_listener = {
	.sync_output = feedback_sync_output,
	.presented = feedback_presented,
	.discarded = feedback_discarded,
};

void
wayland_surface_add_feedback(struct wayland_surface *surf)
{
	struct wayland *wl = surf->wl;
	struct feedback *fb;

	if (!wl->presentation)
		return;

	fb = calloc(1, sizeof *fb);
	if (!fb)
		return;

	fb->surf = surf;
	fb->feedback = wp_presentation_feedback(wl->presentation, surf->surf);
	clock_gettime(wl->clock_id, &fb->committed);

	wp_presentation_feedback_add_listener(fb->feedback, &feedback_listener, fb);

	wl_list_insert(&surf->feedback, &fb->link);
}

void
wayland_surface_schedule_repaint(struct wayland_surface *surf)
{
	/*
	 * If we're not mapped (visible), the compositor isn't going to send us
	 * frame events. Draw something right now to get it all started.
	 */
	if (!surf->mapped) {
		surf->mapped = true;
		wayland_surface_repaint(surf);
		return;
	}

	if (surf->frame)
		return;

	surf->frame = wl_surface_frame(surf->surf);
	wl_callback_add_listener(surf->frame, &frame_listener, surf);

	wl_surface_commit(surf->surf);
}

static void
wayland_surface_init(struct wayland_surface *surf, struct wayland *wl,
		     repaint_fn repaint, void *data)
{
	surf->wl = wl;
	surf->repaint = repaint;
	surf->repaint_priv = data;

	wl_list_init(&surf->feedback);

	surf->surf = wl_compositor_create_surface(wl->compositor);
}

static void
wayland_toplevel_repaint(struct wayland_surface *surf, void *data)
{
	struct wayland_toplevel *top = data;

	if (top->conf.serial) {
		xdg_surface_ack_configure(top->xdg, top->conf.serial);
		top->conf.serial = 0;
	}

	wayland_surface_add_feedback(&top->base);
	vulkan_surface_repaint(&top->vk_surf, top->scene);
}

static void
xdg_configure(void *data, struct xdg_surface *xdg, uint32_t serial)
{
	struct wayland_toplevel *top = data;
	top->conf.serial = serial;

	if (top->conf.width == 0)
		top->conf.width = 500;
	if (top->conf.height == 0)
		top->conf.height = 500;

	if (top->vk_surf.width != top->conf.width ||
	    top->vk_surf.height != top->conf.height) {

		vulkan_surface_resize(&top->vk_surf,
				      top->conf.width,
				      top->conf.height);
	}

	if (top->base.mapped)
		wayland_surface_schedule_repaint(&top->base);
}

static const struct xdg_surface_listener xdg_listener = {
	.configure = xdg_configure,
};

static void
toplevel_configure(void *data, struct xdg_toplevel *toplevel,
		   int32_t width, int32_t height,
		   struct wl_array *states)
{
	struct wayland_toplevel *top = data;
	enum xdg_toplevel_state *st;

	top->conf.width = width;
	top->conf.height = height;

	top->conf.maximized = false;
	top->conf.fullscreen = false;
	top->conf.activated = false;

	wl_array_for_each(st, states) {
		switch (*st) {
		case XDG_TOPLEVEL_STATE_MAXIMIZED:
			top->conf.maximized = true;
			break;
		case XDG_TOPLEVEL_STATE_FULLSCREEN:
			top->conf.fullscreen = true;
			break;
		case XDG_TOPLEVEL_STATE_ACTIVATED:
			top->conf.activated = true;
			break;
		default:
			break;
		}
	}
}

static void
toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
	struct wayland_toplevel *top = data;
	top->close = true;
}

static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure,
	.close = toplevel_close,
};

struct wayland_toplevel *
wayland_toplevel_create(struct wayland *wl, struct vulkan *vk)
{
	struct wayland_toplevel *top;

	top = calloc(1, sizeof *top);
	if (!top)
		return NULL;

	wayland_surface_init(&top->base, wl, wayland_toplevel_repaint, top);

	top->scene = scene_create();
	top->root = scene_layer_create();
	scene_set_root(top->scene, top->root);

	if (vulkan_surface_init(&top->vk_surf, vk, &top->base) < 0)
		goto error;

	top->xdg = xdg_wm_base_get_xdg_surface(wl->wm_base, top->base.surf);
	top->toplevel = xdg_surface_get_toplevel(top->xdg);

	xdg_surface_add_listener(top->xdg, &xdg_listener, top);
	xdg_toplevel_add_listener(top->toplevel, &toplevel_listener, top);

	xdg_toplevel_set_title(top->toplevel, "nori");
	xdg_toplevel_set_app_id(top->toplevel, "nori");

	wl_surface_commit(top->base.surf);
	wl_display_roundtrip(wl->display);

	return top;

error:
	wl_surface_destroy(top->base.surf);
	free(top);
	return NULL;
}

static void
wayland_cursor_repaint(struct wayland_surface *s, void *data)
{
	struct wayland_cursor *c = data;
	struct wl_cursor_image *img;
	struct wl_buffer *buf;
	int64_t time = timespec_to_msec(&s->predicted_time);
	uint32_t duration;
	int frame;

	frame = wl_cursor_frame_and_duration(c->cursor, time, &duration);
	img = c->cursor->images[frame];

	printf("Frame: %d, Duration: %u\n", frame, duration);

	buf = wl_cursor_image_get_buffer(img);

	if (c->pointer) {
		wl_pointer_set_cursor(c->pointer, c->enter_serial,
			s->surf, img->hotspot_x, img->hotspot_y);

		c->pointer = NULL;
		c->enter_serial = 0;
	}

	wl_surface_attach(s->surf, buf, 0, 0);
	wl_surface_damage_buffer(s->surf, 0, 0, INT32_MAX, INT32_MAX);

	wayland_surface_add_feedback(s);
	if (duration != 0)
		wayland_surface_schedule_repaint(s);
	else
		wl_surface_commit(s->surf);
}

struct wayland_cursor *
wayland_cursor_create(struct wayland *wl)
{
	struct wayland_cursor *c;
	
	c = calloc(1, sizeof *c);
	if (!c)
		return NULL;

	c->wl = wl;
	wayland_surface_init(&c->base, wl, wayland_cursor_repaint, c);

	c->cursor = wl_cursor_theme_get_cursor(wl->cursor_theme, "progress");

	return c;
}
