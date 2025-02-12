#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

#include "config.h"

void load_config() {
	const char *home = getenv("HOME");
	if (home == NULL) {
		home = getpwuid(getuid())->pw_dir;
	}
	assert(home);

	char conf_path[256];
	snprintf(conf_path, sizeof(conf_path), "%s/.config/flui/flui.conf", home);

	FILE *file = fopen(conf_path, "r");
	if (!file) {
		return;
	}

	char line[256];
	while (fgets(line, sizeof(line), file)) {
		char *trimmed = line;
		while (*trimmed == ' ' || *trimmed == '\t') {
			trimmed++;
		}
		if (*trimmed == '\0' || *trimmed == '#' || *trimmed == '\n') {
			continue;
		}

		char key[64] = {0};
		char *vmem = malloc(sizeof(char) * 192);
		*vmem = 0;
		char *value = vmem;

		if (sscanf(trimmed, "%63[^=]=%191[^\n]", key, value) == 2) {
			char *end = key + strlen(key) - 1;
			while (end > key && (*end == ' ' || *end == '\t')) {
				*end-- = '\0';
			}
			end = value + strlen(value) - 1;
			while (end > value && (*end == ' ' || *end == '\t')) {
				*end-- = '\0';
			}
			while (*value == ' ' || *value == '\t') {
				value++;
			}

			if (!strcmp(key, "keyboard_layout")) {
				setenv("XKB_DEFAULT_LAYOUT", value, true);
			}
		}
		free(vmem);
	}
	fclose(file);
}
