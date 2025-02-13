#include <stdlib.h>
#include <xkbcommon/xkbcommon.h>

#include "input.h"
#include "layout.h"
#include "server.h"

/* Handle modifier keys, e.g Alt, Ctrl, Shift */
void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
	struct flui_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	/* Send modifiers to the client */
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->wlr_keyboard->modifiers);
}

/* Handle compositor keybindings (assuming Alt pressed) */
bool handle_keybinding(struct flui_server *server, xkb_keysym_t sym) {
	switch (sym) {
		case XKB_KEY_Escape:
			wl_display_terminate(server->wl_display);
			break;
		case XKB_KEY_Tab:
			/* Cycle to the next toplevel */
			if (server->sw_toplevels->size < 2) {
				break;
			}
			if (!server->sw_location) {
				server->sw_location = server->sw_toplevels->head;
			}
			server->sw_location = server->sw_location->next;
			if (!server->sw_location) {
				server->sw_location = server->sw_toplevels->head;
			}
			focus_toplevel(server->sw_location->data);
			break;
		default:
			return false;
	}
	return true;
}

/* Handle key press/release */
void keyboard_handle_key(struct wl_listener *listener, void *data) {
	struct flui_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct flui_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = server->seat;

	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

	for (int i = 0; i < nsyms; i++) {
		xkb_keysym_t sym = syms[i];
		if ((sym == XKB_KEY_Alt_L || sym == XKB_KEY_Alt_R) && event->state == WL_KEYBOARD_KEY_STATE_RELEASED && server->sw_location) {
			struct flui_toplevel *t = server->sw_location->data;
			if (pointer_list_remove(server->sw_toplevels, t)) {
				assert(pointer_list_add_to_head(server->sw_toplevels, t));
			}
			server->sw_location = NULL;
		}
	}

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	if ((modifiers & WLR_MODIFIER_ALT) && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		/* Process compositor keybindings */
		for (int i = 0; i < nsyms; i++) {
			handled = handle_keybinding(server, syms[i]);
		}
	}

	if (!handled) {
		/* Pass keypress to client */
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
	}
}

/* Handle keyboard destruction */
void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
	struct flui_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

/* Handle new keyboards */
static void server_new_keyboard(struct flui_server *server, struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct flui_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	/* Assign keymap (default loaded from environment variable XKB_DEFAULT_LAYOUT) */
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	/* Set up listeners for keyboard events */
	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

	/* Add the keyboard to the server's list of keyboards */
	wl_list_insert(&server->keyboards, &keyboard->link);
}

/* Handle new pointers (i.e. mice) */
static void server_new_pointer(struct flui_server *server, struct wlr_input_device *device) {
	wlr_cursor_attach_input_device(server->cursor, device);
}

/* Handle new input devices (keyboards, mice, etc.) */
void server_new_input(struct wl_listener *listener, void *data) {
	struct flui_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	default:
		break;
	}
	/* Enable pointer & keyboard capability */
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

/* Handle clients providing custom cursor images */
void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct flui_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client =
	server->seat->pointer_state.focused_client;
	/* Check if client is in focus */
	if (focused_client == event->seat_client) {
		/* Set cursor to use provided surface */
		wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
	}
}

