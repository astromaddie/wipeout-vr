#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sfvr_test.h"
#include "vr_log.h"
#include "vr_settings.h"

static void quiet(const char *line) { (void)line; }

static double get(const vr_settings_t *s, const char *name) {
	return vr_settings_get(s, name);
}

static double file_value(const vr_settings_t *s, const char *name) {
	return s->file_values[vr_settings_index(name)];
}

// Parse `text` into a fresh settings struct; returns the number of repairs.
static int parse(vr_settings_t *s, const char *text) {
	memset(s, 0, sizeof(*s));
	vr_settings_defaults(s);
	int repairs = vr_settings_parse(s, text, strlen(text));
	for (size_t i = 0; i < vr_settings_count(); i++) {
		s->values[i] = s->file_values[i];
	}
	return repairs;
}

static char *format(const vr_settings_t *s) {
	char *text = vr_settings_format(s, NULL);
	return text ? text : strdup("");
}

static bool has(const char *haystack, const char *needle) {
	return strstr(haystack, needle) != NULL;
}

static char *read_all(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		return strdup("");
	}
	char *buf = calloc(1, 1 << 16);
	size_t n = fread(buf, 1, (1 << 16) - 1, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

static void write_all(const char *path, const char *text) {
	FILE *f = fopen(path, "wb");
	if (f) {
		fputs(text, f);
		fclose(f);
	}
}

static const char *no_env(const char *name) {
	(void)name;
	return NULL;
}


static void test_registry(void) {
	// the standard keys the doc's section 7 lists, and the port's own
	const char *names[] = {
		"world_scale", "follow_vehicle_rotation", "horizon_lock", "hud_mode", "hud_distance", "hud_scale",
		"force_mono", "panel_distance", "panel_width",
		"resolution_scale", "refresh_rate", "probe_seconds", "force_render", "diag_yaw", "dump_frame", "timing",
		"turn_mode", "snap_angle", "vignette", "seated", "haptics", "dominant_hand"
	};
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		CHECK(vr_settings_index(names[i]) >= 0);
	}
	CHECK_INT(vr_settings_index("nonsense"), -1);
	CHECK_INT(vr_settings_index(NULL), -1);
	CHECK(vr_settings_count() <= VR_SETTINGS_MAX_KEYS);
	CHECK_INT(vr_settings_count(), sfvr_settings_count() + 4);

	// env names
	char name[64];
	CHECK(sfvr_settings_env_name("WIPEOUT", "resolution_scale", name, sizeof(name)));
	CHECK_STR(name, "WIPEOUT_VR_RESOLUTION_SCALE");
}

static void test_defaults_and_creation(void) {
	vr_settings_t s;
	CHECK_INT(parse(&s, ""), (int)vr_settings_count() + 1); // every key and the version
	CHECK_NEAR(get(&s, "snap_angle"), 30, 1e-9);
	CHECK_NEAR(get(&s, "resolution_scale"), 1.0, 1e-9);
	CHECK_NEAR(get(&s, "refresh_rate"), 90, 1e-9);
	CHECK_NEAR(get(&s, "world_scale"), 128, 1e-9);
	CHECK_NEAR(get(&s, "force_mono"), 0, 1e-9);
	CHECK_NEAR(get(&s, "panel_distance"), 2.0, 1e-9);
	CHECK_NEAR(get(&s, "probe_seconds"), 0, 1e-9);
	CHECK_NEAR(get(&s, "hud_mode"), 0, 1e-9);
	CHECK_NEAR(get(&s, "nonsense"), 0, 1e-9);

	char *text = format(&s);
	CHECK(has(text, "version = 1\n"));
	CHECK(has(text, "[vr]\n"));
	CHECK(has(text, "turn_mode = \"snap\"\n"));
	CHECK(has(text, "snap_angle = 30\n"));
	CHECK(has(text, "resolution_scale = 1.0\n"));
	CHECK(has(text, "vignette = false\n"));
	CHECK(has(text, "world_scale = 128.0\n"));
	CHECK(has(text, "hud_mode = \"near\"\n"));
	CHECK(has(text, "WIPEOUT_VR_<KEY>"));

	// what was written reads back with nothing to repair, and is a fixed point
	vr_settings_t back;
	CHECK_INT(parse(&back, text), 0);
	for (size_t i = 0; i < vr_settings_count(); i++) {
		CHECK_NEAR(back.file_values[i], s.file_values[i], 1e-6);
	}
	char *again = format(&back);
	CHECK_STR(again, text);
	free(text);
	free(again);
	vr_settings_free(&s);
	vr_settings_free(&back);
}

