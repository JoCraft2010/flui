cairo = dependency('cairo', required: true)

executable(
	'flui',
	['main.c', 'config.c', 'input.c', 'layout.c', 'output.c', 'server.c', 'util.c', protocols_server_header['xdg-shell']],
	dependencies: [wlroots],
	build_by_default: true
)
