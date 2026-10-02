#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "vr_paths.h"

bool vr_paths_data_dir(const char *xdg_data_home, const char *home, char *out, size_t n) {
	if (!out || n == 0) {
		return false;
	}
	out[0] = '\0';

	int written = -1;
	if (xdg_data_home && xdg_data_home[0] == '/') {
		bool slash = xdg_data_home[strlen(xdg_data_home) - 1] == '/';
		written = snprintf(out, n, "%s%s%s/", xdg_data_home, slash ? "" : "/", VR_DATA_DIR_NAME);
	}
	else if (home && home[0] == '/') {
		bool slash = home[strlen(home) - 1] == '/';
		written = snprintf(out, n, "%s%s.local/share/%s/", home, slash ? "" : "/", VR_DATA_DIR_NAME);
	}

	if (written < 0 || (size_t)written >= n) {
		out[0] = '\0';
		return false;
	}
	return true;
}

bool vr_paths_make_dir(const char *path) {
	char tmp[1024];
	size_t len = strlen(path);
	if (len == 0 || len >= sizeof(tmp)) {
		return false;
	}
	memcpy(tmp, path, len + 1);

	for (size_t i = 1; i < len; i++) {
		if (tmp[i] == '/') {
			tmp[i] = '\0';
			if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
				return false;
			}
			tmp[i] = '/';
		}
	}
	if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
		return false;
	}

	struct stat s;
	return stat(tmp, &s) == 0 && S_ISDIR(s.st_mode);
}

const char *vr_paths_runtime_data_dir(void) {
	static char dir[1024];
	static bool resolved = false;
	if (!resolved) {
		resolved = true;
		if (!vr_paths_data_dir(getenv("XDG_DATA_HOME"), getenv("HOME"), dir, sizeof(dir)) || !vr_paths_make_dir(dir)) {
			dir[0] = '\0';
		}
	}
	return dir;
}