static void test_missing_keys_get_defaults(void) {
	vr_settings_t s;
	const char *file =
		"version = 1\n"
		"\n"
		"[vr]\n"
		"snap_angle = 45   # my favourite\n"
		"turn_mode = \"smooth\"\n";
	int repairs = parse(&s, file);
	CHECK_INT(repairs, (int)vr_settings_count() - 2);
	CHECK_NEAR(get(&s, "snap_angle"), 45, 1e-9);
	CHECK_NEAR(get(&s, "turn_mode"), 1, 1e-9);
	CHECK_NEAR(get(&s, "resolution_scale"), 1.0, 1e-9);

	char *text = format(&s);
	// the user's lines are untouched, comment included; the rest is added in [vr]
	CHECK(has(text, "snap_angle = 45   # my favourite\n"));
	CHECK(has(text, "turn_mode = \"smooth\"\n"));
	CHECK(has(text, "refresh_rate = 90\n"));
	CHECK(strstr(text, "refresh_rate") > strstr(text, "[vr]"));
	vr_settings_t back;
	CHECK_INT(parse(&back, text), 0);
	free(text);
	vr_settings_free(&s);
	vr_settings_free(&back);
}

static void test_bad_values_are_clamped_or_defaulted(void) {
	vr_settings_t s;
	const char *file =
		"version = 1\n"
		"[vr]\n"
		"snap_angle = 500\n"             // above the range: 90
		"smooth_turn_speed = 1\n"        // below: 30
		"resolution_scale = 0.1\n"
		"refresh_rate = 1000\n"
		"hud_distance = 99.5\n"
		"lean_limit = \"fast\"\n"        // not a number: default
		"head_translation = nan\n"       // not usable: default
		"vignette_strength = 2\n"
		"hud_mode = \"banana\"\n"        // unknown enum: default
		"dominant_hand = \"LEFT\"\n"     // case-insensitive
		"turn_mode = 7\n"                // an index is not a name
		"vignette = maybe\n"
		"seated = yes\n"
		"roomscale = TRUE\n"
		"duck_crouch = 0\n"
		"snap_angle_extra = 3\n"         // unknown, kept
		"dump_frame = 12.6\n"            // an int key given a float
		"probe_seconds = -4\n"
		"haptics = 0.25\n";
	int repairs = parse(&s, file);
	CHECK(repairs > 0);
	CHECK_NEAR(get(&s, "snap_angle"), 90, 1e-9);
	CHECK_NEAR(get(&s, "smooth_turn_speed"), 30, 1e-9);
	CHECK_NEAR(get(&s, "resolution_scale"), 0.75, 1e-6);
	CHECK_NEAR(get(&s, "refresh_rate"), 144, 1e-9);
	CHECK_NEAR(get(&s, "hud_distance"), 20.0, 1e-6);
	CHECK_NEAR(get(&s, "lean_limit"), 0.35, 1e-6);
	CHECK_NEAR(get(&s, "head_translation"), 1.0, 1e-6);
	CHECK_NEAR(get(&s, "vignette_strength"), 1.0, 1e-6);
	CHECK_NEAR(get(&s, "hud_mode"), 0, 1e-9);
	CHECK_NEAR(get(&s, "dominant_hand"), 1, 1e-9);
	CHECK_NEAR(get(&s, "turn_mode"), 0, 1e-9);
	CHECK_NEAR(get(&s, "vignette"), 0, 1e-9);
	CHECK_NEAR(get(&s, "seated"), 1, 1e-9);
	CHECK_NEAR(get(&s, "roomscale"), 1, 1e-9);
	CHECK_NEAR(get(&s, "duck_crouch"), 0, 1e-9);
	CHECK_NEAR(get(&s, "dump_frame"), 13, 1e-9);
	CHECK_NEAR(get(&s, "probe_seconds"), 0, 1e-9);
	CHECK_NEAR(get(&s, "haptics"), 0.25, 1e-6);

	// the repaired file shows the repaired values and keeps the unknown key
	char *text = format(&s);
	CHECK(has(text, "snap_angle = 90\n"));
	CHECK(has(text, "resolution_scale = 0.75\n"));
	CHECK(has(text, "lean_limit = 0.35\n"));
	CHECK(has(text, "hud_mode = \"near\"\n"));
	CHECK(has(text, "dominant_hand = \"LEFT\"\n")); // valid as it was: untouched
	CHECK(has(text, "snap_angle_extra = 3\n"));
	CHECK(!has(text, "snap_angle = 500"));
	vr_settings_t back;
	CHECK_INT(parse(&back, text), 0);
	CHECK_NEAR(get(&back, "snap_angle"), 90, 1e-9);
	free(text);
	vr_settings_free(&s);
	vr_settings_free(&back);
}

