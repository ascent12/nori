/* SPDX-License-Identifier: MIT */

#define _POSIX_C_SOURCE 200809L
#include "wayland.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include "xdg-shell-protocol.h"

static void
pointer_enter(void *data, struct wl_pointer *p,
	      uint32_t serial, struct wl_surface *surface,
	      wl_fixed_t surface_x, wl_fixed_t surface_y)
{
	struct wayland_seat *s = data;

	s->fields |= POINTER_ENTER;
	s->enter_surf = surface;
	s->enter_x = wl_fixed_to_double(surface_x);
	s->enter_y = wl_fixed_to_double(surface_y);
	s->enter_serial = serial;
}

static void
pointer_leave(void *data, struct wl_pointer *p,
	      uint32_t serial, struct wl_surface *surface)
{
	struct wayland_seat *s = data;

	s->fields |= POINTER_LEAVE;
	s->leave_surf = surface;
	s->leave_serial = serial;
}

static void
pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
	       wl_fixed_t surface_x, wl_fixed_t surface_y)
{
	struct wayland_seat *s = data;

	s->fields |= POINTER_MOTION;
	s->motion_x = wl_fixed_to_double(surface_x);
	s->motion_y = wl_fixed_to_double(surface_y);
}

static void
pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
	       uint32_t time, uint32_t button, uint32_t state)
{
}

static void
pointer_axis(void *data, struct wl_pointer *p,
	     uint32_t time, uint32_t axis, wl_fixed_t value)
{
}

static void
pointer_frame(void *data, struct wl_pointer *p)
{
	struct wayland_seat *s = data;

	if (s->fields & POINTER_ENTER) {
		struct wayland_cursor *c = s->cursor;

		c->pointer = p;
		c->enter_serial = s->enter_serial;

		wayland_surface_schedule_repaint(&s->cursor->base);
	}
	if (s->fields & POINTER_LEAVE) {
		s->cursor->base.mapped = false;
	}

	s->fields = 0;
}

static void
pointer_axis_source(void *data, struct wl_pointer *p, uint32_t axis_source)
{
}

static void
pointer_axis_stop(void *data, struct wl_pointer *p,
		  uint32_t time, uint32_t axis)
{
}

static void
pointer_axis_discrete(void *data, struct wl_pointer *p,
		      uint32_t axis, int32_t discrete)
{
}

static struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
};

static void
keyboard_keymap(void *data, struct wl_keyboard *kb,
		uint32_t format, int32_t fd, uint32_t size)
{
}

static void
keyboard_enter(void *data, struct wl_keyboard *kb,
	       uint32_t serial, struct wl_surface *surface,
	       struct wl_array *keys)
{
}

static void
keyboard_leave(void *data, struct wl_keyboard *kb,
	       uint32_t serial, struct wl_surface *surface)
{
}

static void
keyboard_key(void *data, struct wl_keyboard *kb,
	     uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
}

static void
keyboard_modifiers(void *data, struct wl_keyboard *kb,
		   uint32_t serial, uint32_t mods_depressed,
		   uint32_t mods_latched, uint32_t mods_locked,
		   uint32_t group)
{
}

static void
keyboard_repeat_info(void *data, struct wl_keyboard *kb,
		     int32_t rate, int32_t delay)
{
}

static struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

static void
seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
	struct wayland_seat *s = data;

	bool has_pointer = caps & WL_SEAT_CAPABILITY_POINTER;
	if (has_pointer && !s->pointer) {
		s->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(s->pointer, &pointer_listener, s);

		s->cursor = wayland_cursor_create(s->wl);
	} else if (!has_pointer && s->pointer) {
		wl_pointer_destroy(s->pointer);
		s->pointer = NULL;
	}

	bool has_keyboard = caps & WL_SEAT_CAPABILITY_KEYBOARD;
	if (has_keyboard && !s->keyboard) {
		s->keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(s->keyboard, &keyboard_listener, s);
	} else if (!has_keyboard && s->keyboard) {
		wl_keyboard_destroy(s->keyboard);
		s->keyboard = NULL;
	}
}

