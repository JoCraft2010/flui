#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_fullscreen_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#define FULLSCREEN_SHELL_VERSION 1

static const struct zwp_fullscreen_shell_v1_interface shell_impl;

static void shell_handle_release(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct wlr_fullscreen_shell_v1 *shell_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwp_fullscreen_shell_v1_interface, &shell_impl));
	return wl_resource_get_user_data(resource);
}

static void fullscreen_shell_surface_handle_commit(struct wlr_surface *surface) {
	if (wlr_surface_has_buffer(surface)) {
		wlr_surface_map(surface);
	}
}

static const struct wlr_surface_role fullscreen_shell_surface_role = {
	.name = "zwp_fullscreen_shell_v1-surface",
	.no_object = true,
	.commit = fullscreen_shell_surface_handle_commit,
};

static void shell_handle_present_surface(struct wl_client *client,
		struct wl_resource *shell_resource,
		struct wl_resource *surface_resource, uint32_t method,
		struct wl_resource *output_resource) {
	struct wlr_fullscreen_shell_v1 *shell = shell_from_resource(shell_resource);
	struct wlr_surface *surface = NULL;
	if (surface_resource != NULL) {
		surface = wlr_surface_from_resource(surface_resource);
	}
	struct wlr_output *output = NULL;
	if (output_resource != NULL) {
		output = wlr_output_from_resource(output_resource);
	}

	if (!wlr_surface_set_role(surface, &fullscreen_shell_surface_role,
			shell_resource, ZWP_FULLSCREEN_SHELL_V1_ERROR_ROLE)) {
		return;
	}

	struct wlr_fullscreen_shell_v1_present_surface_event event = {
		.client = client,
		.surface = surface,
		.method = method,
		.output = output,
	};
	wl_signal_emit_mutable(&shell->events.present_surface, &event);
}

static void shell_handle_present_surface_for_mode(struct wl_client *client,
		struct wl_resource *shell_resource,
		struct wl_resource *surface_resource,
		struct wl_resource *output_resource, int32_t framerate,
		uint32_t feedback_id) {
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);

	if (!wlr_surface_set_role(surface, &fullscreen_shell_surface_role,
			shell_resource, ZWP_FULLSCREEN_SHELL_V1_ERROR_ROLE)) {
		return;
	}

	uint32_t version = wl_resource_get_version(shell_resource);
	struct wl_resource *feedback_resource =
		wl_resource_create(client, NULL, version, feedback_id);
	if (feedback_resource == NULL) {
		wl_resource_post_no_memory(shell_resource);
		return;
	}

	// TODO: add support for mode switch
	zwp_fullscreen_shell_mode_feedback_v1_send_mode_failed(feedback_resource);
	wl_resource_destroy(feedback_resource);
}

static const struct zwp_fullscreen_shell_v1_interface shell_impl = {
	.release = shell_handle_release,
	.present_surface = shell_handle_present_surface,
	.present_surface_for_mode = shell_handle_present_surface_for_mode,
};

static void shell_bind(struct wl_client *client, void *data, uint32_t version,
		uint32_t id) {
	struct wlr_fullscreen_shell_v1 *shell = data;

	struct wl_resource *resource = wl_resource_create(client,
		&zwp_fullscreen_shell_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &shell_impl, shell, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_fullscreen_shell_v1 *shell =
		wl_container_of(listener, shell, display_destroy);
	wl_signal_emit_mutable(&shell->events.destroy, shell);

	assert(wl_list_empty(&shell->events.destroy.listener_list));
	assert(wl_list_empty(&shell->events.present_surface.listener_list));

	wl_list_remove(&shell->display_destroy.link);
	wl_global_destroy(shell->global);
	free(shell);
}

struct wlr_fullscreen_shell_v1 *wlr_fullscreen_shell_v1_create(
		struct wl_display *display) {
	struct wlr_fullscreen_shell_v1 *shell = calloc(1, sizeof(*shell));
	if (shell == NULL) {
		return NULL;
	}
	wl_signal_init(&shell->events.destroy);
	wl_signal_init(&shell->events.present_surface);

	shell->global = wl_global_create(display,
		&zwp_fullscreen_shell_v1_interface, FULLSCREEN_SHELL_VERSION,
		shell, shell_bind);
	if (shell->global == NULL) {
		free(shell);
		return NULL;
	}

	shell->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &shell->display_destroy);

	return shell;
}
