#include <stdlib.h>

#include "layout.h"
#include "server.h"

/* Focus toplevel window (keyboard focus only) */
void focus_toplevel(struct flui_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	struct flui_server *server = toplevel->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	if (prev_surface == surface) {
		return;
	}
	if (prev_surface) {
		/* Unfocus previous toplevel */
		struct wlr_xdg_toplevel *prev_toplevel =
		wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
		if (prev_toplevel != NULL) {
			wlr_xdg_toplevel_set_activated(prev_toplevel, false);
		}
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	/* Move the toplevel to the front */
	wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
	/* Activate the new surface */
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
	/* Make the seat keyboard enter the surface */
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}
}

/* Handle surfaces ready to display */
static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	struct flui_toplevel *toplevel = wl_container_of(listener, toplevel, map);

	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);

	focus_toplevel(toplevel);
}

/* Handle removing surfaces from screen */
static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	struct flui_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);

	/* Reset cursor mode if current toplevel is unmapped */
	if (toplevel == toplevel->server->grabbed_toplevel) {
		reset_cursor_mode(toplevel->server);
	}

	wl_list_remove(&toplevel->link);
}

/* Handle new surface commit */
static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
	struct flui_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

	if (toplevel->xdg_toplevel->base->initial_commit) {
		/* Return config, allow the surface to decide own size */
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
	}
}

/* Handle toplevel destruction */
static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
	struct flui_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);

	free(toplevel);
}

/* Begin interactive mode i.e. resizing/moving windows */
static void begin_interactive(struct flui_toplevel *toplevel, enum flui_cursor_mode mode, uint32_t edges) {
	struct flui_server *server = toplevel->server;

	server->grabbed_toplevel = toplevel;
	server->cursor_mode = mode;

	if (mode == FLUI_CURSOR_MOVE) {
		server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
		server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
	} else {
		struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;

		double border_x = (toplevel->scene_tree->node.x + geo_box->x) + ((edges & WLR_EDGE_RIGHT) ? geo_box->width : 0);
		double border_y = (toplevel->scene_tree->node.y + geo_box->y) + ((edges & WLR_EDGE_BOTTOM) ? geo_box->height : 0);
		server->grab_x = server->cursor->x - border_x;
		server->grab_y = server->cursor->y - border_y;

		server->grab_geobox = *geo_box;
		server->grab_geobox.x += toplevel->scene_tree->node.x;
		server->grab_geobox.y += toplevel->scene_tree->node.y;

		server->resize_edges = edges;
	}
}

/* Handle client requests for interactive movements */
static void xdg_toplevel_request_move(struct wl_listener *listener, void *data) {
	struct flui_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
	if (toplevel->xdg_toplevel->pending.maximized) { return; }
	begin_interactive(toplevel, FLUI_CURSOR_MOVE, 0);
}

/* Handle client requests for interactive resizing */
static void xdg_toplevel_request_resize(struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct flui_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
	if (toplevel->xdg_toplevel->pending.maximized) { return; }
	begin_interactive(toplevel, FLUI_CURSOR_RESIZE, event->edges);
}

/* Handle maximizing usually because of a maximize button press */
static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data) {
	struct flui_toplevel *toplevel = wl_container_of(listener, toplevel, request_maximize);
	if (!toplevel->xdg_toplevel->base->initialized) {
		return;
	}

	struct wlr_surface_output *surf_output = wl_container_of(toplevel->xdg_toplevel->base->surface->current_outputs.next, surf_output, link);
	struct wlr_output *output = surf_output->output;
	int width, height;
	wlr_output_effective_resolution(output, &width, &height);
	
	if (width <= 0 || height <= 0) {
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
		return;
	}
	
	if (toplevel->xdg_toplevel->pending.maximized) {
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width / 2, height / 2);
		wlr_scene_node_set_position(&toplevel->scene_tree->node, width / 4, height / 4);
	} else {
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, true);
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width, height);
		wlr_scene_node_set_position(&toplevel->scene_tree->node, 0, 0);
	}
	wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
}

/* Handle request fullscreen TODO */
static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data) {
	struct flui_toplevel *toplevel =
	wl_container_of(listener, toplevel, request_fullscreen);
	if (toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
	}
}

/* Handle creation of new toplevel (application window) */
void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	struct flui_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	/* Allocate a flui_toplevel for this surface */
	struct flui_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	toplevel->server = server;
	toplevel->xdg_toplevel = xdg_toplevel;
	toplevel->scene_tree = wlr_scene_xdg_surface_create(&toplevel->server->scene->tree, xdg_toplevel->base);
	toplevel->scene_tree->node.data = toplevel;
	xdg_toplevel->base->data = toplevel->scene_tree;

	/* Connect handlers to events */
	toplevel->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
	toplevel->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

	toplevel->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

	toplevel->request_move.notify = xdg_toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
	toplevel->request_resize.notify = xdg_toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);
	toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);
	toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);
}

/* Handle new surface commit for popups */
static void xdg_popup_commit(struct wl_listener *listener, void *data) {
	struct flui_popup *popup = wl_container_of(listener, popup, commit);

	if (popup->xdg_popup->base->initial_commit) {
		wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	}
}

/* Handle popup destruction */
static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
	struct flui_popup *popup = wl_container_of(listener, popup, destroy);

	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);

	free(popup);
}

/* Handle creation of popups */
void server_new_xdg_popup(struct wl_listener *listener, void *data) {
	struct wlr_xdg_popup *xdg_popup = data;

	struct flui_popup *popup = calloc(1, sizeof(*popup));
	popup->xdg_popup = xdg_popup;

	/* Add popup to scene graph */
	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	assert(parent != NULL);
	struct wlr_scene_tree *parent_tree = parent->data;
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

	/* Connect handlers to events */
	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}