/* Handle selection */
void seat_request_set_selection(struct wl_listener *listener, void *data) {
	struct flui_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

/* Reset the cursor mode to passthrough */
void reset_cursor_mode(struct flui_server *server) {
	server->cursor_mode = FLUI_CURSOR_PASSTHROUGH;
	server->grabbed_toplevel = NULL;
}

/* Return toplevel node at given layout position */
static struct flui_toplevel *desktop_toplevel_at(struct flui_server *server, double lx, double ly, struct wlr_surface **surface, double *sx, double *sy) {
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface =
	wlr_scene_surface_try_from_buffer(scene_buffer);
	if (!scene_surface) {
		return NULL;
	}

	*surface = scene_surface->surface;
	/* Find the node at toplevel */
	struct wlr_scene_tree *tree = node->parent;
	while (tree != NULL && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	return tree->node.data;
}

/* Move grabbed window */
static void process_cursor_move(struct flui_server *server) {
	struct flui_toplevel *toplevel = server->grabbed_toplevel;
	wlr_scene_node_set_position(&toplevel->scene_tree->node, server->cursor->x - server->grab_x, server->cursor->y - server->grab_y);
}

/* Resize grabbed window */
static void process_cursor_resize(struct flui_server *server) {
	struct flui_toplevel *toplevel = server->grabbed_toplevel;
	double border_x = server->cursor->x - server->grab_x;
	double border_y = server->cursor->y - server->grab_y;
	int new_left = server->grab_geobox.x;
	int new_right = server->grab_geobox.x + server->grab_geobox.width;
	int new_top = server->grab_geobox.y;
	int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

	if (server->resize_edges & WLR_EDGE_TOP) {
		new_top = border_y;
		if (new_top >= new_bottom) {
			new_top = new_bottom - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
		new_bottom = border_y;
		if (new_bottom <= new_top) {
			new_bottom = new_top + 1;
		}
	}
	if (server->resize_edges & WLR_EDGE_LEFT) {
		new_left = border_x;
		if (new_left >= new_right) {
			new_left = new_right - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_RIGHT) {
		new_right = border_x;
		if (new_right <= new_left) {
			new_right = new_left + 1;
		}
	}

	struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;
	wlr_scene_node_set_position(&toplevel->scene_tree->node, new_left - geo_box->x, new_top - geo_box->y);

	int new_width = new_right - new_left;
	int new_height = new_bottom - new_top;
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
}

/* Process moving the cursor */
static void process_cursor_motion(struct flui_server *server, uint32_t time) {
	if (server->cursor_mode == FLUI_CURSOR_MOVE) {
		process_cursor_move(server);
		return;
	} else if (server->cursor_mode == FLUI_CURSOR_RESIZE) {
		process_cursor_resize(server);
		return;
	}

	/* Send event to toplevel */
	double sx, sy;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *surface = NULL;
	struct flui_toplevel *toplevel = desktop_toplevel_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (!toplevel) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
	if (surface) {
		/* Send pointer and motion events */
		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);
	} else {
		/* Clear pointer focus */
		wlr_seat_pointer_clear_focus(seat);
	}
}

/* Handle relative (delta) pointer movement */
void server_cursor_motion(struct wl_listener *listener, void *data) {
	struct flui_server *server =
	wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	/* Move cursor */
	wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

/* Handle absolute pointer movement (jumps) */
void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	struct flui_server *server =
	wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

/* Handle cursor button presses */
void server_cursor_button(struct wl_listener *listener, void *data) {
	struct flui_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	/* Notify focused client of button press */
	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		/* Exit interactive mode on release */
		reset_cursor_mode(server);
	} else {
		/* Focus hovered client on button press */
		double sx, sy;
		struct wlr_surface *surface = NULL;
		struct flui_toplevel *toplevel = desktop_toplevel_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
		if (pointer_list_remove(server->sw_toplevels, toplevel)) {
			assert(pointer_list_add_to_head(server->sw_toplevels, toplevel));
		}
		focus_toplevel(toplevel);
	}
}

/* Handle cursor axis movements i.e. scroll wheel movement */
void server_cursor_axis(struct wl_listener *listener, void *data) {
	struct flui_server *server =
	wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	/* Notify clients */
	wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation, event->delta, event->delta_discrete, event->source, event->relative_direction);
}

/* Handle frame events i.e. end of cursor event groups */
void server_cursor_frame(struct wl_listener *listener, void *data) {
	struct flui_server *server =
	wl_container_of(listener, server, cursor_frame);
	/* Notify clients */
	wlr_seat_pointer_notify_frame(server->seat);
}