static void
seat_name(void *data, struct wl_seat *seat, const char *name)
{
	struct wayland_seat *s = data;
	s->name = strdup(name);
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

static void
presentation_clock_id(void *data, struct wp_presentation *p, uint32_t clk_id)
{
	struct wayland *wl = data;
	wl->clock_id = clk_id;
}

static const struct wp_presentation_listener presentation_listener = {
	.clock_id = presentation_clock_id,
};

static void
xdg_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
	xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener xdg_listener = {
	.ping = xdg_ping,
};

static void
global_add(void *data, struct wl_registry *reg, uint32_t name,
	   const char *iface, uint32_t version)
{
	struct wayland *wl = data;

	if (strcmp(iface, wl_compositor_interface.name) == 0) {
		wl->compositor =
			wl_registry_bind(reg, name,
					 &wl_compositor_interface, 4);

	} else if (strcmp(iface, wl_seat_interface.name) == 0) {
		struct wayland_seat *s = calloc(1, sizeof *s);
		if (!s)
			return;

		s->wl = wl;
		s->seat = wl_registry_bind(reg, name, &wl_seat_interface, 7);
		wl_seat_add_listener(s->seat, &seat_listener, s);

		wl_list_insert(&wl->seats, &s->link);

	} else if (strcmp(iface, wl_shm_interface.name) == 0) {
		wl->shm =
			wl_registry_bind(reg, name,
					 &wl_shm_interface, 1);

	} else if (strcmp(iface, wp_presentation_interface.name) == 0) {
		wl->presentation =
			wl_registry_bind(reg, name,
					 &wp_presentation_interface, 1);
		wp_presentation_add_listener(wl->presentation,
					     &presentation_listener, wl);

	} else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
		wl->wm_base =
			wl_registry_bind(reg, name,
					 &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(wl->wm_base, &xdg_listener, NULL);

	} else if (strcmp(iface, zwp_input_timestamps_manager_v1_interface.name) == 0) {
		wl->input_timestamps_v1 =
			wl_registry_bind(reg, name,
					 &zwp_input_timestamps_manager_v1_interface,
					 1);
	}
}

static void
global_remove(void *data, struct wl_registry *reg, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	.global = global_add,
	.global_remove = global_remove,
};

static int
wayland_event(int fd, uint32_t mask, void *data)
{
	struct wayland *wl = data;
	int count = 0;

	if ((mask & WL_EVENT_HANGUP) || (mask & WL_EVENT_ERROR)) {
		wl->exit = true;
		return 0;
	}

	if (mask & WL_EVENT_READABLE)
		count = wl_display_dispatch(wl->display);
	if (mask & WL_EVENT_WRITABLE)
		wl_display_flush(wl->display);

	if (mask == 0) {
		count = wl_display_dispatch_pending(wl->display);
		wl_display_flush(wl->display);
	}

	return count;
}

int
wayland_connect(struct wayland *wl, struct wl_event_loop *ev)
{
	struct wl_registry *registry;
	int fd;
	/* If xcursor_theme == NULL, then the default is loaded */
	const char *xcursor_theme = getenv("XCURSOR_THEME");
	const char *xcursor_size = getenv("XCURSOR_SIZE");
	int cursor_size = 24;

	wl->display = wl_display_connect(NULL);
	if (!wl->display) {
		perror("wl_display_connect");
		return -1;
	}

	registry = wl_display_get_registry(wl->display);
	wl_registry_add_listener(registry, &registry_listener, wl);
	wl_display_roundtrip(wl->display);

	if (!wl->compositor) {
		fprintf(stderr, "wl_compositor: Not supported\n");
		return -1;
	}
	if (!wl->shm) {
		fprintf(stderr, "wl_shm: Not supported\n");
		return -1;
	}
	if (!wl->wm_base) {
		fprintf(stderr, "xdg_wm_base: Not supported\n");
		return -1;
	}

	if (xcursor_size) {
		char *end;
		long n;

		errno = 0;
		n = strtol(xcursor_size, &end, 10);
		if (errno || end == xcursor_size || n < 0 || n > INT_MAX)
			fprintf(stderr, "Invalid XCURSOR_SIZE\n");
		else
			cursor_size = n;
	}

	wl->cursor_theme = wl_cursor_theme_load(xcursor_theme, cursor_size, wl->shm);
	if (!wl->cursor_theme) {
		fprintf(stderr, "Failed to load cursor theme\n");
		return -1;
	}

	fd = wl_display_get_fd(wl->display);
	wl->source = wl_event_loop_add_fd(ev, fd, WL_EVENT_READABLE,
					  wayland_event, wl);
	wl_event_source_check(wl->source);

	return 0;
}
