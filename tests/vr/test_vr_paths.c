#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sfvr_test.h"
#include "vr_paths.h"

static void test_data_dir(void) {
	char dir[256];

	CHECK(vr_paths_data_dir("/data", "/home/steamos", dir, sizeof(dir)));
	CHECK_STR(dir, "/data/WipEoutVR/");

	CHECK(vr_paths_data_dir("/data/", "/home/steamos", dir, sizeof(dir)));
	CHECK_STR(dir, "/data/WipEoutVR/");

	// unset, empty or relative XDG_DATA_HOME: ~/.local/share
	CHECK(vr_paths_data_dir(NULL, "/home/steamos", dir, sizeof(dir)));
	CHECK_STR(dir, "/home/steamos/.local/share/WipEoutVR/");
	CHECK(vr_paths_data_dir("", "/home/steamos", dir, sizeof(dir)));
	CHECK_STR(dir, "/home/steamos/.local/share/WipEoutVR/");
	CHECK(vr_paths_data_dir("relative/dir", "/home/steamos", dir, sizeof(dir)));
	CHECK_STR(dir, "/home/steamos/.local/share/WipEoutVR/");
	CHECK(vr_paths_data_dir(NULL, "/home/steamos/", dir, sizeof(dir)));
	CHECK_STR(dir, "/home/steamos/.local/share/WipEoutVR/");

	// nothing usable
	CHECK(!vr_paths_data_dir(NULL, NULL, dir, sizeof(dir)));
	CHECK_STR(dir, "");
	CHECK(!vr_paths_data_dir(NULL, "relative", dir, sizeof(dir)));

	// too small
	CHECK(!vr_paths_data_dir("/data", NULL, dir, 8));
	CHECK_STR(dir, "");
	CHECK(!vr_paths_data_dir("/data", NULL, NULL, 8));
}

static void test_make_dir(void) {
	char base[] = "/tmp/vr_paths_test_XXXXXX";
	CHECK(mkdtemp(base) != NULL);

	char path[512];
	snprintf(path, sizeof(path), "%s/a/b/c/", base);
	CHECK(vr_paths_make_dir(path));
	struct stat s;
	CHECK(stat(path, &s) == 0 && S_ISDIR(s.st_mode));
	CHECK(vr_paths_make_dir(path)); // already there

	// a file in the way
	snprintf(path, sizeof(path), "%s/file", base);
	FILE *f = fopen(path, "w");
	CHECK(f != NULL);
	if (f) {
		fclose(f);
	}
	CHECK(!vr_paths_make_dir(path));

	snprintf(path, sizeof(path), "rm -rf %s", base);
	CHECK(system(path) == 0);
}

int main(void) {
	RUN(test_data_dir);
	RUN(test_make_dir);
	return SFVR_TEST_RESULT("vr_paths");
}