static void test_unknown_content_is_kept(void) {
	vr_settings_t s;
	const char *file =
		"# my notes\n"
		"title = \"hello\"\n"
		"\n"
		"[game]\n"
		"snap_angle = 12\n"             // another table's key with a vr name
		"name = 'x'\n"
		"\n"
		"[vr]\n"
		"mystery = 5\n"
		"# a comment inside\n"
		"snap_angle = 60\n"
		"future_flag = true\n"
		"\n"
		"[later]\n"
		"a = 1\n";
	parse(&s, file);
	CHECK_NEAR(get(&s, "snap_angle"), 60, 1e-9); // the [vr] one, not [game]'s

	char *text = format(&s);
	CHECK(has(text, "# my notes\n"));
	CHECK(has(text, "title = \"hello\"\n"));
	CHECK(has(text, "[game]\nsnap_angle = 12\nname = 'x'\n"));
	CHECK(has(text, "mystery = 5\n"));
	CHECK(has(text, "# a comment inside\n"));
	CHECK(has(text, "future_flag = true\n"));
	CHECK(has(text, "[later]\na = 1\n"));
	// the added keys went into [vr], before [later]
	CHECK(strstr(text, "refresh_rate") > strstr(text, "[vr]"));
	CHECK(strstr(text, "refresh_rate") < strstr(text, "[later]"));
	CHECK(strstr(text, "version = 1") != NULL);
	CHECK(strstr(text, "version = 1") < strstr(text, "[game]"));

	// the first lines stay on top: the version goes after the comment block
	CHECK(strncmp(text, "# my notes\n", 11) == 0);
	free(text);
	vr_settings_free(&s);
}

static void test_version(void) {
	vr_settings_t s;
	char *text;

	parse(&s, "[vr]\nsnap_angle = 30\n");
	CHECK_INT(s.file_version, 0);
	text = format(&s);
	CHECK(has(text, "version = 1\n"));
	CHECK(strstr(text, "version = 1") < strstr(text, "[vr]"));
	free(text);
	vr_settings_free(&s);

	parse(&s, "version = 0\n[vr]\n");
	text = format(&s);
	CHECK(has(text, "version = 1\n"));
	CHECK(!has(text, "version = 0"));
	free(text);
	vr_settings_free(&s);

	// a file from a newer build keeps its version
	parse(&s, "version = 7\n[vr]\n");
	CHECK_INT(s.file_version, 7);
	text = format(&s);
	CHECK(has(text, "version = 7\n"));
	free(text);
	vr_settings_free(&s);
}

