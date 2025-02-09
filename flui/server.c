#include "server.h"

struct flui_server server_setup(void) {
	struct flui_server server = {0};

	/* Create libwayland display */
	server.wl_display = wl_display_create();
	/* Create wlroots backend */
	server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.wl_display), NULL);
	assert(server.backend != NULL);

	/* Create wlroots renderer */
	server.renderer = wlr_renderer_autocreate(server.backend);
	assert(server.renderer != NULL);

	wlr_renderer_init_wl_display(server.renderer, server.wl_display);

	/* Create memory allocator */
	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	assert(server.allocator != NULL);

	/* Create wlroots interfaces */
	wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);

	/* Create an output layout */
	server.output_layout = wlr_output_layout_create(server.wl_display);

	return server;
}

void cleanup_server(struct flui_server *server) {
	wl_display_destroy_clients(server->wl_display);

	wl_list_remove(&server->new_xdg_toplevel.link);
	wl_list_remove(&server->new_xdg_popup.link);

	wl_list_remove(&server->cursor_motion.link);
	wl_list_remove(&server->cursor_motion_absolute.link);
	wl_list_remove(&server->cursor_button.link);
	wl_list_remove(&server->cursor_axis.link);
	wl_list_remove(&server->cursor_frame.link);

	wl_list_remove(&server->new_input.link);
	wl_list_remove(&server->request_cursor.link);
	wl_list_remove(&server->request_set_selection.link);

	wl_list_remove(&server->new_output.link);

	wlr_scene_node_destroy(&server->scene->tree.node);
	wlr_xcursor_manager_destroy(server->cursor_mgr);
	wlr_cursor_destroy(server->cursor);
	wlr_allocator_destroy(server->allocator);
	wlr_renderer_destroy(server->renderer);
	wlr_backend_destroy(server->backend);
	wl_display_destroy(server->wl_display);
}
