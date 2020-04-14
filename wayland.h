/* SPDX-License-Identifier: MIT */

#ifndef NORI_WAYLAND_H
#define NORI_WAYLAND_H

#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-server-core.h>
#include <wayland-cursor.h>

#include "input-timestamps-unstable-v1-protocol.h"
#include "presentation-time-protocol.h"
#include "xdg-shell-protocol.h"

#include "scene.h"
#include "vulkan.h"

struct wayland_surface;
typedef void (*repaint_fn)(struct wayland_surface *, void *);

struct wayland {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_event_source *source;

	bool exit;

	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wp_presentation *presentation;
	struct xdg_wm_base *wm_base;
	struct zwp_input_timestamps_manager_v1 *input_timestamps_v1;

	struct wl_list seats;

	struct wl_cursor_theme *cursor_theme;
	clockid_t clock_id;
};

struct wayland_surface {
	struct wayland *wl;
	struct wl_surface *surf;

	bool mapped;

	void (*repaint)(struct wayland_surface *, void *);
	void *repaint_priv;

	struct wl_callback *frame;
	struct wl_list feedback; /* struct feedback.link */
	struct timespec predicted_time;
	int64_t latency_ns;
	uint32_t refresh_ns;
};

struct feedback {
	struct wl_list link;

	struct wayland_surface *surf;
	struct wp_presentation_feedback *feedback;

	struct timespec committed;
};

struct wayland_toplevel {
	struct wayland_surface base;
	struct wayland *wl;

	struct scene *scene;
	struct scene_layer *root;
	struct vulkan_surface vk_surf;

	struct xdg_surface *xdg;
	struct xdg_toplevel *toplevel;

	bool close;

	int32_t width;
	int32_t height;

	struct {
		uint32_t serial;
		int32_t width;
		int32_t height;
		bool maximized;
		bool fullscreen;
		bool activated;
	} conf;

	/* temporary testing code */
	struct wl_event_source *timer;
};

struct wayland_cursor {
	struct wayland_surface base;
	struct wayland *wl;

	struct wl_cursor *cursor;

	struct wl_pointer *pointer;
	uint32_t enter_serial;
};

enum {
	POINTER_ENTER = 1 << 0,
	POINTER_LEAVE = 1 << 1,
	POINTER_MOTION = 1 << 2,
};

struct wayland_seat {
	struct wl_list link;
	struct wayland *wl;

	struct wl_seat *seat;
	char *name;

	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;

	struct wayland_cursor *cursor;

	uint32_t fields;

	struct wl_surface *enter_surf;
	double enter_x, enter_y;
	uint32_t enter_serial;

	struct wl_surface *leave_surf;
	uint32_t leave_serial;

	double motion_x, motion_y;
};

int
wayland_connect(struct wayland *wl, struct wl_event_loop *ev);

int
wayland_surface_create(struct wayland *wl, struct wayland_surface *surf);

void
wayland_surface_schedule_repaint(struct wayland_surface *surf);

void
wayland_surface_add_feedback(struct wayland_surface *surf);

struct wayland_toplevel *
wayland_toplevel_create(struct wayland *wl, struct vulkan *vk);

struct wayland_cursor *
wayland_cursor_create(struct wayland *wl);

#endif