static void test_duplicates_and_odd_files(void) {
	vr_settings_t s;
	char *text;

	// the first of a duplicated key wins; the second is dropped
	parse(&s, "version = 1\n[vr]\nsnap_angle = 20\nsnap_angle = 80\n");
	CHECK_NEAR(get(&s, "snap_angle"), 20, 1e-9);
	text = format(&s);
	CHECK(has(text, "snap_angle = 20\n"));
	CHECK(!has(text, "snap_angle = 80"));
	free(text);
	vr_settings_free(&s);

	// CRLF line endings and no final newline
	parse(&s, "version = 1\r\n[vr]\r\nsnap_angle = 75\r\nvignette = true");
	CHECK_NEAR(get(&s, "snap_angle"), 75, 1e-9);
	CHECK_NEAR(get(&s, "vignette"), 1, 1e-9);
	text = format(&s);
	CHECK(has(text, "snap_angle = 75\r\n"));
	vr_settings_t back;
	CHECK_INT(parse(&back, text), 0);
	free(text);
	vr_settings_free(&s);
	vr_settings_free(&back);

	// garbage lines are not keys and not an error
	parse(&s, "version = 1\n[vr]\n= = =\nsnap_angle 45\nsnap_angle = 45\n");
	CHECK_NEAR(get(&s, "snap_angle"), 45, 1e-9);
	vr_settings_free(&s);

	// a '#' inside a quoted value is not a comment
	parse(&s, "[vr]\nhud_mode = \"fa#r\"\n");
	CHECK_NEAR(get(&s, "hud_mode"), 0, 1e-9); // invalid enum: default
	vr_settings_free(&s);
}

static const char *env_names[8];
static const char *env_values[8];
static int env_count;

static const char *fake_getenv(const char *name) {
	for (int i = 0; i < env_count; i++) {
		if (strcmp(env_names[i], name) == 0) {
			return env_values[i];
		}
	}
	return NULL;
}

static void test_env_overrides(void) {
	vr_settings_t s;
	parse(&s, "version = 1\n[vr]\nresolution_scale = 1.1\nsnap_angle = 45\n");

	env_count = 0;
#define SET_ENV(n, v) do { env_names[env_count] = n; env_values[env_count] = v; env_count++; } while (0)
	SET_ENV("WIPEOUT_VR_RESOLUTION_SCALE", "1.25");
	SET_ENV("WIPEOUT_VR_SNAP_ANGLE", "1000");        // clamped
	SET_ENV("WIPEOUT_VR_PROBE_SECONDS", "5");
	SET_ENV("WIPEOUT_VR_FORCE_RENDER", "1");
	SET_ENV("WIPEOUT_VR_TURN_MODE", "Smooth");
	SET_ENV("WIPEOUT_VR_HUD_SCALE", "not a number"); // ignored
	SET_ENV("WIPEOUT_VR_WORLD_SCALE", "");           // empty: ignored
	SET_ENV("OTHER_VR_REFRESH_RATE", "72");          // another prefix: ignored
#undef SET_ENV

	vr_log_set_sink(quiet);
	int applied = vr_settings_apply_env(&s, fake_getenv);
	CHECK_INT(applied, 5);
	CHECK_NEAR(get(&s, "resolution_scale"), 1.25, 1e-6);
	CHECK_NEAR(get(&s, "snap_angle"), 90, 1e-9);
	CHECK_NEAR(get(&s, "probe_seconds"), 5, 1e-9);
	CHECK_NEAR(get(&s, "force_render"), 1, 1e-9);
	CHECK_NEAR(get(&s, "turn_mode"), 1, 1e-9);
	CHECK_NEAR(get(&s, "hud_scale"), 100, 1e-9);
	CHECK_NEAR(get(&s, "world_scale"), 128, 1e-9);
	CHECK_NEAR(get(&s, "refresh_rate"), 90, 1e-9);

	// the file's values are not touched, and so are not written back
	CHECK_NEAR(file_value(&s, "resolution_scale"), 1.1, 1e-6);
	CHECK_NEAR(file_value(&s, "snap_angle"), 45, 1e-9);
	CHECK_NEAR(file_value(&s, "probe_seconds"), 0, 1e-9);
	char *text = format(&s);
	CHECK(has(text, "resolution_scale = 1.1\n"));
	CHECK(has(text, "snap_angle = 45\n"));
	CHECK(has(text, "probe_seconds = 0.0\n"));
	free(text);

	CHECK_INT(vr_settings_apply_env(&s, no_env), 0);
	vr_settings_free(&s);
}

static void test_set(void) {
	vr_settings_t s;
	parse(&s, "");
	vr_settings_set(&s, "snap_angle", 500);
	CHECK_NEAR(get(&s, "snap_angle"), 90, 1e-9);
	CHECK_NEAR(file_value(&s, "snap_angle"), 90, 1e-9);
	vr_settings_set(&s, "vignette", 7);
	CHECK_NEAR(get(&s, "vignette"), 1, 1e-9);
	vr_settings_set(&s, "turn_mode", 9);
	CHECK_NEAR(get(&s, "turn_mode"), 2, 1e-9);
	vr_settings_set(&s, "nonsense", 1); // no effect, no crash
	vr_settings_free(&s);
}

static void test_load_and_save_files(void) {
	char dir[] = "/tmp/vr_settings_test_XXXXXX";
	CHECK(mkdtemp(dir) != NULL);
	char path[512], tmp[520];
	snprintf(path, sizeof(path), "%s/vr.toml", dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);

	vr_log_set_sink(quiet);
	vr_settings_t s;

	// no file: created with every key
	CHECK(vr_settings_load(&s, path));
	CHECK_NEAR(get(&s, "snap_angle"), 30, 1e-9);
	char *text = read_all(path);
	CHECK(has(text, "version = 1\n"));
	CHECK(has(text, "[vr]\n"));
	CHECK(has(text, "snap_angle = 30\n"));
	CHECK(has(text, "world_scale = 128.0\n"));
	free(text);
	CHECK(access(tmp, F_OK) != 0); // the temp file was renamed away
	struct stat st;
	CHECK(stat(path, &st) == 0 && st.st_size > 100);
	vr_settings_free(&s);

	// the user edits it: a bad value, an unknown key, one missing key
	write_all(path,
		"version = 1\n[vr]\nsnap_angle = 5\nmystery = \"keep me\"\n"
		"turn_mode = \"snap\"\n");
	CHECK(vr_settings_load(&s, path));
	CHECK_NEAR(get(&s, "snap_angle"), 15, 1e-9);
	text = read_all(path);
	CHECK(has(text, "snap_angle = 15\n"));
	CHECK(has(text, "mystery = \"keep me\"\n"));
	CHECK(has(text, "refresh_rate = 90\n"));
	free(text);
	vr_settings_free(&s);

	// a complete, valid file is not rewritten
	CHECK(vr_settings_load(&s, path));
	vr_settings_free(&s);
	struct stat before, after;
	CHECK(stat(path, &before) == 0);
	CHECK(vr_settings_load(&s, path));
	CHECK(stat(path, &after) == 0);
	CHECK(before.st_ino == after.st_ino); // same file: rename would have changed it
	vr_settings_free(&s);

	// save() changes only what changed in memory
	CHECK(vr_settings_load(&s, path));
	vr_settings_set(&s, "resolution_scale", 1.3);
	CHECK(vr_settings_save(&s));
	vr_settings_free(&s);
	text = read_all(path);
	CHECK(has(text, "resolution_scale = 1.3\n"));
	CHECK(has(text, "mystery = \"keep me\"\n"));
	free(text);
	CHECK(access(tmp, F_OK) != 0);

	// a failed write leaves the old file and no temp file behind
	char *old = read_all(path);
	CHECK(vr_settings_load(&s, path));
	snprintf(s.path, sizeof(s.path), "%s/no/such/dir/vr.toml", dir);
	CHECK(!vr_settings_save(&s));
	vr_settings_free(&s);
	text = read_all(path);
	CHECK_STR(text, old);
	free(text);
	free(old);

	// a missing directory: load still returns usable defaults, and says it could not write
	snprintf(path, sizeof(path), "%s/no/such/dir/vr.toml", dir);
	CHECK(!vr_settings_load(&s, path));
	CHECK_NEAR(get(&s, "refresh_rate"), 90, 1e-9);
	vr_settings_free(&s);

	char cmd[600];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
	CHECK(system(cmd) == 0);
}

int main(void) {
	vr_log_set_sink(quiet);
	RUN(test_registry);
	RUN(test_defaults_and_creation);
	RUN(test_missing_keys_get_defaults);
	RUN(test_bad_values_are_clamped_or_defaulted);
	RUN(test_unknown_content_is_kept);
	RUN(test_version);
	RUN(test_duplicates_and_odd_files);
	RUN(test_env_overrides);
	RUN(test_set);
	RUN(test_load_and_save_files);
	return SFVR_TEST_RESULT("vr_settings");
}
